#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "main.h"
#include "config.h"
#include "printManager.h"
#include "lora.h"
#include "lora_proto.h"
#include "scan.h"

// ===== Scanlist & channel lock (Phase 2/3) ===================================
// The cone scans channels to find the master's beacon, dwelling on each long
// enough to hear one. Channels come in TIERS, in priority order:
//   tier 0.. : /dist/scanlist.txt, /scanlist.txt (each if present & non-empty)
//   then     : the built-in default (a few most-likely channels)
//   last     : an EXHAUSTIVE virtual sweep of every LoRa freq x BW x SF in band
// The scan starts on tier 0 and, after scan_passes fruitless sweeps of the
// PRIMARY tier, widens by one tier -- up to all tiers. Active tiers are scanned
// ROUND-ROBIN (one entry per tier per step, each with its own cursor) so the
// small primary tier stays frequently scanned even while the huge exhaustive
// tier is also swept. On a beacon it LOCKS; after a long loss it re-scans.
//
// STREAMING: scanlist FILES are never cached in RAM. Each file tier reads the
// next entry OPEN-PER-STEP -- open, seek to a saved byte offset, read one entry,
// close -- so the file is never held open and can be deleted or edited at any
// time. Malformed lines are skipped, and a file with NO valid entries (empty,
// all-comment, or all-malformed) is skipped entirely -- the scan falls back to
// the next tier. While scanning we also watch the scanlist files: each step we
// stat the candidate paths and, if the set changed at all -- a file was EDITED
// (mtime/size), DELETED, newly APPEARED, or emptied -- we rebuild the tier list
// and restart scanning from tier 0.

#define SCAN_MAX_TIERS     5
#define SCAN_LOSS_MS       40000   // re-scan after this long locked with no beacon
#define LAST_CONN_EVERY    3       // probe the RAM last-connected channel 1 in N scan steps
// Dwell per channel = config.lora_scan_dwell (s); sweeps before widening =
// config.lora_scan_passes.

// scan_entry_t is declared in scan.h (shared with lora.cpp for the beacon-follow).

// Built-in default: the few most-likely channels (const -> lives in flash, never
// copied to RAM). Used even with no scanlist files present. Use the *_RX mode to
// mark a channel RECEIVE-ONLY (listen for the master, never transmit there -- e.g.
// GMRS / business-band frequencies outside the amateur allocation).
#define LORA     LP_MODE_LORA, false, false
#define LORA_RX  LP_MODE_LORA, true,  false
#define FSK      LP_MODE_FSK,  false, false
#define FSK_RX   LP_MODE_FSK,  true,  false
static const scan_entry_t DEFAULT_SCANLIST[] = {
    //  mode     freq_hz     bw_hz   sf cr  sync   br fd rxbw  fsk_sync
    { LORA,    431250000, 500000, 7, 5, 0xDEAD, 0, 0, 0, "" },
    { LORA,    431250000, 125000, 9, 5, 0x12,   0, 0, 0, "" },
    { LORA,    433000000, 500000, 7, 5, 0xDEAD, 0, 0, 0, "" },
};
#undef LORA
#undef LORA_RX
#undef FSK
#undef FSK_RX
#define DEFAULT_SCANLIST_N ((int)(sizeof(DEFAULT_SCANLIST) / sizeof(DEFAULT_SCANLIST[0])))

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

// Tier source kinds. SRC_FILE streams entries from LittleFS OPEN-PER-STEP (the
// file is never held open between steps, so it can be deleted or edited at any
// time); SRC_BUILTIN indexes the const array; SRC_EXHAUSTIVE is computed.
typedef enum { SRC_FILE, SRC_BUILTIN, SRC_EXHAUSTIVE } tier_src_t;

typedef struct {
    tier_src_t  src;
    const char *name;     // display name; also the logical path for SRC_FILE
    int         count;    // entries (SRC_FILE: counted at open, for display/skip)
    int         cursor;   // SRC_BUILTIN / SRC_EXHAUSTIVE position
    bool        primed;   // SRC_BUILTIN / SRC_EXHAUSTIVE: served at least one entry
    long        offset;   // SRC_FILE: byte offset of the next entry to read
} scan_tier_t;

// The scanlist files streamed as the highest-priority tiers, in priority order.
static const char *SCAN_FILES[] = { "/dist/scanlist.txt", "/scanlist.txt" };
#define SCAN_N_FILES ((int)(sizeof(SCAN_FILES) / sizeof(SCAN_FILES[0])))

