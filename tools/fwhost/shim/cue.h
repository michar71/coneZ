#ifndef FWHOST_CUE_H
#define FWHOST_CUE_H
#include <stdint.h>
static inline bool cue_is_playing(void) { return false; }
static inline uint32_t cue_get_elapsed_ms(void) { return 0; }
#endif
