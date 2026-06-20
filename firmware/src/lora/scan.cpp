#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "config.h"
#include "printManager.h"
#include "lora.h"
#include "lora_proto.h"
#include "scan.h"

// ===== Scanlist & channel lock (Phase 2/3) ===================================
// The cone scans channels to find the master's beacon, dwelling on each long
// enough to hear one. Channels come in TIERS, in priority order:
//   tier 0.. : /dist/scanlist.txt, /scanlist.txt (each if present)
//   then     : the built-in default (a few most-likely channels)
//   last     : an EXHAUSTIVE virtual sweep of every LoRa freq x BW x SF in band
// The scan starts on tier 0 and, after scan_passes fruitless sweeps of the
// PRIMARY tier, widens by one tier -- up to all tiers. Active tiers are scanned
// ROUND-ROBIN (one entry per tier per step), each with its own cursor, so the
// small primary tier stays frequently scanned even while the huge exhaustive
// tier is also swept. On a beacon it LOCKS; after a long loss it re-scans.

#define SCAN_MAX_ENTRIES   48      // concrete entries (files + built-in default)
#define SCAN_MAX_TIERS     5
#define SCAN_LOSS_MS       40000   // re-scan after this long locked with no beacon
// Dwell per channel = config.lora_scan_dwell (s); sweeps before widening =
// config.lora_scan_passes.

typedef struct {
    uint8_t  mode;                 // LP_MODE_LORA / LP_MODE_FSK
    uint32_t freq_hz;
    uint32_t bw_hz;                // LoRa bandwidth
    uint8_t  sf, cr;               // LoRa
    uint16_t sync_word;            // LoRa 16-bit sync
    uint32_t bitrate_bps, freqdev_hz, rxbw_hz;  // FSK
    char     fsk_sync[17];         // FSK sync word as hex string
} scan_entry_t;

// Built-in default: the few most-likely channels (used even with no files).
static const scan_entry_t DEFAULT_SCANLIST[] = {
    { LP_MODE_LORA, 431250000, 500000, 7, 5, 0xDEAD, 0, 0, 0, "" },
    { LP_MODE_LORA, 431250000, 125000, 9, 5, 0x12,   0, 0, 0, "" },
    { LP_MODE_LORA, 433000000, 500000, 7, 5, 0xDEAD, 0, 0, 0, "" },
};

// Exhaustive virtual tier: every LoRa freq x BW x SF across the sub-band,
// computed on demand (too large to store). Sync word is unknowable here so it
// uses the default; file/built-in tiers carry the real sync for odd channels.
#define EX_FREQ_START  430000000u
#define EX_FREQ_STEP      250000u      // 0.25 MHz
#define EX_FREQ_COUNT  17              // 430.00 .. 434.00 MHz
static const uint32_t EX_BW[] = { 500000, 250000, 125000, 62500 };
static const uint8_t  EX_SF[] = { 7, 8, 9, 10, 11, 12 };
#define EX_N_BW   ((int)(sizeof(EX_BW) / sizeof(EX_BW[0])))
#define EX_N_SF   ((int)(sizeof(EX_SF) / sizeof(EX_SF[0])))
#define EX_COUNT  (EX_FREQ_COUNT * EX_N_BW * EX_N_SF)

typedef struct {
    bool exhaustive;   // true = computed sweep; false = scanlist[] slice
    int  base;         // offset into scanlist[] (concrete tiers)
    int  count;        // entries in this tier
    const char *src;
} scan_tier_t;

static scan_entry_t scanlist[SCAN_MAX_ENTRIES];
static int  scanlist_n = 0;
static scan_tier_t tiers[SCAN_MAX_TIERS];
static int  tier_cursor[SCAN_MAX_TIERS] = {0};   // per-tier cursor (0..count-1)
static int  n_tiers     = 0;
static int  active_tier = 0;          // widest active tier
static int  rr_tier     = 0;          // round-robin selector over active tiers
static int  scan_pass_count = 0;      // completed sweeps of the primary tier
static scan_entry_t cur_entry;        // currently-tuned channel (for status)
static bool scan_enabled = true;
static enum { SCAN_LOCKED, SCAN_SCANNING } scan_state = SCAN_SCANNING;
static uint32_t scan_dwell_until = 0;
static uint32_t scan_last_beacon_ms = 0;   // uptime at the last beacon heard
static bool     scan_have_beacon    = false;

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

