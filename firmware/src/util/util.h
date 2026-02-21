#ifndef _conez_util_h
#define _conez_util_h

#include <stdint.h>

// Blink an error code on status LED forever.
void blinkloop( int flashes );

// Dump buffer as hex values to output stream.
void hexdump( uint8_t *buf, int len );

#endif