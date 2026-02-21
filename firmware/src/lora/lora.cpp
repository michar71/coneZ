#include <Arduino.h>
#include <RadioLib.h>
#include "main.h"
#include "util.h"
#include "printManager.h"
#include "config.h"
#include "lora_hal.h"

static EspHal loraHal(LORA_PIN_SCK, LORA_PIN_MISO, LORA_PIN_MOSI);

// Create the LoRa radio object depending on which board we're building for.
#ifdef BOARD_LORA_SX1268
  SX1268 radio = new Module(&loraHal, LORA_PIN_CS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);
#elif defined( BOARD_LORA_SX1262 )
  SX1262 radio = new Module(&loraHal, LORA_PIN_CS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);
#endif


// Mode flag — set during setup, read-only thereafter
static bool fsk_mode = false;

// Packet counters
static uint32_t rx_count = 0;
static uint32_t tx_count = 0;

// IRQ handler for LoRa RX
volatile bool lora_rxdone_flag = false;

void IRAM_ATTR lora_rxdone( void )
{
  lora_rxdone_flag = true;
}


// Parse hex string (e.g. "12AD") into byte array. Returns byte count, or 0 on error.
static int parse_hex_syncword(const char *hex, uint8_t *out, int maxlen)
{
    int slen = strlen(hex);
    if (slen < 2 || slen > maxlen * 2 || (slen & 1) != 0)
        return 0;

    int nbytes = slen / 2;
    for (int i = 0; i < nbytes; i++)
    {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        auto hexval = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = hexval(hi);
        int l = hexval(lo);
        if (h < 0 || l < 0) return 0;
        out[i] = (h << 4) | l;
    }
    return nbytes;
}


