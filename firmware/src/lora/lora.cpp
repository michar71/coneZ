#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <RadioLib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "main.h"
#include "util.h"
#include "printManager.h"
#include "config.h"
#include "lora_hal.h"
#include "conez_usb.h"
#include "lora.h"
#include "lora_proto.h"
#include "dist.h"
#include "scan.h"
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

// RSSI/SNR of the last GENUINELY-received packet. Invalid until the first real RX
// since boot: the SX1268 packet-RSSI/SNR registers read garbage before any packet
// arrives (e.g. -0.5 dBm / implausibly high SNR), so the `lora` status must not
// report them as a "Last RSSI/SNR" until lora_have_rx() is true.
static bool  rx_metrics_valid = false;
static float last_rssi = 0.0f;
static float last_snr  = 0.0f;

// Dedicated LoRa task (RX poll + scan) + a recursive mutex serializing ALL radio
// access (rx / reinit / tx / set_*) and the scan state it drives, so CLI radio
// commands on ShellTask can't race the task. Replaces the old lora_tx_active
// flag stopgap. Recursive because scan_step()->lora_reinit() nests the lock.
static SemaphoreHandle_t lora_mutex       = NULL;
static TaskHandle_t      lora_task_handle = NULL;

// Master on/off for the whole LoRa subsystem (config [lora] enabled, `lora on|off`).
// When false the radio is asleep and the LoRa task idles (no RX, no scan).
static bool lora_active = false;

// True when the current channel is marked receive-only (scanlist LX/FX or built-in
// *_RX). With config.lora_rx_only it gates all TX via lora_tx_allowed().
static bool chan_rx_only = false;

void lora_radio_lock(void)   { if (lora_mutex) xSemaphoreTakeRecursive(lora_mutex, portMAX_DELAY); }
void lora_radio_unlock(void) { if (lora_mutex) xSemaphoreGiveRecursive(lora_mutex); }

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

  if (!lora_mutex)
    lora_mutex = xSemaphoreCreateRecursiveMutex();

  // Always do the hardware init (TCXO, RF switch, begin) even when disabled, so
  // RadioLib remembers the TCXO/board config for later begin() calls; if disabled
  // we just sleep the radio at the end instead of scanning.
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
    // Don't hang the whole boot on a radio-init failure (e.g. a bad [lora]
    // channel in config) -- leave LoRa OFF, keep booting so the CLI is reachable
    // to fix it; `lora on` retries after the config is corrected.
    usb_printf("LoRa radio init FAILED, status=%d -- LoRa left OFF (check [lora] config)\n", status);
    lora_active = false;
    return 0;
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

  lora_rxdone_flag = false;   // drop any boot-time spurious RX-done so the first
                              // lora_rx() doesn't count/parse a ghost packet

  if (config.lora_enabled) {
    lora_active = true;
    scan_init();              // load scanlist + begin scanning for the master beacon
  } else {
    lora_active = false;
    radio.sleep();            // configured but parked; `lora on` wakes + scans
    usb_printf("LoRa disabled (config [lora] enabled=off), radio asleep\n");
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


void lora_rx( void )
{
    if( !lora_rxdone_flag )      // nothing pending -- check the flag before locking
        return;
    lora_rxdone_flag = false;

    uint8_t rxbuf[256];
    size_t  rxlen;
    int16_t state;
    float   rssi, snr;

    // Hold the radio only for the read + re-arm; the dispatch below (which may do
    // a slow dist LittleFS commit) runs UNLOCKED so a CLI TX isn't blocked behind
    // it. Re-arming RX before dispatch also avoids missing the next chunk.
    lora_radio_lock();
    rxlen = radio.getPacketLength();
    if (rxlen > sizeof(rxbuf) - 1)
        rxlen = sizeof(rxbuf) - 1;
    state = radio.readData( rxbuf, rxlen );
    rssi  = radio.getRSSI();
    snr   = radio.getSNR();
    radio.startReceive();
    lora_radio_unlock();

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
        return;
    }

    rx_count++;
    last_rssi = rssi;            // a real packet -> these are now meaningful
    last_snr  = snr;
    rx_metrics_valid = true;

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
            case LP_PKT_DIST_PARITY:
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
}


// The dedicated LoRa task: poll RX + drive the scan state machine, OFF loopTask
// so heavy lora_reinit() channel hops can't starve core-1 idle (was the WDT
// trigger). No affinity -> the scheduler keeps it off a busy core 1. RX is
// IRQ-flagged; a short delay bounds latency while yielding for idle/WDT.
static void lora_task_fn( void *arg )
{
    (void)arg;
    for (;;)
    {
        if (lora_active) {
            lora_rx();
            lora_scan_tick();
            dist_tick();                       // free a stalled (master-lost) transfer
            vTaskDelay( pdMS_TO_TICKS(2) );
        } else {
            vTaskDelay( pdMS_TO_TICKS(50) );   // idle while LoRa is off
        }
    }
}

