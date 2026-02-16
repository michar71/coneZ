#ifndef _conez_led_h
#define _conez_led_h

#include "board.h"
#include "FastLED.h"

// Global LED buffers -- dynamically allocated in led_setup() from config
extern CRGB *leds1;
extern CRGB *leds2;
extern CRGB *leds3;
extern CRGB *leds4;

// Initialize FastLED hardware. Call from setup() before led_start_task().
void led_setup( void );

// Start the LED render task (~30 FPS). Call from setup() after led_setup().
void led_start_task( void );

// Mark LED buffers dirty. The render task will call FastLED.show() on the
// next frame. Safe to call from any task/core.
void led_show( void );

// Call FastLED.show() immediately. ONLY safe during setup() before
// led_start_task() has been called.
void led_show_now( void );

// Set `cnt` LEDs on channel `ch` (1-4) to `col`. Does NOT trigger show.
void led_set_channel( int ch, int cnt, CRGB col );

// Resize channel `ch` (1-4) to `count` LEDs. Copies existing data,
// new LEDs are black. Returns 0 on success, -1 on error.
int led_resize_channel( int ch, int count );

#endif