int lora_setup( void )
{
  Serial.println("Init LoRa... ");

  radio.setTCXO( 1.8, 5000 );
  radio.setDio2AsRfSwitch();

  fsk_mode = (strcasecmp(config.lora_rf_mode, "fsk") == 0);

  int status;

  if (fsk_mode)
  {
    Serial.println("Mode: FSK");
    status = radio.beginFSK(
        config.lora_frequency,
        config.fsk_bitrate,
        config.fsk_freqdev,
        config.fsk_rxbw,
        config.lora_tx_power,
        config.lora_preamble,
        0,      // tcxoVoltage — already set above
        false   // useLDO
    );
  }
  else
  {
    Serial.println("Mode: LoRa");
    status = radio.begin( config.lora_frequency );
  }

  if( status != RADIOLIB_ERR_NONE )
  {
     Serial.print( "Failed, status=" );
     Serial.println( status );
    blinkloop( 3 );
  }

  Serial.println( "OK" );

  if (fsk_mode)
  {
    // Data shaping
    static const uint8_t shaping_map[] = {
        RADIOLIB_SHAPING_NONE,
        RADIOLIB_SHAPING_0_3,
        RADIOLIB_SHAPING_0_5,
        RADIOLIB_SHAPING_0_7,
        RADIOLIB_SHAPING_1_0,
    };
    int idx = config.fsk_shaping;
    if (idx < 0 || idx >= (int)(sizeof(shaping_map) / sizeof(shaping_map[0])))
        idx = 0;
    radio.setDataShaping(shaping_map[idx]);

    // Whitening
    if (config.fsk_whitening)
        radio.setWhitening(true);

    // Sync word (byte array)
    uint8_t sw_bytes[8];
    int sw_len = parse_hex_syncword(config.fsk_syncword, sw_bytes, 8);
    if (sw_len > 0)
        radio.setSyncWord(sw_bytes, sw_len);

    // CRC
    radio.setCRC(config.fsk_crc);
  }
  else
  {
    // LoRa-specific parameters
    radio.setSpreadingFactor( config.lora_sf );
    radio.setBandwidth( config.lora_bandwidth );
    radio.setCodingRate( config.lora_cr );
    radio.setPreambleLength( config.lora_preamble );
    radio.setSyncWord( config.lora_sync_word );
    radio.setCRC( true );
  }

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

        
    uint8_t rxbuf[256];
    size_t rxlen = radio.getPacketLength();
    if (rxlen > sizeof(rxbuf) - 1)
        rxlen = sizeof(rxbuf) - 1;
    int16_t state = radio.readData( rxbuf, rxlen );
    rxbuf[rxlen] = '\0';

    if( state == RADIOLIB_ERR_NONE )
    {
            rx_count++;
            printfnl(SOURCE_LORA, "Packet: %s\n", (const char *)rxbuf );
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

const char *lora_get_mode(void)
{
    return fsk_mode ? "FSK" : "LoRa";
}

bool lora_is_fsk(void)
{
    return fsk_mode;
}

float lora_get_bitrate(void)
{
    return config.fsk_bitrate;
}

float lora_get_freqdev(void)
{
    return config.fsk_freqdev;
}

float lora_get_rxbw(void)
{
    return config.fsk_rxbw;
}

uint32_t lora_get_rx_count(void)
{
    return rx_count;
}

uint32_t lora_get_tx_count(void)
{
    return tx_count;
}

float lora_get_datarate(void)
{
    if (fsk_mode)
        return config.fsk_bitrate * 1000.0f;  // kbps -> bps

    // LoRa bit rate: SF * BW * 4 / (2^SF * CR)
    // BW is in kHz, result in bps
    float bw = config.lora_bandwidth * 1000.0f;  // kHz -> Hz
    int sf = config.lora_sf;
    int cr = config.lora_cr;  // denominator of 4/cr
    float chips_per_symbol = (float)(1 << sf);
    return (sf * 4.0f * bw) / (chips_per_symbol * cr);
}


int lora_set_frequency(float freq)
{
    int status = radio.setFrequency(freq);
    if (status != RADIOLIB_ERR_NONE)
        return status;
    radio.startReceive();
    return 0;
}

int lora_set_tx_power(int power)
{
    int status = radio.setOutputPower(power);
    if (status != RADIOLIB_ERR_NONE)
        return status;
    radio.startReceive();
    return 0;
}

int lora_set_bandwidth(float bw)
{
    int status = radio.setBandwidth(bw);
    if (status != RADIOLIB_ERR_NONE)
        return status;
    radio.startReceive();
    return 0;
}

int lora_set_sf(int sf)
{
    int status = radio.setSpreadingFactor(sf);
    if (status != RADIOLIB_ERR_NONE)
        return status;
    radio.startReceive();
    return 0;
}

int lora_set_cr(int cr)
{
    int status = radio.setCodingRate(cr);
    if (status != RADIOLIB_ERR_NONE)
        return status;
    radio.startReceive();
    return 0;
}

int lora_reinit(void)
{
    fsk_mode = (strcasecmp(config.lora_rf_mode, "fsk") == 0);

    int status;

    if (fsk_mode)
    {
        status = radio.beginFSK(
            config.lora_frequency,
            config.fsk_bitrate,
            config.fsk_freqdev,
            config.fsk_rxbw,
            config.lora_tx_power,
            config.lora_preamble,
            0,
            false
        );
    }
    else
    {
        status = radio.begin(config.lora_frequency);
    }

    if (status != RADIOLIB_ERR_NONE)
        return status;

    if (fsk_mode)
    {
        static const uint8_t shaping_map[] = {
            RADIOLIB_SHAPING_NONE,
            RADIOLIB_SHAPING_0_3,
            RADIOLIB_SHAPING_0_5,
            RADIOLIB_SHAPING_0_7,
            RADIOLIB_SHAPING_1_0,
        };
        int idx = config.fsk_shaping;
        if (idx < 0 || idx >= (int)(sizeof(shaping_map) / sizeof(shaping_map[0])))
            idx = 0;
        radio.setDataShaping(shaping_map[idx]);

        if (config.fsk_whitening)
            radio.setWhitening(true);

        uint8_t sw_bytes[8];
        int sw_len = parse_hex_syncword(config.fsk_syncword, sw_bytes, 8);
        if (sw_len > 0)
            radio.setSyncWord(sw_bytes, sw_len);

        radio.setCRC(config.fsk_crc);
    }
    else
    {
        radio.setSpreadingFactor(config.lora_sf);
        radio.setBandwidth(config.lora_bandwidth);
        radio.setCodingRate(config.lora_cr);
        radio.setPreambleLength(config.lora_preamble);
        radio.setSyncWord(config.lora_sync_word);
        radio.setCRC(true);
    }

    radio.setDio1Action(lora_rxdone);
    radio.startReceive();
    return 0;
}
