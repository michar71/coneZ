#include <Arduino.h>
#include "led.h"
#include "config.h"

// LED buffers -- dynamically allocated in led_setup()
CRGB *leds1 = nullptr;
CRGB *leds2 = nullptr;
CRGB *leds3 = nullptr;
CRGB *leds4 = nullptr;

// Dirty flag -- set by led_show(), cleared by the render task after FastLED.show().
// volatile is sufficient: single writer (render task clears), multiple setters (any task sets).
static volatile bool led_dirty = false;


void led_setup( void )
{
#ifdef BOARD_HAS_RGB_LEDS
    leds1 = new CRGB[config.led_count1]();
    leds2 = new CRGB[config.led_count2]();
    leds3 = new CRGB[config.led_count3]();
    leds4 = new CRGB[config.led_count4]();

    FastLED.addLeds<WS2811, RGB1_PIN, BRG>(leds1, config.led_count1);
    FastLED.addLeds<WS2811, RGB2_PIN, BRG>(leds2, config.led_count2);
    FastLED.addLeds<WS2811, RGB3_PIN, BRG>(leds3, config.led_count3);
    FastLED.addLeds<WS2811, RGB4_PIN, BRG>(leds4, config.led_count4);
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
    CRGB *buf = nullptr;
    int max_leds = 0;

    switch (ch)
    {
        default:
        case 1: buf = leds1; max_leds = config.led_count1; break;
        case 2: buf = leds2; max_leds = config.led_count2; break;
        case 3: buf = leds3; max_leds = config.led_count3; break;
        case 4: buf = leds4; max_leds = config.led_count4; break;
    }

    if (!buf) return;
    if (cnt > max_leds) cnt = max_leds;

    for (int ii = 0; ii < cnt; ii++)
        buf[ii] = col;
#endif
}


#ifdef BOARD_HAS_RGB_LEDS
static void led_task_fun( void *param )
{
    (void)param;
    unsigned long last_show = 0;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(33));   // ~30 FPS
        if (led_dirty || millis() - last_show >= 1000)
        {
            led_dirty = false;
            FastLED.show();
            last_show = millis();
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
