#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <RadioLib.h>
#include "main.h"
#include "util.h"
#include "printManager.h"
#include "config.h"
#include "lora_hal.h"
#include "conez_usb.h"
#include "lora.h"
#include "lora_proto.h"
#include "dist.h"
#include "gps.h"

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


static void scan_init(void);          // load scanlist + start scanning (defined below)
static void scan_notify_beacon(void); // called when a beacon is decoded

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

  scan_init();   // load scanlist + begin scanning for the master beacon

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

    // Discipline our clock from the beacon when we have no GPS/NTP of our own.
    bool t_applied = time_set_from_beacon((uint64_t)g_beacon.epoch_s * 1000ULL + g_beacon.epoch_ms);

    printfnl(SOURCE_LORA,
             "BEACON %s v%u %.3f MHz BW%u SF%u CR4/%u sync 0x%04X manifest %u  t=%u.%03u  RSSI %.0f SNR %.1f  clock:%s\n",
             g_beacon.callsign, (unsigned)g_beacon.version, g_beacon.freq_hz / 1e6,
             (unsigned)(g_beacon.bw_hz / 1000), (unsigned)g_beacon.sf, (unsigned)g_beacon.cr,
             (unsigned)g_beacon.sync_word, (unsigned)g_beacon.manifest_serial,
             (unsigned)g_beacon.epoch_s, (unsigned)g_beacon.epoch_ms, rssi, snr,
             t_applied ? "set" : "kept");

    scan_notify_beacon();   // a beacon here means we're on the master's channel
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


// ===== Scanlist & channel lock (Phase 2) =====================================
// The cone cycles through a scanlist, dwelling on each channel long enough to
// hear a beacon (> the beacon period). On a beacon it LOCKS; if the beacon is
// lost for a while it resumes scanning. Scanlist comes from /scanlist.txt
// (LittleFS) if present, else a built-in default.

#define SCAN_MAX_ENTRIES   48
#define SCAN_DWELL_MS      12000   // listen per channel (> beacon period)
#define SCAN_LOSS_MS       40000   // re-scan after this long locked with no beacon

typedef struct {
    uint8_t  mode;                 // LP_MODE_LORA / LP_MODE_FSK
    uint32_t freq_hz;
    uint32_t bw_hz;                // LoRa bandwidth
    uint8_t  sf, cr;               // LoRa
    uint16_t sync_word;            // LoRa 16-bit sync
    uint32_t bitrate_bps, freqdev_hz, rxbw_hz;  // FSK
    char     fsk_sync[17];         // FSK sync word as hex string
} scan_entry_t;

// Built-in default scanlist (used when /scanlist.txt is absent).
static const scan_entry_t DEFAULT_SCANLIST[] = {
    { LP_MODE_LORA, 431250000, 500000, 7, 5, 0xDEAD, 0, 0, 0, "" },
    { LP_MODE_LORA, 431250000, 125000, 9, 5, 0x12,   0, 0, 0, "" },
    { LP_MODE_LORA, 433000000, 500000, 7, 5, 0xDEAD, 0, 0, 0, "" },
};

static scan_entry_t scanlist[SCAN_MAX_ENTRIES];
static int  scanlist_n   = 0;
static bool scan_enabled = true;
static enum { SCAN_LOCKED, SCAN_SCANNING } scan_state = SCAN_SCANNING;
static int      scan_idx        = 0;
static uint32_t scan_dwell_until = 0;

// Parse one scanlist line. Returns 1=entry, 0=blank/comment, -1=malformed.
static int scan_parse_line(const char *s, scan_entry_t *e)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#' || *s == '\0' || *s == '\r' || *s == '\n') return 0;
    char mode = *s++;
    memset(e, 0, sizeof(*e));
    if (mode == 'L' || mode == 'l') {
        unsigned long freq, bw; int sf, cr; char sync[16];
        if (sscanf(s, "%lu %lu %d %d %15s", &freq, &bw, &sf, &cr, sync) != 5) return -1;
        e->mode = LP_MODE_LORA; e->freq_hz = (uint32_t)freq; e->bw_hz = (uint32_t)bw;
        e->sf = (uint8_t)sf; e->cr = (uint8_t)cr;
        e->sync_word = (uint16_t)strtol(sync, NULL, 16);
        return 1;
    } else if (mode == 'F' || mode == 'f') {
        unsigned long freq, br, fd, rxbw; char sync[17];
        if (sscanf(s, "%lu %lu %lu %lu %16s", &freq, &br, &fd, &rxbw, sync) != 5) return -1;
        e->mode = LP_MODE_FSK; e->freq_hz = (uint32_t)freq; e->bitrate_bps = (uint32_t)br;
        e->freqdev_hz = (uint32_t)fd; e->rxbw_hz = (uint32_t)rxbw;
        strlcpy(e->fsk_sync, sync, sizeof(e->fsk_sync));
        return 1;
    }
    return -1;
}

static void scan_load(void)
{
    scanlist_n = 0;
    char path[80];
    FILE *f = fopen(lfs_path(path, sizeof(path), "/scanlist.txt"), "r");
    if (f) {
        char line[160];
        while (scanlist_n < SCAN_MAX_ENTRIES && fgets(line, sizeof(line), f)) {
            scan_entry_t e;
            if (scan_parse_line(line, &e) == 1) scanlist[scanlist_n++] = e;
        }
        fclose(f);
        printfnl(SOURCE_LORA, "scanlist: loaded %d entries from /scanlist.txt\n", scanlist_n);
    }
    if (scanlist_n == 0) {
        int n = (int)(sizeof(DEFAULT_SCANLIST) / sizeof(DEFAULT_SCANLIST[0]));
        for (int i = 0; i < n && i < SCAN_MAX_ENTRIES; i++) scanlist[scanlist_n++] = DEFAULT_SCANLIST[i];
        printfnl(SOURCE_LORA, "scanlist: using %d built-in default entries\n", scanlist_n);
    }
}

