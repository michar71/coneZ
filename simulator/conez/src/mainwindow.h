#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class LedStripWidget;
class ConsoleWidget;
class SensorPanel;
class WasmWorker;
class CompilerWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

public slots:
    Q_INVOKABLE void runFileFromArg(const QString &path);

private slots:
    void onCommand(const QString &cmd);
    void onOpen();
    void onRun();
    void onStop();
    void onCompiled(const QString &wasmPath);
    void onWasmFinished();

private:
    void runFile(const QString &path);
    QString resolvePath(const QString &path) const;

    // CLI commands
    void cmdHelp();
    void cmdDir(const QStringList &args);
    void cmdDel(const QStringList &args);
    void cmdList(const QStringList &args);
    void cmdRen(const QStringList &args);
    void cmdCp(const QStringList &args);
    void cmdMkdir(const QStringList &args);
    void cmdRmdir(const QStringList &args);
    void cmdGrep(const QStringList &args);
    void cmdHexdump(const QStringList &args);
    void cmdMd5(const QStringList &args);
    void cmdSha256(const QStringList &args);
    void cmdDf();
    void cmdClear();
    void cmdParam(const QStringList &args);
    void cmdLed(const QStringList &args);
    void cmdSensors();
    void cmdTime();
    void cmdUptime();
    void cmdVersion();
    void cmdWasm(const QStringList &args);
    void cmdCue(const QStringList &args);
    void cmdMqtt(const QStringList &args);
    void cmdInflate(const QStringList &args);
    void cmdDeflate(const QStringList &args);
    void cmdArtnet(const QStringList &args);

    LedStripWidget *m_leds;
    ConsoleWidget *m_console;
    SensorPanel *m_sensors;
    WasmWorker *m_wasmWorker;
    CompilerWorker *m_compilerWorker;

    QString m_lastFile;
};

#endif