void lora_start_task( void )
{
    if (!lora_task_handle)
        xTaskCreate( lora_task_fn, "lora", 6144, NULL, 1, &lora_task_handle );
}

// Master on/off for the LoRa subsystem (`lora on|off`, [lora] enabled). Off stops
// scanning, closes the streamed scanlist files and sleeps the radio; on wakes +
// reconfigures the radio (via scan's lora_reinit) and restarts scanning.
void lora_set_active( bool on )
{
    lora_radio_lock();
    if (on && !lora_active) {
        lora_active = true;
        lora_scan_set_enabled(true);   // reinits the radio (scan_apply) + scans
    } else if (!on && lora_active) {
        lora_scan_set_enabled(false);  // stop scanning + close scanlist files
        dist_abort();                  // free any in-progress dist transfer buffers
        radio.sleep();                 // low power; woken by begin() on `lora on`
        lora_active = false;
    }
    lora_radio_unlock();
}

bool lora_is_active( void ) { return lora_active; }

// Mark the current channel receive-only (called by the scanner per tuned entry).
void lora_set_channel_rx_only( bool ro ) { chan_rx_only = ro; }

// TX is permitted only when the subsystem is on, the config isn't in listen-only
// mode, and the current channel isn't marked receive-only. All TX (CLI send and,
// later, registration/polling) funnels through lora_tx(), which enforces this.
bool lora_tx_allowed( void )
{
    return lora_active && !config.lora_rx_only && !chan_rx_only;
}


// Transmit a LoRa packet (blocking), then return to RX-continuous. This is the
// first TX path on the ConeZ board; it exercises the E22-400M22S TXEN/RXEN
// switch, which is driven from the SX1268 DIO2 (3.3 V) via the board's MOSFET.
// Returns 0 on success, else the RadioLib error code.
int lora_tx( const uint8_t *data, size_t len )
{
    if (!lora_tx_allowed())               // RX-only channel / [lora] rx_only / LoRa off
        return LORA_TX_INHIBITED;
    lora_radio_lock();                    // serialize against the LoRa task's RX/scan
    int16_t state = radio.transmit( (uint8_t *)data, len );
    if( state == RADIOLIB_ERR_NONE )
        tx_count++;
    lora_rxdone_flag = false;             // discard any TxDone-triggered IRQ flag
    radio.setDio1Action( lora_rxdone );   // re-arm RX-done IRQ
    radio.startReceive();                 // back to RX continuous
    lora_radio_unlock();
    return ( state == RADIOLIB_ERR_NONE ) ? 0 : (int)state;
}


// RSSI/SNR of the LAST RECEIVED PACKET -- not a live channel read. Meaningless
// until lora_have_rx() returns true (see rx_metrics_valid).
float lora_get_rssi(void)
{
    return last_rssi;
}

float lora_get_snr(void)
{
    return last_snr;
}

// True once at least one genuine packet has been received since boot.
bool lora_have_rx(void)
{
    return rx_metrics_valid;
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
    lora_radio_lock();
    chan_rx_only = false;   // manual tune: no longer on a marked RX-only scanlist channel
    int status = radio.setFrequency(freq);
    if (status == RADIOLIB_ERR_NONE)
        radio.startReceive();
    lora_radio_unlock();
    return status == RADIOLIB_ERR_NONE ? 0 : status;
}

int lora_set_tx_power(int power)
{
    lora_radio_lock();
    int status = radio.setOutputPower(power);
    if (status == RADIOLIB_ERR_NONE)
        radio.startReceive();
    lora_radio_unlock();
    return status == RADIOLIB_ERR_NONE ? 0 : status;
}

int lora_set_bandwidth(float bw)
{
    lora_radio_lock();
    int status = radio.setBandwidth(bw);
    if (status == RADIOLIB_ERR_NONE)
        radio.startReceive();
    lora_radio_unlock();
    return status == RADIOLIB_ERR_NONE ? 0 : status;
}

int lora_set_sf(int sf)
{
    lora_radio_lock();
    int status = radio.setSpreadingFactor(sf);
    if (status == RADIOLIB_ERR_NONE)
        radio.startReceive();
    lora_radio_unlock();
    return status == RADIOLIB_ERR_NONE ? 0 : status;
}

int lora_set_cr(int cr)
{
    lora_radio_lock();
    int status = radio.setCodingRate(cr);
    if (status == RADIOLIB_ERR_NONE)
        radio.startReceive();
    lora_radio_unlock();
    return status == RADIOLIB_ERR_NONE ? 0 : status;
}

int lora_reinit(void)
{
    fsk_mode = (strcasecmp(config.lora_rf_mode, "fsk") == 0);

    lora_radio_lock();          // recursive: scan_step() may already hold it

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

    if (status == RADIOLIB_ERR_NONE)
    {
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
        lora_rxdone_flag = false;   // drop the spurious RX-done a channel hop can latch
    }

    lora_radio_unlock();
    return status;
}
