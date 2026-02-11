#include "mainwindow.h"
#include "led_strip_widget.h"
#include "console_widget.h"
#include "sensor_panel.h"
#include "wasm_worker.h"
#include "compiler_worker.h"
#include "sim_config.h"
#include "led_state.h"
#include "sensor_state.h"

#include <QToolBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QAction>
#include <QDateTime>
#include <chrono>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("ConeZ Simulator");
    resize(1100, 700);

    // Workers
    m_wasmWorker = new WasmWorker(this);
    m_compilerWorker = new CompilerWorker(this);

    // Toolbar
    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);

    auto *openAct = toolbar->addAction("Open");
    auto *runAct = toolbar->addAction("Run");
    auto *stopAct = toolbar->addAction("Stop");

    connect(openAct, &QAction::triggered, this, &MainWindow::onOpen);
    connect(runAct, &QAction::triggered, this, &MainWindow::onRun);
    connect(stopAct, &QAction::triggered, this, &MainWindow::onStop);

    // Main layout
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(2, 2, 2, 2);

    // Horizontal splitter: LEDs + Sensors
    auto *hSplit = new QSplitter(Qt::Horizontal);

    m_leds = new LedStripWidget;
    m_sensors = new SensorPanel;

    hSplit->addWidget(m_leds);
    hSplit->addWidget(m_sensors);
    hSplit->setStretchFactor(0, 3);
    hSplit->setStretchFactor(1, 1);

    // Vertical splitter: top (LEDs+sensors) + bottom (console)
    auto *vSplit = new QSplitter(Qt::Vertical);
    m_console = new ConsoleWidget;

    vSplit->addWidget(hSplit);
    vSplit->addWidget(m_console);
    vSplit->setStretchFactor(0, 2);
    vSplit->setStretchFactor(1, 1);

    vbox->addWidget(vSplit);

    // Connections
    connect(m_console, &ConsoleWidget::commandEntered, this, &MainWindow::onCommand);

    connect(m_wasmWorker, &WasmWorker::outputReady, m_console, &ConsoleWidget::appendText);
    connect(m_wasmWorker, &WasmWorker::finished, this, &MainWindow::onWasmFinished);

    connect(m_compilerWorker, &CompilerWorker::outputReady, m_console, &ConsoleWidget::appendText);
    connect(m_compilerWorker, &CompilerWorker::compiled, this, &MainWindow::onCompiled);
    connect(m_compilerWorker, &CompilerWorker::error, [this](const QString &msg) {
        m_console->appendText("ERROR: " + msg + "\n");
    });

    // Initialize sandbox directory
    QDir().mkpath(QString::fromStdString(simConfig().sandbox_path));

    m_console->appendText("ConeZ Simulator ready.\n");
    m_console->appendText("Data: " + QString::fromStdString(simConfig().sandbox_path) + "\n");
    m_console->appendText("Type ? for help.\n\n");
}

void MainWindow::runFileFromArg(const QString &path)
{
    runFile(resolvePath(path));
}

QString MainWindow::resolvePath(const QString &path) const
{
    // If it exists as-is (absolute or relative to cwd), use it
    if (QFileInfo::exists(path))
        return QFileInfo(path).absoluteFilePath();

    // If path starts with /, try sandbox + path (firmware-style paths)
    if (path.startsWith('/')) {
        QString sandboxed = QString::fromStdString(simConfig().sandbox_path) + path;
        if (QFileInfo::exists(sandboxed))
            return sandboxed;
    }

    // Try sandbox + / + path for bare filenames like "test.bas"
    QString sandboxed = QString::fromStdString(simConfig().sandbox_path) + "/" + path;
    if (QFileInfo::exists(sandboxed))
        return sandboxed;

    // Return as-is, let caller handle missing file
    return path;
}

void MainWindow::onCommand(const QString &cmd)
{
    QStringList parts = cmd.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;

    QString verb = parts[0].toLower();

    if (verb == "?" || verb == "help") {
        cmdHelp();
    } else if (verb == "run" && parts.size() >= 2) {
        runFile(resolvePath(parts[1]));
    } else if (verb == "run") {
        onRun();
    } else if (verb == "stop") {
        onStop();
    } else if (verb == "open") {
        onOpen();
    } else if (verb == "dir") {
        cmdDir();
    } else if (verb == "del") {
        cmdDel(parts);
    } else if (verb == "list") {
        cmdList(parts);
    } else if (verb == "ren") {
        cmdRen(parts);
    } else if (verb == "param") {
        cmdParam(parts);
    } else if (verb == "led") {
        cmdLed();
    } else if (verb == "sensors") {
        cmdSensors();
    } else if (verb == "time") {
        cmdTime();
    } else if (verb == "uptime") {
        cmdUptime();
    } else if (verb == "version") {
        cmdVersion();
    } else if (verb == "wasm") {
        cmdWasm(parts);
    } else {
        m_console->appendText("Unknown command: " + verb + ". Type ? for help.\n");
    }
}

