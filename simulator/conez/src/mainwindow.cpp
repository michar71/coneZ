#include "mainwindow.h"
#include "led_strip_widget.h"
#include "console_widget.h"
#include "sensor_panel.h"
#include "wasm_worker.h"
#include "compiler_worker.h"
#include "sim_config.h"
#include "led_state.h"
#include "sensor_state.h"
#include "cue_engine.h"

#include <QToolBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QAction>
#include <QDateTime>
#include <QCryptographicHash>
#include <chrono>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    char title[64];
    snprintf(title, sizeof(title), "ConeZ Simulator v%d.%d.%04d", VERSION_MAJOR, VERSION_MINOR, BUILD_NUMBER);
    setWindowTitle(title);
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

    // Wire cue engine output to console
    cueEngine().setOutputCallback([this](const QString &msg) {
        m_console->appendText(msg);
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
    } else if (verb == "dir" || verb == "ls") {
        cmdDir(parts);
    } else if (verb == "del") {
        cmdDel(parts);
    } else if (verb == "cat" || verb == "list") {
        cmdList(parts);
    } else if (verb == "ren" || verb == "mv") {
        cmdRen(parts);
    } else if (verb == "cp") {
        cmdCp(parts);
    } else if (verb == "mkdir") {
        cmdMkdir(parts);
    } else if (verb == "rmdir") {
        cmdRmdir(parts);
    } else if (verb == "grep") {
        cmdGrep(parts);
    } else if (verb == "hexdump") {
        cmdHexdump(parts);
    } else if (verb == "md5" || verb == "md5sum") {
        cmdMd5(parts);
    } else if (verb == "sha256" || verb == "sha256sum") {
        cmdSha256(parts);
    } else if (verb == "df") {
        cmdDf();
    } else if (verb == "clear" || verb == "cls") {
        cmdClear();
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
    } else if (verb == "ver" || verb == "version") {
        cmdVersion();
    } else if (verb == "wasm") {
        cmdWasm(parts);
    } else if (verb == "cue") {
        cmdCue(parts);
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
        "Available commands:\n"
        "  cat {filename}                      Show file contents\n"
        "  clear                               Clear console\n"
        "  cp {source} {dest}                  Copy file\n"
        "  cue [load|start|stop|status]        Cue timeline engine\n"
        "  del {filename}                      Delete file\n"
        "  df                                  Show filesystem usage\n"
        "  dir/ls [path]                       List files\n"
        "  grep {pattern} [file]               Search file contents\n"
        "  help                                Show this help\n"
        "  hexdump {file} [count]              Hex dump file (default 256 bytes)\n"
        "  led                                 Show LED configuration\n"
        "  md5 {filename}                      Compute MD5 hash\n"
        "  mkdir {dirname}                     Create directory\n"
        "  open                                Open file dialog\n"
        "  param {id} [value]                  Get/set script parameter (0-15)\n"
        "  ren {oldname} {newname}             Rename file\n"
        "  rmdir {dirname}                     Remove empty directory\n"
        "  run {filename}                      Run program (.bas, .c, .wasm)\n"
        "  sensors                             Show sensor readings\n"
        "  sha256 {filename}                   Compute SHA-256 hash\n"
        "  stop                                Stop running program\n"
        "  time                                Show current date/time\n"
        "  uptime                              Show time since start\n"
        "  version                             Show simulator version\n"
        "  wasm [status|info {file}]           WASM runtime status/info\n"
        "\n"
    );
}

void MainWindow::cmdDir(const QStringList &args)
{
    QString sandbox = QString::fromStdString(simConfig().sandbox_path);
    QString dirPath = sandbox;
    if (args.size() >= 2) {
        QString sub = args[1];
        if (sub.startsWith('/'))
            sub = sub.mid(1);
        dirPath = sandbox + "/" + sub;
    }

    QDir dir(dirPath);
    if (!dir.exists()) {
        m_console->appendText("Not a directory: " + args.value(1, "/") + "\n");
        return;
    }

    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                               QDir::Name | QDir::DirsFirst);
    QString out;
    qint64 totalSize = 0;
    int fileCount = 0;
    int dirCount = 0;
    for (const auto &fi : entries) {
        if (fi.isDir()) {
            out += QString("  %-30s <DIR>\n").arg(fi.fileName() + "/");
            dirCount++;
        } else {
            out += QString("  %-30s %6lld\n").arg(fi.fileName()).arg(fi.size());
            totalSize += fi.size();
            fileCount++;
        }
    }
    out += QString("%1 file%2, %3 dir%4, %5 bytes\n")
        .arg(fileCount).arg(fileCount == 1 ? "" : "s")
        .arg(dirCount).arg(dirCount == 1 ? "" : "s")
        .arg(totalSize);
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
        m_console->appendText("Usage: cat <filename>\n");
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
        m_console->appendText("Usage: ren/mv <oldname> <newname>\n");
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

