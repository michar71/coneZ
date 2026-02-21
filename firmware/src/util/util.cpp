#include <Arduino.h>
#include "driver/gpio.h"
#include "main.h"
#include "util.h"
#include "printManager.h"


// Blink an error code forever
void blinkloop( int flashes )
{
  int i;

  while( 1 )
  {
    for( i = 0; i < flashes; ++i )
    {
      gpio_set_level( (gpio_num_t)LED_PIN, 1 );
      delay( 250 );
      gpio_set_level( (gpio_num_t)LED_PIN, 0 );
      delay( 250 );
    }

    gpio_set_level( (gpio_num_t)LED_PIN, 0 );

    delay( 1000 );

    Serial.print( "." );
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
