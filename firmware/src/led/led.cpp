#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "main.h"
#include "led.h"
#include "config.h"

#ifdef BOARD_HAS_RGB_LEDS
#include "driver/rmt_tx.h"
#endif

// LED buffers -- dynamically allocated in led_setup()
CRGB *leds1 = nullptr;
CRGB *leds2 = nullptr;
CRGB *leds3 = nullptr;
CRGB *leds4 = nullptr;

// Dirty flag -- set by led_show(), cleared by the render task.
// volatile is sufficient: single writer (render task clears), multiple setters (any task sets).
static volatile bool led_dirty = false;

#ifdef BOARD_HAS_RGB_LEDS

// Mutex protects buffer pointer/count swaps in led_resize_channel().
static SemaphoreHandle_t led_mutex = nullptr;

// ---- WS2812B RMT Encoder ----
//
// Custom encoder: bytes_encoder converts pixel bytes to RMT symbols,
// copy_encoder appends the reset pulse. Standard ESP-IDF pattern.

// WS2812B timing at 10 MHz resolution (0.1us per tick)
// T0H=0.4us(4 ticks), T0L=0.85us(9 ticks)
// T1H=0.8us(8 ticks), T1L=0.45us(5 ticks)
// Total bit time: 1.3us (within WS2812B spec of +/-150ns)
// Reset: >=280us LOW

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} ws2812_encoder_t;

static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *primary_data, size_t data_size,
                            rmt_encode_state_t *ret_state)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (enc->state) {
    case 0:  // encode pixel bytes
        encoded_symbols += enc->bytes_encoder->encode(enc->bytes_encoder, channel,
                                                      primary_data, data_size,
                                                      &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            enc->state = 1;  // move to reset pulse
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            *ret_state = RMT_ENCODING_MEM_FULL;
            return encoded_symbols;
        }
        // fall through to state 1
        __attribute__((fallthrough));

    case 1:  // append reset pulse
        encoded_symbols += enc->copy_encoder->encode(enc->copy_encoder, channel,
                                                     &enc->reset_code,
                                                     sizeof(enc->reset_code),
                                                     &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            enc->state = 0;  // back to start for next transmit
            *ret_state = RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            *ret_state = RMT_ENCODING_MEM_FULL;
        }
        return encoded_symbols;
    }

    return encoded_symbols;
}

static esp_err_t ws2812_encoder_reset(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    enc->bytes_encoder->reset(enc->bytes_encoder);
    enc->copy_encoder->reset(enc->copy_encoder);
    enc->state = 0;
    return ESP_OK;
}

static esp_err_t ws2812_encoder_del(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    rmt_del_encoder(enc->bytes_encoder);
    rmt_del_encoder(enc->copy_encoder);
    free(enc);
    return ESP_OK;
}

static esp_err_t ws2812_encoder_new(rmt_encoder_handle_t *ret_encoder)
{
    ws2812_encoder_t *enc = (ws2812_encoder_t *)calloc(1, sizeof(ws2812_encoder_t));
    if (!enc) return ESP_ERR_NO_MEM;

    enc->base.encode = ws2812_encode;
    enc->base.reset  = ws2812_encoder_reset;
    enc->base.del    = ws2812_encoder_del;

    // Bytes encoder: maps each bit to an RMT symbol pair
    rmt_bytes_encoder_config_t bytes_cfg = {};
    bytes_cfg.bit0.duration0 = 4;   // T0H = 0.4us
    bytes_cfg.bit0.level0    = 1;
    bytes_cfg.bit0.duration1 = 9;   // T0L = 0.85us  (round up)
    bytes_cfg.bit0.level1    = 0;
    bytes_cfg.bit1.duration0 = 8;   // T1H = 0.8us
    bytes_cfg.bit1.level0    = 1;
    bytes_cfg.bit1.duration1 = 5;   // T1L = 0.45us  (round up)
    bytes_cfg.bit1.level1    = 0;
    bytes_cfg.flags.msb_first = 1;

    esp_err_t err = rmt_new_bytes_encoder(&bytes_cfg, &enc->bytes_encoder);
    if (err != ESP_OK) { free(enc); return err; }

    // Copy encoder: for the reset pulse
    rmt_copy_encoder_config_t copy_cfg = {};
    err = rmt_new_copy_encoder(&copy_cfg, &enc->copy_encoder);
    if (err != ESP_OK) { rmt_del_encoder(enc->bytes_encoder); free(enc); return err; }

    // Reset pulse: >= 280us LOW. Use 300us (3000 ticks @ 10 MHz).
    enc->reset_code.duration0 = 3000;
    enc->reset_code.level0    = 0;
    enc->reset_code.duration1 = 0;
    enc->reset_code.level1    = 0;
    enc->state = 0;

    *ret_encoder = &enc->base;
    return ESP_OK;
}