// Append entries from a LittleFS scanlist file; returns the running count.
static int scan_load_file(const char *logical)
{
    char path[80];
    FILE *f = fopen(lfs_path(path, sizeof(path), logical), "r");
    if (!f) return scanlist_n;
    char line[160];
    while (scanlist_n < SCAN_MAX_ENTRIES && fgets(line, sizeof(line), f)) {
        scan_entry_t e;
        if (scan_parse_line(line, &e) == 1) scanlist[scanlist_n++] = e;
    }
    fclose(f);
    return scanlist_n;
}

// Compute the exhaustive virtual entry for index c (sf fastest, then bw, freq).
static scan_entry_t exhaustive_entry(int c)
{
    scan_entry_t e; memset(&e, 0, sizeof(e));
    int sf_i = c % EX_N_SF; c /= EX_N_SF;
    int bw_i = c % EX_N_BW; c /= EX_N_BW;
    int fr_i = c % EX_FREQ_COUNT;
    e.mode      = LP_MODE_LORA;
    e.freq_hz   = EX_FREQ_START + (uint32_t)fr_i * EX_FREQ_STEP;
    e.bw_hz     = EX_BW[bw_i];
    e.sf        = EX_SF[sf_i];
    e.cr        = 5;
    e.sync_word = DEFAULT_LORA_SYNC_WORD;
    return e;
}

// The scan_entry for (tier, cursor) -- a scanlist[] slice, or a computed sweep.
static scan_entry_t tier_entry(int t, int cursor)
{
    if (tiers[t].exhaustive) return exhaustive_entry(cursor);
    return scanlist[tiers[t].base + cursor];
}

static void add_tier(bool ex, int base, int count, const char *src)
{
    if (n_tiers >= SCAN_MAX_TIERS || count <= 0) return;
    tiers[n_tiers].exhaustive = ex;
    tiers[n_tiers].base       = base;
    tiers[n_tiers].count      = count;
    tiers[n_tiers].src        = src;
    n_tiers++;
}

