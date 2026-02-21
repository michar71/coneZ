#include <stdint.h>
#include <string.h>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "main.h"
#include "led.h"
#include "config.h"

// LED buffers -- dynamically allocated in led_setup()
CRGB *leds1 = nullptr;
CRGB *leds2 = nullptr;
CRGB *leds3 = nullptr;
CRGB *leds4 = nullptr;

// Dirty flag -- set by led_show(), cleared by the render task.
// volatile is sufficient: single writer (render task clears), multiple setters (any task sets).
static volatile bool led_dirty = false;

// Mutex protects buffer pointer/count swaps in led_resize_channel().
static SemaphoreHandle_t led_mutex = nullptr;


void led_setup( void )
{
#ifdef BOARD_HAS_RGB_LEDS
    led_mutex = xSemaphoreCreateMutex();
    leds1 = new CRGB[config.led_count1]();
    leds2 = new CRGB[config.led_count2]();
    leds3 = new CRGB[config.led_count3]();
    leds4 = new CRGB[config.led_count4]();

    // NOTE: Hardware output (RMT driver) not yet implemented.
    // Color buffers are functional; led_show_now() is a no-op.
#endif
}


void led_show( void )
{
    led_dirty = true;
}


void led_show_now( void )
{
    // No-op until RMT driver is implemented.
    // Color buffers are still valid — commands like 'led' can inspect them.
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


int led_resize_channel( int ch, int count )
{
#ifdef BOARD_HAS_RGB_LEDS
    if (ch < 1 || ch > 4 || count < 0)
        return -1;

    CRGB **buf_ptr;
    int *count_ptr;

    switch (ch) {
        case 1: buf_ptr = &leds1; count_ptr = &config.led_count1; break;
        case 2: buf_ptr = &leds2; count_ptr = &config.led_count2; break;
        case 3: buf_ptr = &leds3; count_ptr = &config.led_count3; break;
        case 4: buf_ptr = &leds4; count_ptr = &config.led_count4; break;
        default: return -1;
    }

    int old_count = *count_ptr;
    if (count == old_count)
        return 0;

    CRGB *old_buf = *buf_ptr;
    CRGB *new_buf = nullptr;

    if (count > 0) {
        new_buf = new(std::nothrow) CRGB[count]();
        if (!new_buf)
            return -1;

        // Copy existing LED data
        int copy_count = (count < old_count) ? count : old_count;
        if (old_buf && copy_count > 0)
            memcpy(new_buf, old_buf, copy_count * sizeof(CRGB));
    }

    // Swap pointer and count under mutex.
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    *buf_ptr = new_buf;
    *count_ptr = count;
    xSemaphoreGive(led_mutex);

    delete[] old_buf;
    led_show();
    return 0;
#else
    return -1;
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
        if (led_dirty || uptime_ms() - last_show >= 1000)
        {
            led_dirty = false;
            // Hardware output placeholder — buffers are up-to-date,
            // RMT driver will push data here once implemented.
            last_show = uptime_ms();
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
