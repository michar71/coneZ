#include "sim_wasm_runtime.h"
#include "sim_wasm_imports.h"
#include "wasm3.h"
#include "m3_env.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>

#define WASM3_STACK_SIZE (8 * 1024)
#define WASM_YIELD_INTERVAL 1000

// ---- Thread-local current runtime ----
static thread_local SimWasmRuntime *tl_currentRuntime = nullptr;

SimWasmRuntime *currentRuntime() { return tl_currentRuntime; }
void setCurrentRuntime(SimWasmRuntime *rt) { tl_currentRuntime = rt; }

// ---- m3_Yield override ----
static thread_local uint32_t yield_counter = 0;

extern "C" M3Result m3_Yield()
{
    if (++yield_counter >= WASM_YIELD_INTERVAL) {
        yield_counter = 0;
        auto *rt = currentRuntime();
        if (rt) rt->flushOutput();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto *rt = currentRuntime();
    if (rt && rt->isStopRequested()) {
        return m3Err_trapExit;
    }
    return m3Err_none;
}

// ---- Link all imports ----
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
    return m3Err_none;
}

// ---- SimWasmRuntime ----

SimWasmRuntime::SimWasmRuntime() {}
SimWasmRuntime::~SimWasmRuntime() {}

void SimWasmRuntime::setOutputCallback(OutputCallback cb) { m_outputCb = cb; }

// Cap each batch to avoid overwhelming the GUI with huge text inserts.
// In a tight print loop the intermediate lines scroll by too fast to read
// anyway — keeping only the tail matches real serial terminal behavior.
static constexpr size_t MAX_OUTPUT_BATCH = 4096;

void SimWasmRuntime::emitOutput(const std::string &text)
{
    std::lock_guard<std::mutex> lock(m_outputMutex);
    m_outputBuf += text;

    // Trim to tail if buffer grew too large
    if (m_outputBuf.size() > MAX_OUTPUT_BATCH) {
        size_t cut = m_outputBuf.size() - MAX_OUTPUT_BATCH;
        // Find next newline so we don't emit a partial line
        size_t nl = m_outputBuf.find('\n', cut);
        if (nl != std::string::npos)
            m_outputBuf.erase(0, nl + 1);
        else
            m_outputBuf.erase(0, cut);
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFlush);
    if (elapsed.count() >= 33) {  // ~30 Hz
        if (m_outputCb && !m_outputBuf.empty()) {
            m_outputCb(m_outputBuf);
            m_outputBuf.clear();
        }
        m_lastFlush = now;
    }
}

void SimWasmRuntime::flushOutput()
{
    std::lock_guard<std::mutex> lock(m_outputMutex);
    if (m_outputCb && !m_outputBuf.empty()) {
        m_outputCb(m_outputBuf);
        m_outputBuf.clear();
    }
    m_lastFlush = std::chrono::steady_clock::now();
}

void SimWasmRuntime::requestStop()
{
    m_stopRequested = true;
    m_params[0] = 1;  // match firmware: param 0 signals scripts to exit
}

int SimWasmRuntime::getParam(int id) const
{
    if (id < 0 || id > 15) return 0;
    return m_params[id];
}

void SimWasmRuntime::setParam(int id, int val)
{
    if (id >= 0 && id <= 15) m_params[id] = val;
}

