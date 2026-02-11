#include "compiler_worker.h"
#include "sim_config.h"

#include <QProcess>
#include <QFileInfo>
#include <QTemporaryFile>

CompilerWorker::CompilerWorker(QObject *parent)
    : QObject(parent)
{
}

void CompilerWorker::compile(const QString &inputPath)
{
    QFileInfo fi(inputPath);
    QString ext = fi.suffix().toLower();

    // Create temp output path
    QString tmpWasm = "/tmp/conez_sim_" + fi.baseName() + ".wasm";
    m_tempWasm = tmpWasm;

    QProcess *proc = new QProcess(this);

    connect(proc, &QProcess::readyReadStandardOutput, [this, proc]() {
        emit outputReady(QString::fromLocal8Bit(proc->readAllStandardOutput()));
    });
    connect(proc, &QProcess::readyReadStandardError, [this, proc]() {
        emit outputReady(QString::fromLocal8Bit(proc->readAllStandardError()));
    });

    connect(proc, &QProcess::errorOccurred, [this, proc](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            emit error("Failed to start compiler: " + proc->program() +
                       "\nCheck that it is installed and on PATH, or use --bas2wasm / --clang.");
            proc->deleteLater();
        }
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, proc, tmpWasm](int exitCode, QProcess::ExitStatus status) {
        proc->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0) {
            emit compiled(tmpWasm);
        } else {
            emit error("Compilation failed (exit code " + QString::number(exitCode) + ")");
        }
    });

    if (ext == "bas") {
        // bas2wasm
        QString compiler = QString::fromStdString(simConfig().bas2wasm_path);
        QStringList args = {inputPath, "-o", tmpWasm};
        emit outputReady("$ " + compiler + " " + args.join(" ") + "\n");
        proc->start(compiler, args);
    } else if (ext == "c") {
        // clang --target=wasm32
        QString compiler = QString::fromStdString(simConfig().clang_path);
        QStringList args = {
            "--target=wasm32", "-O2", "-nostdlib",
            "-Wl,--no-entry", "-Wl,--export=setup", "-Wl,--export=loop",
            "-Wl,--allow-undefined",
            "-o", tmpWasm, inputPath
        };
        if (!simConfig().api_header_dir.empty()) {
            args.insert(3, "-I" + QString::fromStdString(simConfig().api_header_dir));
        }
        emit outputReady("$ " + compiler + " " + args.join(" ") + "\n");
        proc->start(compiler, args);
    } else if (ext == "wasm") {
        // Already compiled
        emit compiled(inputPath);
        proc->deleteLater();
    } else {
        emit error("Unknown file type: " + ext);
        proc->deleteLater();
    }
}