// Per-candidate-file fingerprint, snapshotted when we (re)build the tier list.
// Comparing it each step detects edits/deletes/appearances.
typedef struct { bool present; time_t mtime; off_t size; } file_sig_t;
static file_sig_t scan_sig[SCAN_N_FILES];

static scan_tier_t tiers[SCAN_MAX_TIERS];
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
// Last-connected channel, remembered IN RAM only (never persisted -- a reboot
// reverts to the configured scanlist). On a rescan we interleave a probe of it
// with the tiered sweep, so a master we FOLLOWED to a non-scanlist channel (or one
// that briefly dropped) is re-found fast, alongside the normal scanlist.
static scan_entry_t last_conn;
static bool         have_last_conn = false;
static int          last_conn_ctr  = 0;       // step counter: probe when it hits 0 (1 in LAST_CONN_EVERY)

// Parse one scanlist line. Returns 1=entry, 0=blank/comment, -1=malformed.
static int scan_parse_line(const char *s, scan_entry_t *e)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#' || *s == '\0' || *s == '\r' || *s == '\n') return 0;
    char mode = *s++;
    bool rx_only = false, whitening = false;
    for (;;) {   // flag letters after the mode char: X = receive-only, W = FSK whitening
        if      (*s == 'X' || *s == 'x') { rx_only   = true; s++; }
        else if (*s == 'W' || *s == 'w') { whitening = true; s++; }
        else break;
    }
    memset(e, 0, sizeof(*e));
    if (mode == 'L' || mode == 'l') {
        unsigned long freq, bw; int sf, cr; char sync[16];
        if (sscanf(s, "%lu %lu %d %d %15s", &freq, &bw, &sf, &cr, sync) != 5) return -1;
        e->mode = LP_MODE_LORA; e->freq_hz = (uint32_t)freq; e->bw_hz = (uint32_t)bw;
        e->sf = (uint8_t)sf; e->cr = (uint8_t)cr;
        e->sync_word = (uint16_t)strtol(sync, NULL, 16);
        e->rx_only = rx_only;
        return 1;
    } else if (mode == 'F' || mode == 'f') {
        unsigned long freq, br, fd, rxbw; char sync[17];
        if (sscanf(s, "%lu %lu %lu %lu %16s", &freq, &br, &fd, &rxbw, sync) != 5) return -1;
        e->mode = LP_MODE_FSK; e->freq_hz = (uint32_t)freq; e->bitrate_bps = (uint32_t)br;
        e->freqdev_hz = (uint32_t)fd; e->rxbw_hz = (uint32_t)rxbw;
        strlcpy(e->fsk_sync, sync, sizeof(e->fsk_sync));
        e->rx_only = rx_only;
        e->whitening = whitening;
        return 1;
    }
    return -1;
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

// Read the next valid (L/F) entry from an open file, skipping comment/blank and
// MALFORMED lines (so a syntax error never aborts the scan -- we just continue).
// Returns true with *e filled, or false at EOF (caller decides whether to rewind).
static bool file_read_next(FILE *f, scan_entry_t *e)
{
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        if (scan_parse_line(line, e) == 1) return true;
    }
    return false;   // EOF
}

// Count the valid entries in an open file, leaving it rewound to the start.
static int file_count(FILE *f)
{
    int n = 0; scan_entry_t e;
    rewind(f);
    while (file_read_next(f, &e)) n++;
    rewind(f);
    return n;
}

// Snapshot one candidate file's existence/mtime/size.
static file_sig_t stat_sig(const char *logical)
{
    file_sig_t s = { false, 0, 0 };
    char path[80]; struct stat st;
    if (stat(lfs_path(path, sizeof(path), logical), &st) == 0) {
        s.present = true; s.mtime = st.st_mtime; s.size = st.st_size;
    }
    return s;
}

// True if any candidate scanlist file changed since scan_open() -- edited
// (mtime/size), deleted (present->absent), or newly appeared (absent->present).
static bool scanlist_changed(void)
{
    for (int i = 0; i < SCAN_N_FILES; i++) {
        file_sig_t now = stat_sig(SCAN_FILES[i]);
        if (now.present != scan_sig[i].present ||
            now.mtime   != scan_sig[i].mtime   ||
            now.size    != scan_sig[i].size)
            return true;
    }
    return false;
}