void MainWindow::onOpen()
{
    QString startDir = QString::fromStdString(simConfig().sandbox_path);
    QString path = QFileDialog::getOpenFileName(this, "Open Script",
        startDir, "Scripts (*.bas *.c *.wasm);;All Files (*)");
    if (!path.isEmpty()) {
        m_lastFile = path;
        runFile(path);
    }
}

void MainWindow::onRun()
{
    if (!m_lastFile.isEmpty()) {
        runFile(m_lastFile);
    } else {
        onOpen();
    }
}

void MainWindow::onStop()
{
    if (m_wasmWorker->isRunning()) {
        m_console->appendText("Stopping...\n");
        m_wasmWorker->stopWasm();
    }
}

void MainWindow::runFile(const QString &path)
{
    // Stop any running program
    if (m_wasmWorker->isRunning()) {
        m_wasmWorker->stopWasm();
        m_wasmWorker->wait(3000);
    }

    m_lastFile = path;
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();

    // Reset LED state
    auto &cfg = simConfig();
    ledState().resize(cfg.led_count1, cfg.led_count2, cfg.led_count3, cfg.led_count4);

    if (ext == "wasm") {
        onCompiled(path);
    } else {
        m_compilerWorker->compile(path);
    }
}

void MainWindow::onCompiled(const QString &wasmPath)
{
    m_wasmWorker->startWasm(wasmPath);
}

void MainWindow::onWasmFinished()
{
    // Nothing extra needed — output already shown via signal
}

// ---- CLI Commands ----

void MainWindow::cmdHelp()
{
    m_console->appendText(
        "Commands:\n"
        "  ?                    Show this help\n"
        "  run <file>           Run script (.bas, .c, .wasm)\n"
        "  stop                 Stop running program\n"
        "  open                 Open file dialog\n"
        "  dir                  List files in data directory\n"
        "  del <file>           Delete file\n"
        "  list <file>          Show file contents\n"
        "  ren <old> <new>      Rename file\n"
        "  param <id> [value]   Get/set script parameter (0-15)\n"
        "  led                  Show LED configuration\n"
        "  sensors              Show sensor values\n"
        "  time                 Show current time\n"
        "  uptime               Show time since start\n"
        "  version              Show version info\n"
        "  wasm [status]        Show WASM runtime status\n"
        "  wasm info <file>     Show WASM file info\n"
        "\n"
    );
}

void MainWindow::cmdDir()
{
    QDir dir(QString::fromStdString(simConfig().sandbox_path));
    if (!dir.exists()) {
        m_console->appendText("Data directory not found.\n");
        return;
    }

    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                               QDir::Name | QDir::DirsFirst);
    QString out;
    qint64 totalSize = 0;
    int fileCount = 0;
    for (const auto &fi : entries) {
        QString name = ("/" + fi.fileName()).leftJustified(30);
        if (fi.isDir()) {
            out += "  " + name + " <DIR>\n";
        } else {
            out += "  " + name + " " + QString::number(fi.size()) + "\n";
            totalSize += fi.size();
            fileCount++;
        }
    }
    out += QString("\n  %1 file(s), %2 bytes\n").arg(fileCount).arg(totalSize);
    m_console->appendText(out);
}

void MainWindow::cmdDel(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: del <filename>\n");
        return;
    }
    QString path = resolvePath(args[1]);
    QFileInfo fi(path);
    if (!fi.exists()) {
        m_console->appendText("File not found: " + args[1] + "\n");
        return;
    }
    if (fi.fileName() == "config.ini") {
        m_console->appendText("Cannot delete config.ini\n");
        return;
    }
    if (QFile::remove(path)) {
        m_console->appendText("Deleted: " + args[1] + "\n");
    } else {
        m_console->appendText("Failed to delete: " + args[1] + "\n");
    }
}

void MainWindow::cmdList(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: list <filename>\n");
        return;
    }
    QString path = resolvePath(args[1]);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_console->appendText("Cannot open: " + args[1] + "\n");
        return;
    }
    QString contents = QString::fromUtf8(file.readAll());
    m_console->appendText(contents);
    if (!contents.endsWith('\n'))
        m_console->appendText("\n");
}

void MainWindow::cmdRen(const QStringList &args)
{
    if (args.size() < 3) {
        m_console->appendText("Usage: ren <oldname> <newname>\n");
        return;
    }
    QString oldPath = resolvePath(args[1]);
    // For new name, resolve relative to sandbox
    QString sandbox = QString::fromStdString(simConfig().sandbox_path);
    QString newName = args[2];
    if (newName.startsWith('/'))
        newName = newName.mid(1);
    QString newPath = sandbox + "/" + newName;

    if (!QFileInfo::exists(oldPath)) {
        m_console->appendText("File not found: " + args[1] + "\n");
        return;
    }
    if (QFile::rename(oldPath, newPath)) {
        m_console->appendText("Renamed: " + args[1] + " -> " + args[2] + "\n");
    } else {
        m_console->appendText("Failed to rename.\n");
    }
}

