#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "main.h"
#include "util.h"
#include "printManager.h"
#include "conez_usb.h"


// Blink an error code forever
void blinkloop( int flashes )
{
  int i;

  while( 1 )
  {
    for( i = 0; i < flashes; ++i )
    {
      gpio_set_level( (gpio_num_t)LED_PIN, 1 );
      vTaskDelay(pdMS_TO_TICKS(250));
      gpio_set_level( (gpio_num_t)LED_PIN, 0 );
      vTaskDelay(pdMS_TO_TICKS(250));
    }

    gpio_set_level( (gpio_num_t)LED_PIN, 0 );

    vTaskDelay(pdMS_TO_TICKS(1000));

    usb_printf(".");
  }
}


void hexdump( uint8_t *buf, int len )
{
  int i;

  if( len < 1 )
    return;

  for( i = 0; i < len; ++i )
  {
    printfnl(SOURCE_NONE,"%02x", buf[ i ]);
    printfnl(SOURCE_NONE,"\n");
  }

  printfnl(SOURCE_NONE,"\n");
}


// ---- GPIO pin guards (shared by the `gpio` CLI command and the WASM pin_* imports) ----

// Returns true if the pin is a valid ESP32-S3 GPIO (0-21, 33-48)
bool gpio_valid_pin(int pin)
{
    return (pin >= 0 && pin <= 21) || (pin >= 33 && pin <= 48);
}

// Returns true if the pin is reserved for critical hardware and should not be reconfigured
bool gpio_is_reserved(int pin)
{
    // USB
    if (pin == 19 || pin == 20) return true;
#ifdef BOARD_CONEZ_V0_1
    // PSRAM SPI
    if (pin >= 4 && pin <= 7) return true;
    // LoRa SPI + control
    if (pin >= 8 && pin <= 14) return true;
    // GPS UART
    if (pin == 43 || pin == 44) return true;
    // GPS PPS
    if (pin == 42) return true;
    // I2C
    if (pin == 17 || pin == 18) return true;
#elif defined(BOARD_HELTEC_LORA32_V3)
    // LoRa SPI + control
    if (pin >= 8 && pin <= 14) return true;
    // I2C / OLED
    if (pin == 17 || pin == 18 || pin == 21) return true;
#endif
    return false;
}