// Read the next entry from a file tier WITHOUT holding the file open between steps
// (so it can be deleted/edited at any time). Seeks to the saved byte offset, reads
// one entry, records the new offset; rewinds (and sets *wrapped) at EOF. Returns
// false if the file is gone or has no valid entries.
static bool file_tier_next(scan_tier_t *t, scan_entry_t *e, bool *wrapped)
{
    char path[80];
    FILE *f = fopen(lfs_path(path, sizeof(path), t->name), "r");
    if (!f) { memset(e, 0, sizeof(*e)); return false; }   // deleted out from under us
    if (t->offset > 0) fseek(f, t->offset, SEEK_SET);
    if (!file_read_next(f, e)) {            // at/past EOF -> rewind to the first entry
        rewind(f);
        if (!file_read_next(f, e)) { fclose(f); memset(e, 0, sizeof(*e)); return false; }
        *wrapped = true;
    }
    t->offset = ftell(f);                   // next step resumes here
    fclose(f);
    return true;
}

// Fetch the next entry from a tier. Sets *wrapped when the tier loops back to its
// first entry (drives tier-widening). Returns false only if the tier is empty.
static bool tier_next(scan_tier_t *t, scan_entry_t *e, bool *wrapped)
{
    *wrapped = false;

    if (t->src == SRC_FILE) return file_tier_next(t, e, wrapped);

    // SRC_BUILTIN / SRC_EXHAUSTIVE: index by cursor.
    int count = (t->src == SRC_EXHAUSTIVE) ? EX_COUNT : DEFAULT_SCANLIST_N;
    if (count <= 0) { memset(e, 0, sizeof(*e)); return false; }
    if (t->cursor == 0 && t->primed) *wrapped = true;  // first entry of a new sweep
    *e = (t->src == SRC_EXHAUSTIVE) ? exhaustive_entry(t->cursor)
                                    : DEFAULT_SCANLIST[t->cursor];
    t->primed = true;
    if (++t->cursor >= count) t->cursor = 0;
    return true;
}

// (Re)build the tier list for a fresh scan: include each scanlist file that exists
// and has >=1 valid entry (counted now, then closed -- read open-per-step while
// scanning), then the built-in and exhaustive virtual tiers. Snapshots every
// candidate file's signature so later appearances/deletions/edits are detected.
static void scan_open(void)
{
    n_tiers = 0;

    for (int i = 0; i < SCAN_N_FILES && n_tiers < SCAN_MAX_TIERS; i++) {
        char path[80];
        FILE *f = fopen(lfs_path(path, sizeof(path), SCAN_FILES[i]), "r");
        if (!f) continue;
        int c = file_count(f);                 // counts valid entries, rewinds
        fclose(f);                             // don't hold it open (open-per-step)
        if (c <= 0) {                          // empty / all-comment / all-malformed
            printfnl(SOURCE_LORA, "scan: %s has no valid entries, skipped\n", SCAN_FILES[i]);
            continue;                          // fall back to the next tier
        }
        scan_tier_t *t = &tiers[n_tiers++];
        memset(t, 0, sizeof(*t));              // offset = 0 -> start at first entry
        t->src = SRC_FILE; t->name = SCAN_FILES[i]; t->count = c;
    }
    if (n_tiers < SCAN_MAX_TIERS) {
        scan_tier_t *t = &tiers[n_tiers++];
        memset(t, 0, sizeof(*t));
        t->src = SRC_BUILTIN; t->name = "built-in"; t->count = DEFAULT_SCANLIST_N;
    }
    if (n_tiers < SCAN_MAX_TIERS) {
        scan_tier_t *t = &tiers[n_tiers++];
        memset(t, 0, sizeof(*t));
        t->src = SRC_EXHAUSTIVE; t->name = "exhaustive"; t->count = EX_COUNT;
    }

    for (int i = 0; i < SCAN_N_FILES; i++) scan_sig[i] = stat_sig(SCAN_FILES[i]);

    printfnl(SOURCE_LORA, "scanlist: %d tiers (files streamed, not cached):\n", n_tiers);
    for (int t = 0; t < n_tiers; t++)
        printfnl(SOURCE_LORA, "  tier %d: %s (%d entries)\n", t, tiers[t].name, tiers[t].count);
}