void MainWindow::cmdCp(const QStringList &args)
{
    if (args.size() < 3) {
        m_console->appendText("Usage: cp <source> <dest>\n");
        return;
    }
    QString srcPath = resolvePath(args[1]);
    if (!QFileInfo::exists(srcPath)) {
        m_console->appendText("File not found: " + args[1] + "\n");
        return;
    }
    QString sandbox = QString::fromStdString(simConfig().sandbox_path);
    QString dstName = args[2];
    if (dstName.startsWith('/'))
        dstName = dstName.mid(1);
    QString dstPath = sandbox + "/" + dstName;

    if (QFile::copy(srcPath, dstPath)) {
        m_console->appendText("Copied: " + args[1] + " -> " + args[2] + "\n");
    } else {
        m_console->appendText("Failed to copy.\n");
    }
}

void MainWindow::cmdMkdir(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: mkdir <dirname>\n");
        return;
    }
    QString sandbox = QString::fromStdString(simConfig().sandbox_path);
    QString name = args[1];
    if (name.startsWith('/'))
        name = name.mid(1);
    QString path = sandbox + "/" + name;

    if (QDir().mkpath(path)) {
        m_console->appendText("Created: " + args[1] + "\n");
    } else {
        m_console->appendText("Failed to create directory.\n");
    }
}

void MainWindow::cmdRmdir(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: rmdir <dirname>\n");
        return;
    }
    QString sandbox = QString::fromStdString(simConfig().sandbox_path);
    QString name = args[1];
    if (name.startsWith('/'))
        name = name.mid(1);
    QString path = sandbox + "/" + name;

    QDir dir(path);
    if (!dir.exists()) {
        m_console->appendText("Directory not found: " + args[1] + "\n");
        return;
    }
    // rmdir only removes empty directories
    if (dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).count() > 0) {
        m_console->appendText("Directory not empty: " + args[1] + "\n");
        return;
    }
    if (QDir().rmdir(path)) {
        m_console->appendText("Removed: " + args[1] + "\n");
    } else {
        m_console->appendText("Failed to remove directory.\n");
    }
}

static void grepFile(const QString &pattern, const QString &filePath,
                     bool showFilename, ConsoleWidget *console)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    int lineno = 0;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        if (line.endsWith('\n'))
            line.chop(1);
        if (line.endsWith('\r'))
            line.chop(1);
        lineno++;

        if (line.contains(pattern, Qt::CaseInsensitive)) {
            if (showFilename)
                console->appendText(QString("%1:%2: %3\n").arg(filePath).arg(lineno).arg(line));
            else
                console->appendText(QString("%1: %2\n").arg(lineno, 3).arg(line));
        }
    }
}

static void grepDir(const QString &pattern, const QString &dirPath,
                    ConsoleWidget *console)
{
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        grepFile(pattern, it.filePath(), true, console);
    }
}

void MainWindow::cmdGrep(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: grep <pattern> [file]  (no file = search all)\n");
        return;
    }
    QString pattern = args[1];

    if (args.size() >= 3) {
        QString path = resolvePath(args[2]);
        if (!QFileInfo::exists(path)) {
            m_console->appendText("File not found: " + args[2] + "\n");
            return;
        }
        grepFile(pattern, path, false, m_console);
    } else {
        QString sandbox = QString::fromStdString(simConfig().sandbox_path);
        grepDir(pattern, sandbox, m_console);
    }
}

