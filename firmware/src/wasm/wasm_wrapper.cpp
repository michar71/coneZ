#ifdef INCLUDE_WASM

#include "wasm_wrapper.h"
#include "wasm_internal.h"
#include "m3_env.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "printManager.h"
#include "main.h"
#include "basic_wrapper.h"   // get_basic_param / set_basic_param
#if d_m3UsePsramMemory
#include "psram.h"
#include "m3_psram_glue.h"
#endif


// WASM runtime stack size (bytes inside wasm3 interpreter)
#define WASM3_STACK_SIZE    (8 * 1024)

// Yield to FreeRTOS every N wasm3 Call opcodes (~1K for responsive stop handling)
#define WASM_YIELD_INTERVAL 1000

// ---------- State ----------

static TaskHandle_t wasm_task_handle = NULL;
static SemaphoreHandle_t wasm_mutex = NULL;
static char next_wasm[256] = {0};
static volatile bool wasm_running = false;
volatile bool wasm_stop_requested = false;
static char wasm_current_path[256] = {0};

// Persistent pre-allocated WASM linear memory (1 page = 64KB).
// DRAM path: allocated at boot (prevents heap fragmentation).
// PSRAM path: lazy-allocated on first wasm_run() (PSRAM allocator doesn't fragment).
// Both paths reuse the block across runs (zeroed, not freed).
static const u32 PREALLOC_PAGES = 1;
#if d_m3UsePsramMemory
// PSRAM path: header in DRAM, DRAM window + PSRAM for linear memory data
static M3MemoryHeader *s_prealloc_hdr = NULL;
static uint8_t *s_prealloc_dram = NULL;
static uint32_t s_prealloc_psram = 0;
#else
// DRAM path: single contiguous block (header + data)
static M3MemoryHeader *s_prealloc_mem = NULL;
#endif


// ---------- Automatic yield via m3_Yield override ----------
// wasm3 declares m3_Yield() as M3_WEAK and calls it on every Call opcode.
// We provide a strong definition that periodically yields to FreeRTOS and
// checks the stop flag so runaway programs can be killed.

static uint32_t yield_counter = 0;

extern "C" M3Result m3_Yield(void)
{
    if (++yield_counter >= WASM_YIELD_INTERVAL) {
        yield_counter = 0;
        vTaskDelay(pdMS_TO_TICKS(1));
        inc_thread_count(xPortGetCoreID());
    }

    // Check stop request — return a trap to abort execution
    if (wasm_stop_requested || get_basic_param(0) == 1) {
        return m3Err_trapExit;
    }

    return m3Err_none;
}


// ---------- Link all imports (dispatcher) ----------

static M3Result link_imports(IM3Module module)
{
    M3Result r;
    if ((r = link_led_imports(module)))      return r;
    if ((r = link_sensor_imports(module)))   return r;
    if ((r = link_datetime_imports(module))) return r;
    if ((r = link_gpio_imports(module)))     return r;
    if ((r = link_system_imports(module)))   return r;
    if ((r = link_file_imports(module)))     return r;
    if ((r = link_io_imports(module)))       return r;
    if ((r = link_math_imports(module)))     return r;
    if ((r = link_format_imports(module)))   return r;
    if ((r = link_string_imports(module)))       return r;
    if ((r = link_compression_imports(module))) return r;
    if ((r = link_deflate_imports(module)))     return r;
    return m3Err_none;
}


// ---------- Run a .wasm file ----------

