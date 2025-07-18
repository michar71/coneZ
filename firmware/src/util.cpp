#include <Arduino.h>
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
      digitalWrite( LED_PIN, HIGH );
      delay( 250 );
      digitalWrite( LED_PIN, LOW );
      delay( 250 );
    }

    digitalWrite( LED_PIN, LOW );

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