static void scan_describe(const scan_entry_t *e, char *buf, size_t n)
{
    const char *ro = e->rx_only ? " [RX-only]" : "";
    if (e->mode == LP_MODE_FSK)
        snprintf(buf, n, "FSK %.3f MHz %u bps dev%.1f bw%.1f sync 0x%X%s%s",
                 e->freq_hz / 1e6, (unsigned)e->bitrate_bps,
                 e->freqdev_hz / 1000.0, e->rxbw_hz / 1000.0,
                 (unsigned)strtol(e->fsk_sync, NULL, 16),
                 e->whitening ? " whiten" : "", ro);
    else
        snprintf(buf, n, "LoRa %.3f MHz BW%u SF%u sync 0x%04X%s",
                 e->freq_hz / 1e6, (unsigned)(e->bw_hz / 1000), (unsigned)e->sf,
                 (unsigned)e->sync_word, ro);
}

// Tune the radio to a scanlist entry. We BORROW config.* to feed lora_reinit(),
// then RESTORE it: the scanner must never persist a transient scanned channel into
// the user's saved config (a config_save() mid-scan would otherwise pin a random
// channel and could brick the next boot). The user's configured channel is the
// default the scan starts from; the live tuned channel is shown via the scan
// status / beacon, not the saved config.
static void scan_apply(const scan_entry_t *e)
{
    char  s_mode[sizeof(config.lora_rf_mode)];  strlcpy(s_mode, config.lora_rf_mode, sizeof(s_mode));
    char  s_fsync[sizeof(config.fsk_syncword)]; strlcpy(s_fsync, config.fsk_syncword, sizeof(s_fsync));
    float s_freq = config.lora_frequency, s_bw = config.lora_bandwidth;
    int   s_sf = config.lora_sf, s_cr = config.lora_cr, s_sync = config.lora_sync_word;
    float s_br = config.fsk_bitrate, s_fd = config.fsk_freqdev, s_rxbw = config.fsk_rxbw;
    bool  s_white = config.fsk_whitening;

    config.lora_frequency = e->freq_hz / 1e6f;
    if (e->mode == LP_MODE_FSK) {
        strlcpy(config.lora_rf_mode, "fsk", sizeof(config.lora_rf_mode));
        config.fsk_bitrate = e->bitrate_bps / 1000.0f;
        config.fsk_freqdev = e->freqdev_hz / 1000.0f;
        config.fsk_rxbw    = e->rxbw_hz / 1000.0f;
        strlcpy(config.fsk_syncword, e->fsk_sync, sizeof(config.fsk_syncword));
        config.fsk_whitening = e->whitening;
    } else {
        strlcpy(config.lora_rf_mode, "lora", sizeof(config.lora_rf_mode));
        config.lora_bandwidth = e->bw_hz / 1000.0f;
        config.lora_sf        = e->sf;
        config.lora_cr        = e->cr;
        config.lora_sync_word = e->sync_word;
    }
    lora_set_channel_rx_only(e->rx_only);   // gate TX on receive-only channels
    lora_reinit();

    // restore the user's configured channel
    config.lora_frequency = s_freq; config.lora_bandwidth = s_bw;
    config.lora_sf = s_sf; config.lora_cr = s_cr; config.lora_sync_word = s_sync;
    config.fsk_bitrate = s_br; config.fsk_freqdev = s_fd; config.fsk_rxbw = s_rxbw;
    config.fsk_whitening = s_white;
    strlcpy(config.lora_rf_mode, s_mode, sizeof(config.lora_rf_mode));
    strlcpy(config.fsk_syncword, s_fsync, sizeof(config.fsk_syncword));
}

static uint32_t scan_dwell_ms(void)
{
    int s = config.lora_scan_dwell;
    if (s < 1) s = 1;
    return (uint32_t)s * 1000;
}