void SimWasmRuntime::run(const std::string &wasmPath)
{
    m_stopRequested = false;
    yield_counter = 0;
    std::memset(m_params, 0, sizeof(m_params));
    m_lastFlush = std::chrono::steady_clock::now();
    m_outputBuf.clear();
    setCurrentRuntime(this);

    // Read .wasm file
    FILE *f = fopen(wasmPath.c_str(), "rb");
    if (!f) {
        emitOutput("wasm: cannot open " + wasmPath + "\n");
        setCurrentRuntime(nullptr);
        return;
    }

    fseek(f, 0, SEEK_END);
    long wasm_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (wasm_size <= 0) {
        emitOutput("wasm: file is empty\n");
        fclose(f);
        setCurrentRuntime(nullptr);
        return;
    }

    uint8_t *wasm_buf = (uint8_t *)malloc(wasm_size);
    if (!wasm_buf) {
        emitOutput("wasm: alloc failed\n");
        fclose(f);
        setCurrentRuntime(nullptr);
        return;
    }

    size_t bytes_read = fread(wasm_buf, 1, wasm_size, f);
    fclose(f);

    if ((long)bytes_read != wasm_size) {
        emitOutput("wasm: read error\n");
        free(wasm_buf);
        setCurrentRuntime(nullptr);
        return;
    }

    // Create wasm3 environment and runtime
    IM3Environment env = m3_NewEnvironment();
    if (!env) {
        emitOutput("wasm: env alloc failed\n");
        free(wasm_buf);
        setCurrentRuntime(nullptr);
        return;
    }

    IM3Runtime runtime = m3_NewRuntime(env, WASM3_STACK_SIZE, this);
    if (!runtime) {
        emitOutput("wasm: runtime alloc failed\n");
        m3_FreeEnvironment(env);
        free(wasm_buf);
        setCurrentRuntime(nullptr);
        return;
    }

    // Parse module
    IM3Module module = nullptr;
    M3Result result = m3_ParseModule(env, &module, wasm_buf, wasm_size);
    if (result) {
        emitOutput(std::string("wasm: parse error: ") + result + "\n");
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        setCurrentRuntime(nullptr);
        return;
    }

    // Load module into runtime
    result = m3_LoadModule(runtime, module);
    if (result) {
        emitOutput(std::string("wasm: load error: ") + result + "\n");
        m3_FreeModule(module);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        setCurrentRuntime(nullptr);
        return;
    }

    // Link host imports
    result = link_imports(module);
    if (result) {
        emitOutput(std::string("wasm: link error: ") + result + "\n");
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        setCurrentRuntime(nullptr);
        return;
    }

    // Find entry points
    IM3Function func_setup = nullptr;
    IM3Function func_loop = nullptr;
    IM3Function func_start = nullptr;

    m3_FindFunction(&func_setup, runtime, "setup");
    m3_FindFunction(&func_loop, runtime, "loop");
    m3_FindFunction(&func_start, runtime, "_start");

    if (!func_setup && !func_loop && !func_start)
        m3_FindFunction(&func_start, runtime, "main");

    if (!func_setup && !func_loop && !func_start) {
        emitOutput("wasm: no entry point (setup/loop/_start/main)\n");
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        setCurrentRuntime(nullptr);
        return;
    }

    // Look up __line global for BASIC line tracking
    IM3Global g_line = m3_FindGlobal(module, "__line");
    auto get_basic_line = [&]() -> int {
        if (!g_line) return 0;
        M3TaggedValue val;
        if (m3_GetGlobal(g_line, &val) == m3Err_none)
            return val.value.i32;
        return 0;
    };

    emitOutput("wasm: running " + wasmPath + "\n");

    // Run start section
    result = m3_RunStart(module);
    if (result) {
        emitOutput(std::string("wasm: start section error: ") + result + "\n");
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        free(wasm_buf);
        setCurrentRuntime(nullptr);
        return;
    }

    // Execute
    if (func_setup && func_loop) {
        result = m3_CallV(func_setup);
        if (result) {
            int ln = get_basic_line();
            if (ln) {
                char msg[256];
                snprintf(msg, sizeof(msg), "wasm: setup() error: %s (BASIC line %d)\n", result, ln);
                emitOutput(msg);
            } else {
                emitOutput(std::string("wasm: setup() error: ") + result + "\n");
            }
        } else {
            while (!m_stopRequested) {
                result = m3_CallV(func_loop);
                if (result) {
                    int ln = get_basic_line();
                    if (ln) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "wasm: loop() error: %s (BASIC line %d)\n", result, ln);
                        emitOutput(msg);
                    } else {
                        emitOutput(std::string("wasm: loop() error: ") + result + "\n");
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } else if (func_start) {
        result = m3_CallV(func_start);
        if (result && result != m3Err_trapExit) {
            int ln = get_basic_line();
            if (ln) {
                char msg[256];
                snprintf(msg, sizeof(msg), "wasm: error: %s (BASIC line %d)\n", result, ln);
                emitOutput(msg);
            } else {
                emitOutput(std::string("wasm: error: ") + result + "\n");
            }
        }
    } else if (func_loop) {
        while (!m_stopRequested) {
            result = m3_CallV(func_loop);
            if (result) {
                emitOutput(std::string("wasm: loop() error: ") + result + "\n");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } else if (func_setup) {
        result = m3_CallV(func_setup);
        if (result) {
            emitOutput(std::string("wasm: setup() error: ") + result + "\n");
        }
    }

    // Flush any remaining output
    flushOutput();

    // Cleanup
    wasm_close_all_files();
    wasm_reset_gamma();
    wasm_string_pool_reset();

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    free(wasm_buf);

    if (m_stopRequested)
        emitOutput("wasm: stopped\n");
    else
        emitOutput("wasm: DONE\n");

    setCurrentRuntime(nullptr);
}
