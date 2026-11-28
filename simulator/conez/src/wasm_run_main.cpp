// conez-wasm-run -- the simulator's WASM runtime and host imports without the
// GUI: run one .wasm module and print what it prints.
//
//   conez-wasm-run <sandbox-dir> </path/inside/sandbox.wasm>
//
// Same command line as tools/fwhost (the firmware's host code built for this
// machine), so the compilers' runtime tests can run on either: see
// `make test-host` in tools/bas2wasm. <sandbox-dir> is the script's filesystem,
// as --sandbox is for the GUI. Output is between the runtime's own
// "wasm: running ..." and "wasm: DONE" lines.

#include <QCoreApplication>
#include <cstdio>
#include <string>

#include "sim_config.h"
#include "sim_wasm_runtime.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);       // the cue engine is a QObject
    if (argc != 3) {
        fprintf(stderr, "usage: %s <sandbox-dir> </path/inside/sandbox.wasm>\n", argv[0]);
        return 2;
    }
    simConfig().sandbox_path = argv[1];

    SimWasmRuntime rt;
    rt.setOutputBatching(false);            // every line, in order, at once
    rt.setOutputCallback([](const std::string &s) { fwrite(s.data(), 1, s.size(), stdout); });
    rt.run(std::string(argv[1]) + argv[2]);
    fflush(stdout);
    return 0;
}