void MainWindow::cmdParam(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: param <id> [value]\n");
        return;
    }
    bool ok;
    int id = args[1].toInt(&ok);
    if (!ok || id < 0 || id > 15) {
        m_console->appendText("Parameter id must be 0-15.\n");
        return;
    }
    if (args.size() >= 3) {
        int val = args[2].toInt(&ok);
        if (!ok) {
            m_console->appendText("Value must be an integer.\n");
            return;
        }
        m_wasmWorker->setParam(id, val);
        m_console->appendText(QString("param[%1] = %2\n").arg(id).arg(val));
    } else {
        int val = m_wasmWorker->getParam(id);
        m_console->appendText(QString("param[%1] = %2\n").arg(id).arg(val));
    }
}

void MainWindow::cmdLed()
{
    auto &cfg = simConfig();
    m_console->appendText(QString(
        "LED Configuration:\n"
        "  Channel 1: %1 LEDs\n"
        "  Channel 2: %2 LEDs\n"
        "  Channel 3: %3 LEDs\n"
        "  Channel 4: %4 LEDs\n"
    ).arg(cfg.led_count1).arg(cfg.led_count2)
     .arg(cfg.led_count3).arg(cfg.led_count4));
}

void MainWindow::cmdSensors()
{
    auto m = sensorState().read();
    QString out = "Sensors:\n";
    out += QString("  GPS:     lat=%1  lon=%2  alt=%3  valid=%4\n")
        .arg(m.lat, 0, 'f', 6).arg(m.lon, 0, 'f', 6)
        .arg(m.alt, 0, 'f', 1).arg(m.gps_valid);
    out += QString("  IMU:     roll=%1  pitch=%2  yaw=%3  valid=%4\n")
        .arg(m.roll, 0, 'f', 1).arg(m.pitch, 0, 'f', 1)
        .arg(m.yaw, 0, 'f', 1).arg(m.imu_valid);
    out += QString("  Env:     temp=%1 C  humidity=%2%  brightness=%3\n")
        .arg(m.temp, 0, 'f', 1).arg(m.humidity, 0, 'f', 1)
        .arg(m.brightness, 0, 'f', 0);
    out += QString("  Power:   bat=%1V  solar=%2V  charge=%3%  runtime=%4min\n")
        .arg(m.bat_voltage, 0, 'f', 2).arg(m.solar_voltage, 0, 'f', 2)
        .arg((int)m.battery_percentage).arg(m.battery_runtime, 0, 'f', 0);
    out += QString("  Sun:     rise=%1  set=%2  daylight=%3\n")
        .arg(m.sunrise).arg(m.sunset).arg(m.is_daylight);
    m_console->appendText(out);
}

void MainWindow::cmdTime()
{
    QDateTime now = QDateTime::currentDateTime();
    m_console->appendText(QString("Time: %1\n").arg(now.toString("yyyy-MM-dd hh:mm:ss ddd")));
    qint64 epochMs = now.toMSecsSinceEpoch();
    m_console->appendText(QString("Epoch: %1 ms\n").arg(epochMs));
    m_console->appendText("Source: system clock\n");
}

void MainWindow::cmdUptime()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - simConfig().start_time).count();

    int days = elapsed / 86400;
    int hours = (elapsed % 86400) / 3600;
    int mins = (elapsed % 3600) / 60;
    int secs = elapsed % 60;

    QString out = "Uptime: ";
    if (days > 0) out += QString("%1d ").arg(days);
    out += QString("%1h %2m %3s\n").arg(hours).arg(mins).arg(secs);
    m_console->appendText(out);
}

void MainWindow::cmdVersion()
{
    m_console->appendText(
        "ConeZ Desktop Simulator v1.0\n"
        "Platform: Qt " QT_VERSION_STR "\n"
        "Build: " __DATE__ " " __TIME__ "\n"
    );
}

void MainWindow::cmdWasm(const QStringList &args)
{
    QString sub = (args.size() >= 2) ? args[1].toLower() : "status";

    if (sub == "status") {
        if (m_wasmWorker->isRunning()) {
            m_console->appendText("WASM: running");
            if (!m_lastFile.isEmpty())
                m_console->appendText(" (" + m_lastFile + ")");
            m_console->appendText("\n");
        } else {
            m_console->appendText("WASM: stopped\n");
        }
    } else if (sub == "info" && args.size() >= 3) {
        QString path = resolvePath(args[2]);
        QFileInfo fi(path);
        if (!fi.exists()) {
            m_console->appendText("File not found: " + args[2] + "\n");
            return;
        }
        m_console->appendText(QString("File: %1\nSize: %2 bytes\n")
            .arg(fi.fileName()).arg(fi.size()));
    } else {
        m_console->appendText("Usage: wasm [status], wasm info <file>\n");
    }
}
