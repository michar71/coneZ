#include <Arduino.h>
#include <Wire.h>
#include <RadioLib.h>
#include "main.h"
#include "util.h"
#include "printManager.h"


// Default LoRa parameters
#define DOWNSTREAM_FREQUENCY    431.250         // 431.250MHz
#define DOWNSTREAM_BANDWIDTH    500.0           // 500kHz
#define DOWNSTREAM_SF           9               // SF7...SF12
#define DOWNSTREAM_CR           6               // 4/6 coding rate
#define DOWNSTREAM_PREAMBLE     8               // 8 preamble symbols
#define DOWNSTREAM_TXPOWER      5               // Transmit power
#define LORA_SYNC_WORD          0x1424          // LoRa private sync word

#define LORA_SPI_FREQ           1000000

SPIClass spiLoRa( HSPI );
SPISettings spiLoRaSettings( LORA_SPI_FREQ, MSBFIRST, SPI_MODE0 );
SX1268 radio = new Module( LORA_PIN_CS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY, spiLoRa, spiLoRaSettings );


// IRQ handler for LoRa RX
volatile bool lora_rxdone_flag = false;

void IRAM_ATTR lora_rxdone( void )
{
  lora_rxdone_flag = true;
}


int lora_setup( void )
{
  Serial.println("Init LoRa... ");

  spiLoRa.begin( LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS );


  radio.setTCXO( 1.8, 5000 );
  radio.setDio2AsRfSwitch();

  //int status = radio.begin( DOWNSTREAM_FREQUENCY, DOWNSTREAM_BANDWIDTH, DOWNSTREAM_SF, DOWNSTREAM_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, DOWNSTREAM_TXPOWER, DOWNSTREAM_PREAMBLE );
  int status = radio.begin( DOWNSTREAM_FREQUENCY );

  if( status != RADIOLIB_ERR_NONE )
  {
     Serial.print( "Failed, status=" );
     Serial.println( status );

    //while( 1 )
    //  delay( 1 );   // Dead in the water
    blinkloop( 3 );
  }

   Serial.println( "OK" );

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
     Serial.println( "LoRa set to receive mode." );
  }
  else
  {
     Serial.printf( "Failed to set LoRa to receive mode, status=%d", status );
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

    printfnl(SOURCE_LORA, "\nWe have RX flag!\n" );
    printfnl(SOURCE_LORA, "radio.available = %d\n", radio.available() );
    printfnl(SOURCE_LORA, "radio.getRSSI = %f\n" , radio.getRSSI() );
    printfnl(SOURCE_LORA, "radio.getSNR = %f\n" ,radio.getSNR() );
    printfnl(SOURCE_LORA,  "radio.getPacketLength = %d\n" ,radio.getPacketLength() );

        
    String str;
    int16_t state = radio.readData( str );

    
    if( state == RADIOLIB_ERR_NONE )
    {
            printfnl(SOURCE_LORA, "Packet: %s\n",str.c_str() );
            //hexdump( (uint8_t*)str.c_str(), str.length() );
    }
    
}
