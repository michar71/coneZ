#ifdef INCLUDE_WASM

#include "wasm_wrapper.h"
#include "wasm_internal.h"
#include "m3_env.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "printManager.h"
#include "FS.h"
#include <LittleFS.h>
#include "main.h"
#include "basic_wrapper.h"   // get_basic_param / set_basic_param


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
    strlcpy(wasm_current_path, path, sizeof(wasm_current_path));

    // Load file from LittleFS
    File f = LittleFS.open(path, "r");
    if (!f) {
        printfnl(SOURCE_WASM, "wasm: cannot open %s\n", path);
        wasm_running = false;
        return;
    }

    size_t wasm_size = f.size();
    if (wasm_size == 0) {
        printfnl(SOURCE_WASM, "wasm: %s is empty\n", path);
        f.close();
        wasm_running = false;
        return;
    }

    // Allocate buffer for .wasm binary (must persist during module lifetime)
    uint8_t *wasm_buf = (uint8_t *)malloc(wasm_size);
    if (!wasm_buf) {
        printfnl(SOURCE_WASM, "wasm: alloc failed (%u bytes)\n", (unsigned)wasm_size);
        f.close();
        wasm_running = false;
        return;
    }

    size_t bytes_read = f.read(wasm_buf, wasm_size);
    f.close();

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

    // Load module into runtime (runtime takes ownership)
    result = m3_LoadModule(runtime, module);
    if (result) {
        u32 pages = module->memoryInfo.initPages;
        printfnl(SOURCE_WASM, "wasm: load error: %s (module wants %u pages = %uKB)\n",
                 result, pages, (unsigned)(pages * 64));
        m3_FreeModule(module);
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
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        wasm_running = false;
        return;
    }

    // Look up __line global (exported by bas2wasm-compiled programs)
    IM3Global g_line = m3_FindGlobal(module, "__line");

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

    // Cleanup
    wasm_close_all_files();
    wasm_reset_gamma();
    wasm_string_pool_reset();
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
                // No program queued — kill task to free 16KB stack
                wasm_task_handle = NULL;
                xSemaphoreGive(wasm_mutex);
                vTaskDelete(NULL);
            }
        }
    }
}


// ---------- Public API ----------

void setup_wasm()
{
    wasm_mutex = xSemaphoreCreateMutex();
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
        bool need_task = (wasm_task_handle == NULL);
        xSemaphoreGive(wasm_mutex);

        // Create task if not running (killed after previous program ended to free 16KB stack)
        // Pin to core 1: HWCDC interrupt is on core 1 — cross-core Serial writes corrupt output
        if (need_task)
            xTaskCreatePinnedToCore(wasm_task_fun, "WasmTask", 16384, NULL, 1, &wasm_task_handle, 1);

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