// ---- RMT channels and encoder ----

static rmt_channel_handle_t rmt_chan[4] = {};
static rmt_encoder_handle_t rmt_enc = NULL;

// GRB conversion buffer (lazily allocated, reused across channels)
static uint8_t *grb_buf = NULL;
static int grb_buf_size = 0;

static void led_push_hw(void)
{
    CRGB *bufs[]  = { leds1, leds2, leds3, leds4 };
    int counts[]  = { config.led_count1, config.led_count2,
                      config.led_count3, config.led_count4 };

    // Find max LED count to size conversion buffer
    int max_count = 0;
    for (int i = 0; i < 4; i++) {
        if (counts[i] > max_count) max_count = counts[i];
    }

    // Ensure conversion buffer is large enough
    int needed = max_count * 3;
    if (needed > grb_buf_size) {
        free(grb_buf);
        grb_buf = (uint8_t *)malloc(needed);
        if (!grb_buf) { grb_buf_size = 0; return; }
        grb_buf_size = needed;
    }

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;

    for (int ch = 0; ch < 4; ch++) {
        if (!bufs[ch] || counts[ch] == 0 || !rmt_chan[ch]) continue;

        // Convert RGB -> GRB for WS2812B
        for (int i = 0; i < counts[ch]; i++) {
            grb_buf[i * 3 + 0] = bufs[ch][i].g;
            grb_buf[i * 3 + 1] = bufs[ch][i].r;
            grb_buf[i * 3 + 2] = bufs[ch][i].b;
        }

        rmt_transmit(rmt_chan[ch], rmt_enc, grb_buf, counts[ch] * 3, &tx_cfg);
        rmt_tx_wait_all_done(rmt_chan[ch], pdMS_TO_TICKS(100));
    }
}

static void rmt_init(void)
{
    const int pins[4] = { RGB1_PIN, RGB2_PIN, RGB3_PIN, RGB4_PIN };

    for (int i = 0; i < 4; i++) {
        rmt_tx_channel_config_t tx_cfg = {};
        tx_cfg.gpio_num = (gpio_num_t)pins[i];
        tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
        tx_cfg.resolution_hz = 10 * 1000 * 1000;  // 10 MHz = 0.1us per tick
        tx_cfg.mem_block_symbols = 64;
        tx_cfg.trans_queue_depth = 1;
        rmt_new_tx_channel(&tx_cfg, &rmt_chan[i]);
        rmt_enable(rmt_chan[i]);
    }

    ws2812_encoder_new(&rmt_enc);
}

#endif  // BOARD_HAS_RGB_LEDS


void led_setup( void )
{
#ifdef BOARD_HAS_RGB_LEDS
    led_mutex = xSemaphoreCreateMutex();
    leds1 = new CRGB[config.led_count1]();
    leds2 = new CRGB[config.led_count2]();
    leds3 = new CRGB[config.led_count3]();
    leds4 = new CRGB[config.led_count4]();

    rmt_init();
#endif
}


void led_show( void )
{
    led_dirty = true;
}


void led_show_now( void )
{
#ifdef BOARD_HAS_RGB_LEDS
    led_push_hw();
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
        if (led_dirty || uptime_ms() - last_show >= 500)
        {
            led_dirty = false;
            led_push_hw();
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