void MainWindow::cmdHexdump(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: hexdump <filename> [count]\n");
        return;
    }
    QString path = resolvePath(args[1]);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_console->appendText("Cannot open: " + args[1] + "\n");
        return;
    }

    int limit = (args.size() >= 3) ? args[2].toInt() : 256;
    if (limit <= 0) limit = 256;

    qint64 fsize = file.size();
    m_console->appendText(QString("%1  (%2 bytes)\n").arg(args[1]).arg(fsize));

    char buf[16];
    int offset = 0;
    while (!file.atEnd() && offset < limit) {
        qint64 n = file.read(buf, 16);
        if (n <= 0) break;
        if (offset + n > limit) n = limit - offset;

        QString line = QString("%1  ").arg(offset, 4, 16, QChar('0'));

        // Hex bytes
        for (int i = 0; i < 16; i++) {
            if (i == 8) line += ' ';
            if (i < n)
                line += QString("%1 ").arg((unsigned char)buf[i], 2, 16, QChar('0'));
            else
                line += "   ";
        }

        // ASCII
        line += " |";
        for (int i = 0; i < n; i++) {
            unsigned char c = buf[i];
            line += (c >= 32 && c < 127) ? QChar(c) : QChar('.');
        }
        line += "|\n";
        m_console->appendText(line);

        offset += n;
    }
    if (fsize > limit)
        m_console->appendText(QString("... (%1 more bytes)\n").arg(fsize - limit));
}

void MainWindow::cmdMd5(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: md5 <filename>\n");
        return;
    }
    QString path = resolvePath(args[1]);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_console->appendText("Cannot open " + args[1] + "\n");
        return;
    }
    QByteArray hash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
    m_console->appendText(hash.toHex() + "  " + args[1] + "\n");
}

void MainWindow::cmdSha256(const QStringList &args)
{
    if (args.size() < 2) {
        m_console->appendText("Usage: sha256 <filename>\n");
        return;
    }
    QString path = resolvePath(args[1]);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_console->appendText("Cannot open " + args[1] + "\n");
        return;
    }
    QByteArray hash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
    m_console->appendText(hash.toHex() + "  " + args[1] + "\n");
}

void MainWindow::cmdDf()
{
    QString sandbox = QString::fromStdString(simConfig().sandbox_path);
    qint64 totalSize = 0;
    int fileCount = 0;

    QDirIterator it(sandbox, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        totalSize += it.fileInfo().size();
        fileCount++;
    }

    m_console->appendText(
        QString("Filesystem: sandbox\n"
                "  Path:  %1\n"
                "  Files: %2\n"
                "  Used:  %3 bytes (%4 KB)\n")
        .arg(sandbox)
        .arg(fileCount)
        .arg(totalSize)
        .arg(totalSize / 1024));
}

void MainWindow::cmdClear()
{
    m_console->clear();
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
    char vbuf[128];
    snprintf(vbuf, sizeof(vbuf),
        "ConeZ Desktop Simulator v%d.%d.%04d\n",
        VERSION_MAJOR, VERSION_MINOR, BUILD_NUMBER);
    m_console->appendText(QString(vbuf));
    m_console->appendText(
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

void MainWindow::cmdCue(const QStringList &args)
{
    QString sub = (args.size() >= 2) ? args[1].toLower() : "status";

    if (sub == "status") {
        auto &eng = cueEngine();
        m_console->appendText("Cue Engine:\n");
        m_console->appendText(QString("  Loaded:  %1\n").arg(eng.cueCount() > 0 ? "yes" : "no"));
        m_console->appendText(QString("  Cues:    %1\n").arg(eng.cueCount()));
        m_console->appendText(QString("  Playing: %1\n").arg(eng.isPlaying() ? "yes" : "no"));
        if (eng.isPlaying()) {
            m_console->appendText(QString("  Elapsed: %1 ms\n").arg(eng.elapsedMs()));
            m_console->appendText(QString("  Cursor:  %1 / %2\n").arg(eng.cueCursor()).arg(eng.cueCount()));
        }
    } else if (sub == "load") {
        if (args.size() < 3) {
            m_console->appendText("Usage: cue load <path>\n");
            return;
        }
        cueEngine().load(resolvePath(args[2]));
    } else if (sub == "start") {
        qint64 offset = 0;
        if (args.size() >= 3)
            offset = args[2].toLongLong();
        cueEngine().start(offset);
    } else if (sub == "stop") {
        cueEngine().stop();
    } else {
        m_console->appendText("Usage: cue [load <path> | start [ms] | stop | status]\n");
    }
}
