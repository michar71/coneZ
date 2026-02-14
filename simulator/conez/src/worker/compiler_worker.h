#ifndef COMPILER_WORKER_H
#define COMPILER_WORKER_H

#include <QObject>
#include <QString>

class CompilerWorker : public QObject {
    Q_OBJECT
public:
    explicit CompilerWorker(QObject *parent = nullptr);

    // Compile a source file to .wasm. Emits compiled() on success, error() on failure.
    void compile(const QString &inputPath);

signals:
    void outputReady(const QString &text);
    void compiled(const QString &wasmPath);
    void error(const QString &msg);

private:
    bool compileBasEmbedded(const QString &inputPath, const QString &outPath);
    bool compileCEmbedded(const QString &inputPath, const QString &outPath);

    QString m_tempWasm;
};

#endif
