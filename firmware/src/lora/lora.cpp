#include <stdint.h>
#include <string.h>
#include <RadioLib.h>
#include "main.h"
#include "util.h"
#include "printManager.h"
#include "config.h"
#include "lora_hal.h"
#include "conez_usb.h"

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

// True while a CLI-triggered transmit is in progress, so the RX poll (loopTask)
// skips the radio and doesn't race the TX on the shared SPI bus.
static volatile bool lora_tx_active = false;

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


// Apply the configured LoRa sync word, supporting a full 16-bit register value.
//
// config.lora_sync_word is interpreted exactly like the Python LoRaRF master:
//   <= 0xFF : single-byte convention, 0xXY expands to register 0xX4Y4
//             (e.g. 0x12 -> 0x1424, the LoRaWAN "private network" value)
//   >  0xFF : raw 16-bit register value (e.g. 0xDEAD)
//
// RadioLib's setSyncWord(syncWord, controlBits) packs the two register bytes as
//   MSB = (syncWord & 0xF0) | ((controlBits & 0xF0) >> 4)
//   LSB = ((syncWord & 0x0F) << 4) | (controlBits & 0x0F)
// so we invert that mapping to hit an arbitrary 16-bit register value.
static void lora_apply_sync_word( void )
{
    uint32_t sw = (uint32_t)config.lora_sync_word;

    uint16_t reg;
    if (sw <= 0xFF)
        reg = (uint16_t)((((sw & 0xF0) | 0x04) << 8) | (((sw << 4) | 0x04) & 0xFF));
    else
        reg = (uint16_t)(sw & 0xFFFF);

    uint8_t msb = (reg >> 8) & 0xFF;
    uint8_t lsb = reg & 0xFF;
    uint8_t sync_byte    = (msb & 0xF0) | ((lsb & 0xF0) >> 4);
    uint8_t control_bits = ((msb & 0x0F) << 4) | (lsb & 0x0F);
    radio.setSyncWord(sync_byte, control_bits);
}


int lora_setup( void )
{
  usb_printf("Init LoRa...\n");

  radio.setTCXO( 1.8, 5000 );
  radio.setDio2AsRfSwitch();

  fsk_mode = (strcasecmp(config.lora_rf_mode, "fsk") == 0);

  int status;

  if (fsk_mode)
  {
    usb_printf("Mode: FSK\n");
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
    usb_printf("Mode: LoRa\n");
    status = radio.begin( config.lora_frequency );
  }

  if( status != RADIOLIB_ERR_NONE )
  {
     usb_printf("Failed, status=%d\n", status);
    blinkloop( 3 );
  }

  usb_printf("OK\n");

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
    lora_apply_sync_word();
    radio.setCRC( true );
  }

  radio.setDio1Action( lora_rxdone );
  status = radio.startReceive();

  if( status == RADIOLIB_ERR_NONE )
  {
     usb_printf("LoRa set to receive mode.\n");
  }
  else
  {
     usb_printf("Failed to set LoRa to receive mode, status=%d\n", status);
  }

  return 0;
}


void lora_rx( void )
{
    if( lora_tx_active )      // don't touch the radio mid-transmit
        return;

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

    if( state == RADIOLIB_ERR_NONE )
    {
            rx_count++;
            // Replace non-printable bytes with '.' for safe display
            char display[256];
            for (size_t i = 0; i < rxlen; i++)
                display[i] = (rxbuf[i] >= 0x20 && rxbuf[i] < 0x7F) ? (char)rxbuf[i] : '.';
            display[rxlen] = '\0';
            printfnl(SOURCE_LORA, "Packet (%d bytes): %s\n", (int)rxlen, display);
    }
    else
    {
            const char *reason;
            switch (state)
            {
                case RADIOLIB_ERR_CRC_MISMATCH:        reason = "CRC mismatch";   break;
                case RADIOLIB_ERR_RX_TIMEOUT:          reason = "RX timeout";     break;
                case RADIOLIB_ERR_LORA_HEADER_DAMAGED: reason = "header damaged"; break;
                default:                               reason = "?";              break;
            }
            printfnl(SOURCE_LORA, "readData failed: state=%d (%s)\n", (int)state, reason);
    }

    // Re-enter receive mode for the next packet
    radio.startReceive();
}


// Transmit a LoRa packet (blocking), then return to RX-continuous. This is the
// first TX path on the ConeZ board; it exercises the E22-400M22S TXEN/RXEN
// switch, which is driven from the SX1268 DIO2 (3.3 V) via the board's MOSFET.
// Returns 0 on success, else the RadioLib error code.
int lora_tx( const uint8_t *data, size_t len )
{
    lora_tx_active = true;
    int16_t state = radio.transmit( (uint8_t *)data, len );
    if( state == RADIOLIB_ERR_NONE )
        tx_count++;
    lora_rxdone_flag = false;             // discard any TxDone-triggered IRQ flag
    radio.setDio1Action( lora_rxdone );   // re-arm RX-done IRQ
    radio.startReceive();                 // back to RX continuous
    lora_tx_active = false;
    return ( state == RADIOLIB_ERR_NONE ) ? 0 : (int)state;
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
        lora_apply_sync_word();
        radio.setCRC(true);
    }

    radio.setDio1Action(lora_rxdone);
    radio.startReceive();
    return 0;
}
