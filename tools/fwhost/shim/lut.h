// Host shim for firmware/src/util/lut.h: no LUT files on the host, so every
// LUT is "not found" and reads give 0.
#ifndef FWHOST_LUT_H
#define FWHOST_LUT_H
#include <stdint.h>
static inline int  loadLut(uint8_t) { return 0; }
static inline int  saveLut(uint8_t) { return 0; }
static inline int  checkLut(uint8_t) { return -1; }
static inline bool lut_read(int, int *) { return false; }
static inline bool lut_write(int, int) { return false; }
static inline int  lut_get_size(void) { return 0; }
#endif
