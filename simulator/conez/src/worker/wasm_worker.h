#ifndef WASM_WORKER_H
#define WASM_WORKER_H

#include <QThread>
#include <QString>
#include "sim_wasm_runtime.h"

class WasmWorker : public QThread {
    Q_OBJECT
public:
    explicit WasmWorker(QObject *parent = nullptr);
    ~WasmWorker();

    void startWasm(const QString &wasmPath);
    void stopWasm();
    bool isRunning() const { return m_running; }

    int getParam(int id) const { return m_runtime.getParam(id); }
    void setParam(int id, int val) { m_runtime.setParam(id, val); }

signals:
    void outputReady(const QString &text);
    void finished();

protected:
    void run() override;

private:
    SimWasmRuntime m_runtime;
    QString m_wasmPath;
    volatile bool m_running = false;
};

#endif
