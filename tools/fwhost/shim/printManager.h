// Host shim for firmware/src/console/printManager.h -- see ../README.txt
#ifndef FWHOST_PRINTMANAGER_H
#define FWHOST_PRINTMANAGER_H

typedef enum {
    SOURCE_NONE   = 0x00000000,
    SOURCE_BASIC  = 0x00000001,
    SOURCE_WASM   = 0x00000002,
    SOURCE_SYSTEM = 0x00000080,
} source_e;

// Everything a script prints goes to stdout.
void printfnl(source_e source, const char *format, ...) __attribute__((format(printf, 2, 3)));

#endif