static void scan_load(void)
{
    scanlist_n = 0;
    n_tiers = 0;
    // Concrete tiers in priority order: dist-delivered, root override, built-in.
    const char *files[] = { "/dist/scanlist.txt", "/scanlist.txt" };
    for (int s = 0; s < 2; s++) {
        int before = scanlist_n;
        scan_load_file(files[s]);
        add_tier(false, before, scanlist_n - before, files[s]);
    }
    int before = scanlist_n;
    int n = (int)(sizeof(DEFAULT_SCANLIST) / sizeof(DEFAULT_SCANLIST[0]));
    for (int i = 0; i < n && scanlist_n < SCAN_MAX_ENTRIES; i++) scanlist[scanlist_n++] = DEFAULT_SCANLIST[i];
    add_tier(false, before, scanlist_n - before, "built-in");
    // Always-present last resort: the exhaustive virtual sweep.
    add_tier(true, 0, EX_COUNT, "exhaustive");

    printfnl(SOURCE_LORA, "scanlist: %d concrete entries, %d tiers:\n", scanlist_n, n_tiers);
    for (int t = 0; t < n_tiers; t++)
        printfnl(SOURCE_LORA, "  tier %d: %s (%d entries)\n", t, tiers[t].src, tiers[t].count);
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

static uint32_t scan_dwell_ms(void)
{
    int s = config.lora_scan_dwell;
    if (s < 1) s = 1;
    return (uint32_t)s * 1000;
}

// One round-robin step: scan tier rr_tier's current entry, advance that tier's
// cursor, then rotate rr_tier across the active tiers (0..active_tier). Widen by
// one tier after the primary is swept scan_passes times with no master found.
static void scan_step(void)
{
    if (n_tiers == 0) return;
    int t = rr_tier;                              // round-robin: this step's tier
    cur_entry = tier_entry(t, tier_cursor[t]);
    scan_apply(&cur_entry);
    scan_dwell_until = uptime_ms() + scan_dwell_ms();
    char d[72]; scan_describe(&cur_entry, d, sizeof(d));
    printfnl(SOURCE_LORA, "scan: tier %d/%d entry %d %s\n", t, active_tier, tier_cursor[t], d);

    // advance this tier's cursor (wraps within its own tier)
    bool primary_wrapped = false;
    if (++tier_cursor[t] >= tiers[t].count) {
        tier_cursor[t] = 0;
        if (t == 0) primary_wrapped = true;
    }
    // rotate to the next active tier
    if (++rr_tier > active_tier) rr_tier = 0;
    // widen after the primary tier has been swept scan_passes times
    if (primary_wrapped) {
        scan_pass_count++;
        int passes = config.lora_scan_passes;
        if (passes < 1) passes = 1;
        if (scan_pass_count >= passes && active_tier < n_tiers - 1) {
            active_tier++;
            tier_cursor[active_tier] = 0;
            scan_pass_count = 0;
            printfnl(SOURCE_LORA, "scan: primary swept %dx, no master -> widen to tier %d/%d (+%s, %d entries)\n",
                     passes, active_tier, n_tiers - 1, tiers[active_tier].src, tiers[active_tier].count);
        }
    }
}

// (Re)start scanning from tier 0, all cursors reset.
static void scan_restart(void)
{
    scan_state = SCAN_SCANNING;
    active_tier = 0;
    scan_pass_count = 0;
    rr_tier = 0;
    for (int t = 0; t < n_tiers; t++) tier_cursor[t] = 0;
    if (n_tiers > 0) scan_step();
}

void scan_init(void)
{
    scan_load();
    if (n_tiers > 0) {
        printfnl(SOURCE_LORA, "scan: starting across %d tier(s)\n", n_tiers);
        scan_restart();
    }
}

void scan_notify_beacon(void)
{
    scan_last_beacon_ms = uptime_ms();
    scan_have_beacon    = true;
    if (scan_state == SCAN_SCANNING) {
        scan_state = SCAN_LOCKED;
        printfnl(SOURCE_LORA, "scan: LOCKED %.3f MHz (was scanning tier %d/%d)\n",
                 config.lora_frequency, active_tier, n_tiers - 1);
    }
}

void lora_scan_tick(void)   // called from loop() after lora_rx()
{
    if (!scan_enabled || n_tiers == 0 || lora_tx_busy()) return;
    uint32_t now = uptime_ms();

    if (scan_state == SCAN_SCANNING) {
        if ((int32_t)(now - scan_dwell_until) >= 0)
            scan_step();
    } else { // SCAN_LOCKED
        if (scan_have_beacon && (now - scan_last_beacon_ms) >= SCAN_LOSS_MS) {
            printfnl(SOURCE_LORA, "scan: beacon lost (%us), re-scanning from tier 0\n",
                     (unsigned)((now - scan_last_beacon_ms) / 1000));
            scan_restart();
        }
    }
}

void lora_scan_set_enabled(bool en)   // `lora scan on|off`
{
    scan_enabled = en;
    if (en) {
        scan_restart();
        printfnl(SOURCE_COMMANDS, "scan: enabled (from tier 0)\n");
    } else {
        printfnl(SOURCE_COMMANDS, "scan: disabled (staying on current channel)\n");
    }
}

void lora_scan_print(void)   // shown in `lora` status
{
    int passes = config.lora_scan_passes; if (passes < 1) passes = 1;
    printfnl(SOURCE_COMMANDS, "  Scan: %s  (%d tiers, active 0..%d, %ds dwell, widen @ %d sweeps)\n",
             !scan_enabled ? "off" : (scan_state == SCAN_LOCKED ? "LOCKED" : "scanning"),
             n_tiers, active_tier, config.lora_scan_dwell, passes);
    for (int t = 0; t < n_tiers; t++)
        printfnl(SOURCE_COMMANDS, "    tier %d %-18s %5d entries%s\n",
                 t, tiers[t].src, tiers[t].count, t <= active_tier ? "  [active]" : "");
    if (scan_enabled) {
        char d[72]; scan_describe(&cur_entry, d, sizeof(d));
        printfnl(SOURCE_COMMANDS, "    primary sweep %d/%d, last: %s\n", scan_pass_count, passes, d);
    }
}