// One round-robin step: scan tier rr_tier's next entry, then rotate rr_tier
// across the active tiers (0..active_tier). Widen by one tier after the primary
// is swept scan_passes times with no master found.
static void scan_step(void)
{
    if (n_tiers == 0) return;

    // Interleave a probe of the LAST-CONNECTED channel (RAM-only, this boot) with
    // the tiered sweep -- 1 step in every LAST_CONN_EVERY -- so a master we FOLLOWED
    // to a non-scanlist channel, or that briefly dropped, is re-found fast WITHOUT
    // monopolizing the scan. The tiered sweep proceeds and widens unchanged.
    if (have_last_conn) {
        if (last_conn_ctr == 0) {
            last_conn_ctr = LAST_CONN_EVERY - 1;   // then LAST_CONN_EVERY-1 tier steps
            cur_entry = last_conn;
            scan_apply(&cur_entry);
            scan_dwell_until = uptime_ms() + scan_dwell_ms();
            char d[96]; scan_describe(&cur_entry, d, sizeof(d));
            printfnl(SOURCE_LORA, "scan: last-conn %s\n", d);
            return;
        }
        last_conn_ctr--;
    }

    int t = rr_tier;
    bool wrapped = false;
    if (!tier_next(&tiers[t], &cur_entry, &wrapped)) {  // tier yielded nothing
        if (++rr_tier > active_tier) rr_tier = 0;
        return;
    }
    scan_apply(&cur_entry);
    scan_dwell_until = uptime_ms() + scan_dwell_ms();
    char d[96]; scan_describe(&cur_entry, d, sizeof(d));
    printfnl(SOURCE_LORA, "scan: tier %d/%d %s\n", t, active_tier, d);

    bool primary_wrapped = (t == 0 && wrapped);
    // rotate to the next active tier
    if (++rr_tier > active_tier) rr_tier = 0;
    // widen after the primary tier has been swept scan_passes times
    if (primary_wrapped) {
        scan_pass_count++;
        int passes = config.lora_scan_passes;
        if (passes < 1) passes = 1;
        if (scan_pass_count >= passes && active_tier < n_tiers - 1) {
            active_tier++;
            scan_pass_count = 0;
            printfnl(SOURCE_LORA, "scan: primary swept %dx, no master -> widen to tier %d/%d (+%s, %d entries)\n",
                     passes, active_tier, n_tiers - 1, tiers[active_tier].name, tiers[active_tier].count);
        }
    }
}

// (Re)start scanning from tier 0 -- reopens scanlist files (picking up any edits,
// deletions, appearances or a freshly dist-delivered scanlist) and resets the
// round-robin.
static void scan_restart(void)
{
    scan_state = SCAN_SCANNING;
    active_tier = 0;
    scan_pass_count = 0;
    rr_tier = 0;
    last_conn_ctr = 0;                 // probe the last-connected channel first on a rescan
    scan_open();
    if (n_tiers > 0) scan_step();
}

void scan_init(void)
{
    scan_restart();
    if (n_tiers > 0)
        printfnl(SOURCE_LORA, "scan: starting across %d tier(s)\n", n_tiers);
}

// True if two channels are the SAME downstream PHY a locked cone must track: freq
// + sync + the LoRa (bw/sf) or FSK (bitrate/freqdev/rxbw/whitening) params. Mirrors
// the master's _phy_signature -- excludes LoRa CR (RX auto-detects it from the
// header) and rx_only (a cone-side TX policy the beacon doesn't carry).
static bool scan_phy_eq(const scan_entry_t *a, const scan_entry_t *b)
{
    if (a->mode != b->mode || a->freq_hz != b->freq_hz) return false;
    if (a->mode == LP_MODE_FSK)
        return a->bitrate_bps == b->bitrate_bps && a->freqdev_hz == b->freqdev_hz &&
               a->rxbw_hz == b->rxbw_hz && a->whitening == b->whitening &&
               (uint16_t)strtol(a->fsk_sync, NULL, 16) == (uint16_t)strtol(b->fsk_sync, NULL, 16);
    return a->bw_hz == b->bw_hz && a->sf == b->sf && a->sync_word == b->sync_word;
}

// Light sanity gate on a beacon-advertised channel before we retune to it: a
// corrupt-but-CRC-valid (or future-buggy-master) beacon must not strand us on a
// dead channel. Roughly the 70cm band + plausible modem params.
static bool scan_phy_sane(const scan_entry_t *e)
{
    if (e->freq_hz < 100000000u || e->freq_hz > 1000000000u) return false;
    if (e->mode == LP_MODE_FSK)
        return e->bitrate_bps >= 100 && e->rxbw_hz >= 1000;
    return e->sf >= 5 && e->sf <= 12 && e->bw_hz >= 1000;
}

// Remember the channel we're currently locked on, in RAM (see last_conn). Cleared
// only by a reboot, so a power-cycle reverts to the persisted scanlist.
static void remember_last_conn(void) { last_conn = cur_entry; have_last_conn = true; }

