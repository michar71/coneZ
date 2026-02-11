#ifndef SIM_WASM_RUNTIME_H
#define SIM_WASM_RUNTIME_H

#include <string>
#include <functional>
#include <chrono>
#include <mutex>

class LedState;
class SensorState;

class SimWasmRuntime {
public:
    using OutputCallback = std::function<void(const std::string &)>;

    SimWasmRuntime();
    ~SimWasmRuntime();

    // Set output callback (thread-safe, called from WASM thread)
    void setOutputCallback(OutputCallback cb);

    // Run a .wasm file (blocks until done or stopped)
    void run(const std::string &wasmPath);

    // Request stop (called from GUI thread)
    void requestStop();

    // Emit text to console (batched — flushes at ~30 Hz to avoid UI flood)
    void emitOutput(const std::string &text);
    void flushOutput();

    bool isStopRequested() const { return m_stopRequested; }

    // Params (inter-task communication)
    int getParam(int id) const;
    void setParam(int id, int val);

private:
    OutputCallback m_outputCb;
    volatile bool m_stopRequested = false;
    int m_params[16] = {};

    // Output batching
    std::mutex m_outputMutex;
    std::string m_outputBuf;
    std::chrono::steady_clock::time_point m_lastFlush;
};

// Thread-local current runtime (for m3_Yield and import functions)
SimWasmRuntime *currentRuntime();
void setCurrentRuntime(SimWasmRuntime *rt);

#endif
