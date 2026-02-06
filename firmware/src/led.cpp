#include <Arduino.h>
#include "led.h"

// LED buffers (always defined -- BASIC extensions reference leds1)
CRGB leds1[NUM_LEDS1];
CRGB leds2[NUM_LEDS2];
CRGB leds3[NUM_LEDS3];
CRGB leds4[NUM_LEDS4];

// Dirty flag -- set by led_show(), cleared by the render task after FastLED.show().
// volatile is sufficient: single writer (render task clears), multiple setters (any task sets).
static volatile bool led_dirty = false;


void led_setup( void )
{
#ifdef BOARD_HAS_RGB_LEDS
    FastLED.addLeds<WS2811, RGB1_PIN, BRG>(leds1, NUM_LEDS1);
    FastLED.addLeds<WS2811, RGB2_PIN, BRG>(leds2, NUM_LEDS2);
    FastLED.addLeds<WS2811, RGB3_PIN, BRG>(leds3, NUM_LEDS3);
    FastLED.addLeds<WS2811, RGB4_PIN, BRG>(leds4, NUM_LEDS4);
#endif
}


void led_show( void )
{
    led_dirty = true;
}


void led_show_now( void )
{
#ifdef BOARD_HAS_RGB_LEDS
    FastLED.show();
#endif
}


void led_set_channel( int ch, int cnt, CRGB col )
{
#ifdef BOARD_HAS_RGB_LEDS
    if (cnt > NUM_LEDS1)
        cnt = NUM_LEDS1;

    switch (ch)
    {
        default:
        case 1:
            for (int ii = 0; ii < cnt; ii++)
                leds1[ii] = col;
            break;
        case 2:
            for (int ii = 0; ii < NUM_LEDS2; ii++)
                leds2[ii] = col;
            break;
        case 3:
            for (int ii = 0; ii < NUM_LEDS3; ii++)
                leds3[ii] = col;
            break;
        case 4:
            for (int ii = 0; ii < NUM_LEDS4; ii++)
                leds4[ii] = col;
            break;
    }
#endif
}


#ifdef BOARD_HAS_RGB_LEDS
static void led_task_fun( void *param )
{
    (void)param;
    for (;;)
    {
        vTaskDelay(33 / portTICK_PERIOD_MS);   // ~30 FPS
        if (led_dirty)
        {
            led_dirty = false;
            FastLED.show();
        }
    }
}
#endif


void led_start_task( void )
{
#ifdef BOARD_HAS_RGB_LEDS
    xTaskCreatePinnedToCore(
        led_task_fun,
        "led_render",
        4096,           // stack size
        NULL,
        2,              // priority
        NULL,
        1               // Core 1
    );
#endif
}