void scan_notify_beacon(const scan_entry_t *advertised)
{
    lora_radio_lock();

    // FOLLOW: the master announces an imminent PHY change by beaconing the NEW
    // params on the OLD (current) PHY several times before it retunes. We can only
    // RECEIVE a beacon on the PHY we're tuned to, so a beacon whose advertised PHY
    // differs from cur_entry means the master is about to move -- retune to it now
    // and stay locked, instead of waiting ~40s to lose the beacon and rescan. Gated
    // on scan_enabled so a manual `lora scan off` pin is never overridden.
    if (scan_enabled && advertised && scan_phy_sane(advertised) &&
        !scan_phy_eq(advertised, &cur_entry)) {
        scan_entry_t next = *advertised;
        next.rx_only = cur_entry.rx_only;     // inherit TX policy (beacon can't carry it)
        char from[96], to[96];
        scan_describe(&cur_entry, from, sizeof(from));
        scan_describe(&next,      to,   sizeof(to));
        scan_apply(&next);
        cur_entry           = next;
        scan_state          = SCAN_LOCKED;
        scan_last_beacon_ms = uptime_ms();
        scan_have_beacon    = true;
        remember_last_conn();                 // followed channel becomes the last-connected
        printfnl(SOURCE_LORA, "scan: following master %s -> %s\n", from, to);
        lora_radio_unlock();
        return;
    }

    scan_last_beacon_ms = uptime_ms();
    scan_have_beacon    = true;
    remember_last_conn();                     // current channel is good -> remember it
    if (scan_state == SCAN_SCANNING) {
        scan_state = SCAN_LOCKED;
        // Report the channel we actually locked on (cur_entry). NOT
        // config.lora_frequency -- scan_apply() restores config.* after tuning,
        // so it holds the default, not the locked entry.
        char d[96]; scan_describe(&cur_entry, d, sizeof(d));
        printfnl(SOURCE_LORA, "scan: LOCKED %s (was scanning tier %d/%d)\n",
                 d, active_tier, n_tiers - 1);
    }
    lora_radio_unlock();
}

void lora_scan_tick(void)   // called from the LoRa task after lora_rx()
{
    if (!scan_enabled || n_tiers == 0) return;   // cheap reads; lock only to act
    uint32_t now = uptime_ms();

    if (scan_state == SCAN_SCANNING) {
        if ((int32_t)(now - scan_dwell_until) >= 0) {
            lora_radio_lock();
            if (scanlist_changed()) {   // edited / deleted / appeared -> rebuild
                printfnl(SOURCE_LORA, "scan: scanlist files changed, rescanning from tier 0\n");
                scan_restart();
            } else {
                scan_step();
            }
            lora_radio_unlock();
        }
    } else { // SCAN_LOCKED
        if (scan_have_beacon && (now - scan_last_beacon_ms) >= SCAN_LOSS_MS) {
            lora_radio_lock();
            printfnl(SOURCE_LORA, "scan: beacon lost (%us), re-scanning from tier 0\n",
                     (unsigned)((now - scan_last_beacon_ms) / 1000));
            scan_restart();
            lora_radio_unlock();
        }
    }
}

void lora_scan_set_enabled(bool en)   // `lora scan on|off`
{
    lora_radio_lock();
    scan_enabled = en;
    if (en) {
        scan_restart();
        printfnl(SOURCE_COMMANDS, "scan: enabled (from tier 0)\n");
    } else {
        printfnl(SOURCE_COMMANDS, "scan: disabled (staying on current channel)\n");
    }
    lora_radio_unlock();
}

bool lora_scan_is_enabled(void) { return scan_enabled; }

void lora_scan_print(void)   // shown in `lora` status
{
    lora_radio_lock();        // consistent snapshot vs the LoRa task's scan_step()
    int passes = config.lora_scan_passes; if (passes < 1) passes = 1;
    printfnl(SOURCE_COMMANDS, "  Scan: %s  (%d tiers, active 0..%d, %ds dwell, widen @ %d sweeps)\n",
             !scan_enabled ? "off" : (scan_state == SCAN_LOCKED ? "LOCKED" : "scanning"),
             n_tiers, active_tier, config.lora_scan_dwell, passes);
    for (int t = 0; t < n_tiers; t++)
        printfnl(SOURCE_COMMANDS, "    tier %d %-18s %5d entries%s\n",
                 t, tiers[t].name, tiers[t].count, t <= active_tier ? "  [active]" : "");
    if (scan_enabled) {
        char d[96]; scan_describe(&cur_entry, d, sizeof(d));
        printfnl(SOURCE_COMMANDS, "    primary sweep %d/%d, last: %s\n", scan_pass_count, passes, d);
        if (have_last_conn) {
            char l[96]; scan_describe(&last_conn, l, sizeof(l));
            printfnl(SOURCE_COMMANDS, "    last-conn (RAM, probed 1-in-%d): %s\n", LAST_CONN_EVERY, l);
        }
    }
    lora_radio_unlock();
}
