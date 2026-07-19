#ifndef lut_h
#define lut_h

#include <stdint.h>

void lutMutexInit(void);
int  loadLut(uint8_t index);
int  saveLut(uint8_t index);
int  checkLut(uint8_t index);
void lutReset(void);

// Locked accessors -- LUT state is private to lut.cpp (see note there). All
// external readers/writers MUST go through these so access is serialized.
bool lut_read(int index, int *out);          // true + *out set if in range & a LUT is loaded
bool lut_write(int index, int value);        // true if written (in range & loaded)
int  lut_get_size(void);                     // current LUT size (0 if none)
int  lut_current_index(void);                // currentLUTIndex (-1 if none)
int  lut_replace_from_array(const int *vals, int count);  // replace LUT; returns size or 0

#endif
