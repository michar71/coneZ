#include "wasm_worker.h"

WasmWorker::WasmWorker(QObject *parent)
    : QThread(parent)
{
    m_runtime.setOutputCallback([this](const std::string &text) {
        emit outputReady(QString::fromStdString(text));
    });
}

WasmWorker::~WasmWorker()
{
    stopWasm();
    wait();
}

void WasmWorker::startWasm(const QString &wasmPath)
{
    if (isRunning()) {
        stopWasm();
        wait();
    }
    m_wasmPath = wasmPath;
    m_running = true;
    start();
}

void WasmWorker::stopWasm()
{
    m_runtime.requestStop();
}

void WasmWorker::run()
{
    m_runtime.run(m_wasmPath.toStdString());
    m_running = false;
    emit finished();
}
