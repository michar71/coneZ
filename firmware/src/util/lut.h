#ifndef lut_h
#define lut_h

#include <Arduino.h>

extern int* pLUT;
extern int lutSize;
extern int currentLUTIndex;

void lutMutexInit(void);
int  loadLut(uint8_t index);
int  saveLut(uint8_t index);
int  checkLut(uint8_t index);
void lutReset(void);

#endif
