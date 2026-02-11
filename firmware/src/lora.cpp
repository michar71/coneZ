#include <Arduino.h>
#include <Wire.h>
#include <RadioLib.h>
#include "main.h"
#include "util.h"
#include "printManager.h"
#include "config.h"

#define LORA_SPI_FREQ           1000000

SPIClass spiLoRa( HSPI );
SPISettings spiLoRaSettings( LORA_SPI_FREQ, MSBFIRST, SPI_MODE0 );

// Create the LoRa radio object depending on which board we're building for.
#ifdef BOARD_LORA_SX1268
  SX1268 radio = new Module( LORA_PIN_CS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY, spiLoRa, spiLoRaSettings );
#elif defined( BOARD_LORA_SX1262 )
  SX1262 radio = new Module( LORA_PIN_CS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY, spiLoRa, spiLoRaSettings );
#endif


// IRQ handler for LoRa RX
volatile bool lora_rxdone_flag = false;

void IRAM_ATTR lora_rxdone( void )
{
  lora_rxdone_flag = true;
}


int lora_setup( void )
{
  Serial.println("Init LoRa... ");

  spiLoRa.begin( LORA_PIN_SCK, LORA_PIN_MISO, LORA_PIN_MOSI, LORA_PIN_CS );


  radio.setTCXO( 1.8, 5000 );
  radio.setDio2AsRfSwitch();

  int status = radio.begin( config.lora_frequency );

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
  radio.setSpreadingFactor( config.lora_sf );
  radio.setBandwidth( config.lora_bandwidth );
  radio.setCodingRate( config.lora_cr );
  radio.setPreambleLength( config.lora_preamble );
  radio.setSyncWord( config.lora_sync_word );
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

    // Re-enter receive mode for the next packet
    radio.startReceive();
}


float lora_get_rssi(void)
{
    return radio.getRSSI();
}

float lora_get_snr(void)
{
    return radio.getSNR();
}

float lora_get_frequency(void)
{
    return config.lora_frequency;
}

float lora_get_bandwidth(void)
{
    return config.lora_bandwidth;
}

int lora_get_sf(void)
{
    return config.lora_sf;
}
