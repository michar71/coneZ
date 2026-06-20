#include <stdint.h>
#include <string.h>
#include <RadioLib.h>
#include "main.h"
#include "util.h"
#include "printManager.h"
#include "config.h"
#include "lora_hal.h"
#include "conez_usb.h"
#include "lora_proto.h"

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


// ---- v1 BEACON (0x01) tracking ----------------------------------------------
static struct {
    bool     valid;
    uint32_t uptime_ms;
    float    rssi, snr;
    uint8_t  version, mode, sf, cr;
    uint32_t epoch_s, freq_hz, bw_hz;
    uint16_t epoch_ms, sync_word, manifest_serial;
    char     callsign[LP_BCN_CALLSIGN_LEN + 1];
} g_beacon;

static void lora_handle_beacon( const uint8_t *p, size_t len, float rssi, float snr )
{
    if (len < LP_BCN_LEN) {
        printfnl(SOURCE_LORA, "BEACON too short (%d B)\n", (int)len);
        return;
    }
    g_beacon.valid           = true;
    g_beacon.uptime_ms       = uptime_ms();
    g_beacon.rssi            = rssi;
    g_beacon.snr             = snr;
    g_beacon.version         = p[LP_BCN_VERSION];
    g_beacon.epoch_s         = lp_rd_u32(p + LP_BCN_EPOCH_S);
    g_beacon.epoch_ms        = lp_rd_u16(p + LP_BCN_EPOCH_MS);
    g_beacon.mode            = p[LP_BCN_MODE];
    g_beacon.freq_hz         = lp_rd_u32(p + LP_BCN_FREQ);
    g_beacon.bw_hz           = lp_rd_u32(p + LP_BCN_BW);
    g_beacon.sf              = p[LP_BCN_SF];
    g_beacon.cr              = p[LP_BCN_CR];
    g_beacon.sync_word       = lp_rd_u16(p + LP_BCN_SYNC);
    g_beacon.manifest_serial = lp_rd_u16(p + LP_BCN_MANIFEST);
    memcpy(g_beacon.callsign, p + LP_BCN_CALLSIGN, LP_BCN_CALLSIGN_LEN);
    g_beacon.callsign[LP_BCN_CALLSIGN_LEN] = '\0';
    for (int i = LP_BCN_CALLSIGN_LEN - 1; i >= 0 && g_beacon.callsign[i] == ' '; i--)
        g_beacon.callsign[i] = '\0';

    printfnl(SOURCE_LORA,
             "BEACON %s v%u %.3f MHz BW%u SF%u CR4/%u sync 0x%04X manifest %u  t=%u.%03u  RSSI %.0f SNR %.1f\n",
             g_beacon.callsign, (unsigned)g_beacon.version, g_beacon.freq_hz / 1e6,
             (unsigned)(g_beacon.bw_hz / 1000), (unsigned)g_beacon.sf, (unsigned)g_beacon.cr,
             (unsigned)g_beacon.sync_word, (unsigned)g_beacon.manifest_serial,
             (unsigned)g_beacon.epoch_s, (unsigned)g_beacon.epoch_ms, rssi, snr);
}

// Show the last beacon in the `lora` status command (SOURCE_COMMANDS).
void lora_print_beacon( void )
{
    if (!g_beacon.valid) {
        printfnl(SOURCE_COMMANDS, "  Master beacon: none heard yet\n");
        return;
    }
    uint32_t age = (uptime_ms() - g_beacon.uptime_ms) / 1000;
    printfnl(SOURCE_COMMANDS, "  Master beacon (%us ago): %s  v%u  manifest %u\n",
             (unsigned)age, g_beacon.callsign, (unsigned)g_beacon.version,
             (unsigned)g_beacon.manifest_serial);
    printfnl(SOURCE_COMMANDS, "    chan: %.3f MHz  BW %u kHz  SF%u  CR4/%u  sync 0x%04X\n",
             g_beacon.freq_hz / 1e6, (unsigned)(g_beacon.bw_hz / 1000),
             (unsigned)g_beacon.sf, (unsigned)g_beacon.cr, (unsigned)g_beacon.sync_word);
    printfnl(SOURCE_COMMANDS, "    master time %u.%03u  RSSI %.0f dBm  SNR %.1f dB\n",
             (unsigned)g_beacon.epoch_s, (unsigned)g_beacon.epoch_ms, g_beacon.rssi, g_beacon.snr);
}


void lora_rx( void )
{
    if( lora_tx_active )      // don't touch the radio mid-transmit
        return;
    if( !lora_rxdone_flag )
        return;
    lora_rxdone_flag = false;

    uint8_t rxbuf[256];
    size_t rxlen = radio.getPacketLength();
    if (rxlen > sizeof(rxbuf) - 1)
        rxlen = sizeof(rxbuf) - 1;
    int16_t state = radio.readData( rxbuf, rxlen );
    float rssi = radio.getRSSI();
    float snr  = radio.getSNR();

    if( state != RADIOLIB_ERR_NONE )
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
        radio.startReceive();
        return;
    }

    rx_count++;

    // v1 dispatch on the 4-byte common header (type, network, src, dst)
    if (rxlen >= LP_HDR_LEN)
    {
        uint8_t ptype = rxbuf[LP_HDR_TYPE];
        switch (ptype)
        {
            case LP_PKT_BEACON:
                lora_handle_beacon(rxbuf, rxlen, rssi, snr);
                break;
            default:
                printfnl(SOURCE_LORA, "RX type 0x%02X net %u src %u dst %u  %d B  RSSI %.0f SNR %.1f\n",
                         (unsigned)ptype, (unsigned)rxbuf[LP_HDR_NET], (unsigned)rxbuf[LP_HDR_SRC],
                         (unsigned)rxbuf[LP_HDR_DST], (int)rxlen, rssi, snr);
                break;
        }
    }
    else
    {
        printfnl(SOURCE_LORA, "RX runt (%d B) RSSI %.0f\n", (int)rxlen, rssi);
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
