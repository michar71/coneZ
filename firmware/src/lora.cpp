#include <Arduino.h>
#include <Wire.h>
#include <RadioLib.h>
#include "main.h"
#include "util.h"


// Default LoRa parameters
#define DOWNSTREAM_FREQUENCY    431.250         // 431.250MHz
#define DOWNSTREAM_BANDWIDTH    500.0           // 500kHz
#define DOWNSTREAM_SF           9               // SF7...SF12
#define DOWNSTREAM_CR           6               // 4/6 coding rate
#define DOWNSTREAM_PREAMBLE     8               // 8 preamble symbols
#define DOWNSTREAM_TXPOWER      5               // Transmit power
#define LORA_SYNC_WORD          0x1424          // LoRa private sync word

#define LORA_SPI_FREQ           1000000

extern uint32_t debug;

SPIClass spiLoRa( HSPI );
SPISettings spiLoRaSettings( LORA_SPI_FREQ, MSBFIRST, SPI_MODE0 );
SX1268 radio = new Module( LORA_PIN_CS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY, spiLoRa, spiLoRaSettings );

// External variables
extern Stream *OutputStream;


// IRQ handler for LoRa RX
volatile bool lora_rxdone_flag = false;

void IRAM_ATTR lora_rxdone( void )
{
  lora_rxdone_flag = true;
}


int lora_setup( void )
{
  OutputStream->print( "\nInit LoRa... " );

  spiLoRa.begin( LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS );


  radio.setTCXO( 1.8, 5000 );
  radio.setDio2AsRfSwitch();

  //int status = radio.begin( DOWNSTREAM_FREQUENCY, DOWNSTREAM_BANDWIDTH, DOWNSTREAM_SF, DOWNSTREAM_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, DOWNSTREAM_TXPOWER, DOWNSTREAM_PREAMBLE );
  int status = radio.begin( DOWNSTREAM_FREQUENCY );

  if( status != RADIOLIB_ERR_NONE )
  {
    OutputStream->print( "Failed, status=" );
    OutputStream->println( status );

    //while( 1 )
    //  delay( 1 );   // Dead in the water
    blinkloop( 3 );
  }

  OutputStream->print( "OK\n" );

  // Set misc LoRa parameters.
  //radio.setSyncWord( 0xDE, 0xAD );
  radio.setSpreadingFactor( DOWNSTREAM_SF );
  radio.setBandwidth( DOWNSTREAM_BANDWIDTH );
  radio.setCodingRate( DOWNSTREAM_CR );
  radio.setPreambleLength( DOWNSTREAM_PREAMBLE );
  radio.setSyncWord( 0x12 );
  radio.setCRC( true );

  radio.setDio1Action( lora_rxdone );
  status = radio.startReceive();

  if( status == RADIOLIB_ERR_NONE )
  {
    OutputStream->print( "LoRa set to receive mode.\n" );
  }
  else
  {
    OutputStream->printf( "Failed to set LoRa to receive mode, status=%d\n", status );
  }

  return 0;
}


void lora_rx( void )
{
    unsigned int len;
    uint8_t buf[256];
    int status;
    float RSSI;
    float SNR;

    if( !lora_rxdone_flag )
        return;

    lora_rxdone_flag = false;

    if( debug & DEBUG_MSG_LORA )
    {
        OutputStream->print( "\nWe have RX flag!\n" );
        OutputStream->print( "radio.available = " );
        OutputStream->println( radio.available() );
        OutputStream->print( "radio.getRSSI = " );
        OutputStream->println( radio.getRSSI() );
        OutputStream->print( "radio.getSNR = " );
        OutputStream->println( radio.getSNR() );
        OutputStream->print( "radio.getPacketLength = " );
        OutputStream->println( radio.getPacketLength() );
    }
        
    String str;
    int16_t state = radio.readData( str );

    if( state == RADIOLIB_ERR_NONE )
    {
        if( debug & DEBUG_MSG_LORA_RAW )
        {
            OutputStream->print( "Packet: " );
            OutputStream->println( str );
            hexdump( (uint8_t*)str.c_str(), str.length() );
            OutputStream->print( "\n" );
        }
    }
}
