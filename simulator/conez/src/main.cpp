#include <QApplication>
#include "mainwindow.h"
#include "sim_config.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ConeZ Simulator");
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%d.%02d.%04d", VERSION_MAJOR, VERSION_MINOR, BUILD_NUMBER);
    app.setApplicationVersion(vbuf);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"leds", "LED count per channel", "count", "50"});
    parser.addOption({"sandbox", "Sandbox directory for file I/O", "path"});
    parser.addOption({"bas2wasm", "Path to bas2wasm compiler", "path", "bas2wasm"});
    parser.addOption({"c2wasm", "Path to c2wasm compiler", "path", "c2wasm"});
    parser.addOption({"clang", "Path to clang compiler", "path", "clang"});
    parser.addOption({"api-dir", "Path to directory containing conez_api.h", "path"});
    parser.addOption({"cone-id", "Cone ID for cue targeting", "id", "0"});
    parser.addOption({"cone-group", "Cone group for cue targeting", "group", "0"});
    parser.addPositionalArgument("file", "Script to run on startup (.bas, .c, .wasm)");
    parser.process(app);

    auto &cfg = simConfig();
    int leds = parser.value("leds").toInt();
    if (leds > 0) {
        cfg.led_count1 = cfg.led_count2 = cfg.led_count3 = cfg.led_count4 = leds;
    }
    if (parser.isSet("c2wasm"))   cfg.c2wasm_path    = parser.value("c2wasm").toStdString();
    if (parser.isSet("clang"))    cfg.clang_path     = parser.value("clang").toStdString();
    if (parser.isSet("api-dir"))  cfg.api_header_dir = parser.value("api-dir").toStdString();
    if (parser.isSet("cone-id"))    cfg.cone_id    = parser.value("cone-id").toInt();
    if (parser.isSet("cone-group")) cfg.cone_group = parser.value("cone-group").toInt();

    // Binary location — used for relative path auto-detection
    // Build dir is simulator/conez/build/, project root is ../../../
    QString binDir = QCoreApplication::applicationDirPath();
    QString projectRoot = QDir(binDir + "/../../..").canonicalPath();

    // Sandbox path: explicit --sandbox, or auto-detect data/ dir relative to binary
    if (parser.isSet("sandbox")) {
        cfg.sandbox_path = parser.value("sandbox").toStdString();
    } else {
        QString dataDir = binDir + "/../data";
        if (QDir(dataDir).exists()) {
            cfg.sandbox_path = QDir(dataDir).canonicalPath().toStdString();
        }
    }

    // bas2wasm: explicit --bas2wasm, or auto-detect from project tree
    if (parser.isSet("bas2wasm")) {
        cfg.bas2wasm_path = parser.value("bas2wasm").toStdString();
    } else {
        QString candidate = projectRoot + "/tools/bas2wasm/bas2wasm";
        if (QFileInfo(candidate).isExecutable()) {
            cfg.bas2wasm_path = candidate.toStdString();
        }
    }

    // Auto-detect api-dir: tools/wasm/ in the project tree
    if (cfg.api_header_dir.empty()) {
        QString candidate = projectRoot + "/tools/wasm";
        if (QFileInfo::exists(candidate + "/conez_api.h")) {
            cfg.api_header_dir = candidate.toStdString();
        }
    }

    MainWindow w;
    w.show();

    // Run startup file if provided
    QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        QMetaObject::invokeMethod(&w, "runFileFromArg",
            Qt::QueuedConnection, Q_ARG(QString, positional[0]));
    }

    return app.exec();
}