static void wasm_run(const char *path)
{
    wasm_running = true;
    wasm_stop_requested = false;
    set_basic_param(0, 0);    // clear stale stop flag from previous 'stop' command
    yield_counter = 0;
#if d_m3UsePsramMemory
    m3_psram_yield_ctr = 0;
#endif
    strlcpy(wasm_current_path, path, sizeof(wasm_current_path));

    // Load file from LittleFS
    char fpath[256];
    lfs_path(fpath, sizeof(fpath), path);
    FILE *f = fopen(fpath, "r");
    if (!f) {
        printfnl(SOURCE_WASM, "wasm: cannot open %s\n", path);
        wasm_running = false;
        return;
    }

    size_t wasm_size = fsize(f);
    if (wasm_size == 0) {
        printfnl(SOURCE_WASM, "wasm: %s is empty\n", path);
        fclose(f);
        wasm_running = false;
        return;
    }

    // Allocate buffer for .wasm binary (must persist during module lifetime)
    uint8_t *wasm_buf = (uint8_t *)malloc(wasm_size);
    if (!wasm_buf) {
        printfnl(SOURCE_WASM, "wasm: alloc failed (%u bytes)\n", (unsigned)wasm_size);
        fclose(f);
        wasm_running = false;
        return;
    }

    size_t bytes_read = fread(wasm_buf, 1, wasm_size, f);
    fclose(f);

    if (bytes_read != wasm_size) {
        printfnl(SOURCE_WASM, "wasm: read error (%u/%u)\n", (unsigned)bytes_read, (unsigned)wasm_size);
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    // Create wasm3 environment and runtime
    IM3Environment env = m3_NewEnvironment();
    if (!env) {
        printfnl(SOURCE_WASM, "wasm: env alloc failed\n");
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    IM3Runtime runtime = m3_NewRuntime(env, WASM3_STACK_SIZE, NULL);
    if (!runtime) {
        printfnl(SOURCE_WASM, "wasm: runtime alloc failed\n");
        m3_FreeEnvironment(env);
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    // Parse module
    IM3Module module = NULL;
    M3Result result = m3_ParseModule(env, &module, wasm_buf, wasm_size);
    if (result) {
        printfnl(SOURCE_WASM, "wasm: parse error: %s\n", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    // Inject persistent pre-allocated linear memory into the runtime.  When
    // m3_LoadModule calls ResizeMemory(initPages), it sees numPages already ==
    // initPages, so m3_Realloc(ptr, size, size) returns the same pointer (no-op).
    // The prealloc flag tells ResizeMemory to clone (not realloc/free) on memory.grow,
    // and tells Runtime_Release to skip freeing this block.
#if d_m3UsePsramMemory
    if (module->memoryInfo.initPages == PREALLOC_PAGES) {
        // Lazy-allocate on first run, reuse thereafter
        size_t psram_bytes = PREALLOC_PAGES * d_m3MemPageSize - d_m3PsramDramWindow;
        if (!s_prealloc_hdr) {
            s_prealloc_hdr = (M3MemoryHeader *)calloc(1, sizeof(M3MemoryHeader));
            s_prealloc_dram = (uint8_t *)malloc(d_m3PsramDramWindow);
            s_prealloc_psram = psram_malloc(psram_bytes);
        }
        if (s_prealloc_hdr && s_prealloc_dram && s_prealloc_psram) {
            memset(s_prealloc_dram, 0, d_m3PsramDramWindow);
            psram_memset(s_prealloc_psram, 0, psram_bytes);
            s_prealloc_hdr->dram_buf = s_prealloc_dram;
            s_prealloc_hdr->psram_addr = s_prealloc_psram;
            s_prealloc_hdr->length = PREALLOC_PAGES * d_m3MemPageSize;
            s_prealloc_hdr->runtime = runtime;
            s_prealloc_hdr->prealloc = true;
            runtime->memory.mallocated = s_prealloc_hdr;
            runtime->memory.numPages = PREALLOC_PAGES;
        }
    }
#else
    if (s_prealloc_mem && module->memoryInfo.initPages == PREALLOC_PAGES) {
        // Zero the data portion (header stays intact from initial calloc)
        memset((uint8_t *)s_prealloc_mem + sizeof(M3MemoryHeader), 0,
               PREALLOC_PAGES * d_m3MemPageSize);
        s_prealloc_mem->prealloc = true;
        runtime->memory.mallocated = s_prealloc_mem;
        runtime->memory.numPages = PREALLOC_PAGES;
    }
#endif

    // Load module into runtime (runtime takes ownership)
    result = m3_LoadModule(runtime, module);
    if (result) {
        u32 pages = module->memoryInfo.initPages;
        printfnl(SOURCE_WASM, "wasm: load error: %s (module wants %u pages = %uKB)\n",
                 result, pages, (unsigned)(pages * 64));
        m3_FreeModule(module);
        // prealloc flag in header tells Runtime_Release to skip freeing
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    // Link host imports
    result = link_imports(module);
    if (result) {
        printfnl(SOURCE_WASM, "wasm: link error: %s\n", result);
        // prealloc flag in header tells Runtime_Release to skip freeing
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    // Look up __line global (exported by bas2wasm-compiled programs)
    IM3Global g_line = m3_FindGlobal(module, "__line");

    // Look up _heap_ptr global — initialize low-heap allocator for DIM arrays
    IM3Global g_heap = m3_FindGlobal(module, "_heap_ptr");
    if (g_heap) {
        M3TaggedValue val;
        if (m3_GetGlobal(g_heap, &val) == m3Err_none)
            low_heap_init((uint32_t)val.value.i32);
        else
            low_heap_init(0);
    } else {
        low_heap_init(0);  // old binary — low heap disabled, all goes to string pool
    }

    // Try to find and call setup() then loop(), or fall back to _start() / main()
    IM3Function func_setup = NULL;
    IM3Function func_loop = NULL;
    IM3Function func_start = NULL;

    m3_FindFunction(&func_setup, runtime, "setup");
    m3_FindFunction(&func_loop,  runtime, "loop");
    m3_FindFunction(&func_start, runtime, "_start");

    // If no entry point found, try "main"
    if (!func_setup && !func_loop && !func_start) {
        m3_FindFunction(&func_start, runtime, "main");
    }

    if (!func_setup && !func_loop && !func_start) {
        printfnl(SOURCE_WASM, "wasm: no entry point (setup/loop/_start/main)\n");
        // prealloc flag in header tells Runtime_Release to skip freeing
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    // Helper: read current BASIC line from __line global (0 if unavailable)
    auto get_basic_line = [&]() -> int {
        if (!g_line) return 0;
        M3TaggedValue val;
        if (m3_GetGlobal(g_line, &val) == m3Err_none)
            return val.value.i32;
        return 0;
    };

    printfnl(SOURCE_WASM, "wasm: running %s on Core:%d\n", path, xPortGetCoreID());

    // Run start section if present
    result = m3_RunStart(module);
    if (result) {
        printfnl(SOURCE_WASM, "wasm: start section error: %s\n", result);
        // prealloc flag in header tells Runtime_Release to skip freeing
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    if (func_setup && func_loop) {
        // Arduino-style: call setup() once, then loop() repeatedly
        result = m3_CallV(func_setup);
        if (result) {
            int ln = get_basic_line();
            if (ln) printfnl(SOURCE_WASM, "wasm: setup() error: %s (BASIC line %d)\n", result, ln);
            else    printfnl(SOURCE_WASM, "wasm: setup() error: %s\n", result);
        } else {
            while (!wasm_stop_requested && get_basic_param(0) != 1) {
                result = m3_CallV(func_loop);
                if (result) {
                    int ln = get_basic_line();
                    if (ln) printfnl(SOURCE_WASM, "wasm: loop() error: %s (BASIC line %d)\n", result, ln);
                    else    printfnl(SOURCE_WASM, "wasm: loop() error: %s\n", result);
                    break;
                }
                // Yield if loop() didn't call delay_ms
                vTaskDelay(pdMS_TO_TICKS(1));
                inc_thread_count(xPortGetCoreID());
            }
        }
    } else if (func_start) {
        // Single entry point: _start() or main()
        result = m3_CallV(func_start);
        if (result && result != m3Err_trapExit) {
            int ln = get_basic_line();
            if (ln) printfnl(SOURCE_WASM, "wasm: %s error: %s (BASIC line %d)\n",
                             m3_GetFunctionName(func_start), result, ln);
            else    printfnl(SOURCE_WASM, "wasm: %s error: %s\n",
                             m3_GetFunctionName(func_start), result);
        }
    } else if (func_loop) {
        // loop() only, no setup()
        while (!wasm_stop_requested && get_basic_param(0) != 1) {
            result = m3_CallV(func_loop);
            if (result) {
                int ln = get_basic_line();
                if (ln) printfnl(SOURCE_WASM, "wasm: loop() error: %s (BASIC line %d)\n", result, ln);
                else    printfnl(SOURCE_WASM, "wasm: loop() error: %s\n", result);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            inc_thread_count(xPortGetCoreID());
        }
    } else if (func_setup) {
        // setup() only, no loop()
        result = m3_CallV(func_setup);
        if (result) {
            int ln = get_basic_line();
            if (ln) printfnl(SOURCE_WASM, "wasm: setup() error: %s (BASIC line %d)\n", result, ln);
            else    printfnl(SOURCE_WASM, "wasm: setup() error: %s\n", result);
        }
    }

    // Cleanup — prealloc flag in M3MemoryHeader tells Runtime_Release to skip freeing
    wasm_close_all_files();
    wasm_reset_gamma();
    wasm_string_pool_reset();
    low_heap_reset();
    wasm_current_path[0] = '\0';
    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    free(wasm_buf);
    wasm_running = false;

    if (wasm_stop_requested) {
        printfnl(SOURCE_WASM, "wasm: stopped\n");
    } else {
        printfnl(SOURCE_WASM, "wasm: DONE\n");
    }
}


// ---------- FreeRTOS task ----------

static void wasm_task_fun(void *parameter)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5));
        inc_thread_count(xPortGetCoreID());

        if (xSemaphoreTake(wasm_mutex, portMAX_DELAY) == pdTRUE) {
            if (next_wasm[0] != 0) {
                char local_path[256];
                strncpy(local_path, next_wasm, sizeof(local_path));
                local_path[sizeof(local_path) - 1] = '\0';
                next_wasm[0] = 0;
                xSemaphoreGive(wasm_mutex);

                wasm_run(local_path);
            } else {
                xSemaphoreGive(wasm_mutex);
            }
        }
    }
}


// ---------- Public API ----------

void setup_wasm()
{
    wasm_mutex = xSemaphoreCreateMutex();

    // DRAM prealloc at boot — prevents heap fragmentation from 64KB contiguous block.
    // PSRAM prealloc is lazy (allocated on first wasm_run) since PSRAM doesn't fragment.
#if !d_m3UsePsramMemory
    size_t prealloc_bytes = PREALLOC_PAGES * d_m3MemPageSize + sizeof(M3MemoryHeader);
    s_prealloc_mem = (M3MemoryHeader *)calloc(1, prealloc_bytes);
#endif

    xTaskCreatePinnedToCore(wasm_task_fun, "WasmTask", 10240, NULL, 1, &wasm_task_handle, tskNO_AFFINITY);
}

bool set_wasm_program(const char *path)
{
    // Stop the currently running program and wait for it to finish
    if (wasm_running) {
        wasm_stop_requested = true;
        set_basic_param(0, 1);
        // Clear any previously queued path so it doesn't start between stop and new queue
        if (xSemaphoreTake(wasm_mutex, 1000) == pdTRUE) {
            next_wasm[0] = 0;
            xSemaphoreGive(wasm_mutex);
        }
        while (wasm_running) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    if (xSemaphoreTake(wasm_mutex, 1000) == pdTRUE) {
        strncpy(next_wasm, path, sizeof(next_wasm) - 1);
        next_wasm[sizeof(next_wasm) - 1] = '\0';
        xSemaphoreGive(wasm_mutex);
        return true;
    }
    return false;
}

bool wasm_is_running(void)
{
    return wasm_running;
}

void wasm_request_stop(void)
{
    wasm_stop_requested = true;
}

const char *wasm_get_current_path(void)
{
    return wasm_current_path;
}

#endif // INCLUDE_WASM