static void scan_describe(const scan_entry_t *e, char *buf, size_t n)
{
    if (e->mode == LP_MODE_FSK)
        snprintf(buf, n, "FSK %.3f MHz %u bps", e->freq_hz / 1e6, (unsigned)e->bitrate_bps);
    else
        snprintf(buf, n, "LoRa %.3f MHz BW%u SF%u sync 0x%04X",
                 e->freq_hz / 1e6, (unsigned)(e->bw_hz / 1000), (unsigned)e->sf, (unsigned)e->sync_word);
}

// Tune the radio to a scanlist entry by writing config.* and re-initialising.
static void scan_apply(const scan_entry_t *e)
{
    config.lora_frequency = e->freq_hz / 1e6f;
    if (e->mode == LP_MODE_FSK) {
        strlcpy(config.lora_rf_mode, "fsk", sizeof(config.lora_rf_mode));
        config.fsk_bitrate = e->bitrate_bps / 1000.0f;
        config.fsk_freqdev = e->freqdev_hz / 1000.0f;
        config.fsk_rxbw    = e->rxbw_hz / 1000.0f;
        strlcpy(config.fsk_syncword, e->fsk_sync, sizeof(config.fsk_syncword));
    } else {
        strlcpy(config.lora_rf_mode, "lora", sizeof(config.lora_rf_mode));
        config.lora_bandwidth = e->bw_hz / 1000.0f;
        config.lora_sf        = e->sf;
        config.lora_cr        = e->cr;
        config.lora_sync_word = e->sync_word;
    }
    lora_reinit();
}

static void scan_init(void)
{
    scan_load();
    if (scanlist_n > 0) {
        scan_state = SCAN_SCANNING;
        scan_idx   = 0;
        scan_apply(&scanlist[scan_idx]);
        scan_dwell_until = uptime_ms() + SCAN_DWELL_MS;
        char d[72]; scan_describe(&scanlist[scan_idx], d, sizeof(d));
        printfnl(SOURCE_LORA, "scan: start, entry 0/%d %s\n", scanlist_n, d);
    }
}

static void scan_notify_beacon(void)
{
    if (scan_state == SCAN_SCANNING) {
        scan_state = SCAN_LOCKED;
        printfnl(SOURCE_LORA, "scan: LOCKED on entry %d/%d (%.3f MHz)\n",
                 scan_idx, scanlist_n, config.lora_frequency);
    }
}

void lora_scan_tick(void)   // called from loop() after lora_rx()
{
    if (!scan_enabled || scanlist_n == 0 || lora_tx_active) return;
    uint32_t now = uptime_ms();

    if (scan_state == SCAN_SCANNING) {
        if ((int32_t)(now - scan_dwell_until) >= 0) {
            scan_idx = (scan_idx + 1) % scanlist_n;
            scan_apply(&scanlist[scan_idx]);
            scan_dwell_until = now + SCAN_DWELL_MS;
            char d[72]; scan_describe(&scanlist[scan_idx], d, sizeof(d));
            printfnl(SOURCE_LORA, "scan: entry %d/%d %s\n", scan_idx, scanlist_n, d);
        }
    } else { // SCAN_LOCKED
        if (g_beacon.valid && (now - g_beacon.uptime_ms) >= SCAN_LOSS_MS) {
            printfnl(SOURCE_LORA, "scan: beacon lost (%us), re-scanning\n",
                     (unsigned)((now - g_beacon.uptime_ms) / 1000));
            scan_state = SCAN_SCANNING;
            scan_idx   = 0;
            scan_apply(&scanlist[scan_idx]);
            scan_dwell_until = now + SCAN_DWELL_MS;
        }
    }
}

void lora_scan_set_enabled(bool en)   // `lora scan on|off`
{
    scan_enabled = en;
    if (en) {
        scan_state = SCAN_SCANNING;
        scan_idx   = 0;
        if (scanlist_n > 0) scan_apply(&scanlist[scan_idx]);
        scan_dwell_until = uptime_ms() + SCAN_DWELL_MS;
        printfnl(SOURCE_COMMANDS, "scan: enabled (scanning from entry 0)\n");
    } else {
        printfnl(SOURCE_COMMANDS, "scan: disabled (staying on current channel)\n");
    }
}

void lora_scan_print(void)   // shown in `lora` status
{
    printfnl(SOURCE_COMMANDS, "  Scan: %s  (%d entries)\n",
             !scan_enabled ? "off" : (scan_state == SCAN_LOCKED ? "LOCKED" : "scanning"),
             scanlist_n);
    if (scan_enabled && scanlist_n > 0) {
        char d[72]; scan_describe(&scanlist[scan_idx], d, sizeof(d));
        printfnl(SOURCE_COMMANDS, "    entry %d/%d: %s\n", scan_idx, scanlist_n, d);
    }
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
            case LP_PKT_DIST_DATA:
                dist_handle_chunk(rxbuf, rxlen);
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
