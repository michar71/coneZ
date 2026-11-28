fwhost -- the firmware's WASM host code, built to run on this machine
======================================================================

What it is
----------
A command-line program that runs a .wasm module exactly the way the firmware
does, using the firmware's own source files compiled unchanged:

  firmware/lib/wasm3/src/*.c                the wasm3 fork (interpreter)
  firmware/src/wasm/wasm_wrapper.cpp        load / link / run / error reporting
  firmware/src/wasm/wasm_psram_glue.cpp     linear-memory access (wasm_mem_*)
  firmware/src/wasm/wasm_imports_string.cpp string pool, low heap, basic_str_*
  firmware/src/wasm/wasm_format.cpp         host_printf / snprintf / sscanf
  firmware/src/wasm/wasm_imports_io.cpp     print_i32 / f32 / i64 / f64 / str, LUT
  firmware/src/wasm/wasm_imports_file.cpp   file_* imports
  firmware/src/wasm/wasm_imports_math.cpp   math, lerp / larp
  firmware/src/wasm/wasm_imports_system.cpp params, should_stop, random, WAITFOR
  firmware/src/util/curve.cpp

Why
---
The compilers' runtime tests (tools/bas2wasm: make test-runtime) run under
Node with host imports re-implemented in JavaScript. That tests the compiler,
but a bug in the firmware's imports cannot fail it -- and the JavaScript copy
drifts from the real code. fwhost puts the real code under the same tests:

  cd tools/bas2wasm && make test-host

Two variants are built, matching the two boards:

  fwhost-dram    d_m3UsePsramMemory=0   Heltec: linear memory in ordinary RAM
  fwhost-psram   d_m3UsePsramMemory=1   ConeZ: first 4 KB in RAM, the rest
                                        through psram_read() / psram_write()

Both are built with AddressSanitizer and UBSan (make SAN= to turn them off),
so a host import that reads or writes outside its buffers fails the test.

Usage
-----
  make                                   builds fwhost-dram and fwhost-psram
  ./fwhost-dram <fs-root> /prog.wasm

<fs-root> is a directory that stands in for the device's LittleFS: the module
is loaded from <fs-root>/prog.wasm and a script's OPEN "/x.txt" is
<fs-root>/x.txt. Output is what the device prints on its console:

  wasm: running /prog.wasm on Core:0
  ...the program's output...
  wasm: DONE

A trap prints the firmware's message, e.g.
  wasm: setup() error: [trap] unreachable executed (BASIC line 9)

What is NOT real
----------------
Only the hardware. shim/ has small stand-ins for the headers the firmware
files include (printManager.h, main.h, FreeRTOS, psram.h, ...), and
fwhost.cpp implements them:

  - printfnl() writes to stdout.
  - The clock only advances when something sleeps; nothing actually waits.
  - esp_random() is a fixed sequence (the same one the Node harness uses).
  - PSRAM is host memory behind the same 0x10000000-based addresses; an access
    outside a live block aborts with a message.
  - LED, sensor, GPS, date/time, GPIO and compression imports are stubs that
    return fixed values (the same values as the Node harness), because the
    real ones need the hardware. LUT files do not exist.

So fwhost says nothing about those stubbed imports, about timing, or about
anything that depends on the ESP32 itself (stack sizes of FreeRTOS tasks,
32-bit pointers, the real PSRAM driver and its cache). It is a 64-bit build.

The simulator has the equivalent for ITS host code: conez-wasm-run (built
with the simulator), used by `make test-sim` in tools/bas2wasm.

Keeping it working
------------------
If a firmware file listed above starts including a new header, the build here
fails with "No such file": add a stand-in to shim/ (or, if the new dependency
is pure logic, add its source to FW_SRCS in the Makefile). Adding a new host
import to one of the stubbed groups needs a line in the matching table in
fwhost.cpp, or modules that use it will not link.
