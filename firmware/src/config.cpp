#include <stdint.h>
#include <string.h>
#include <esp_http_server.h>
#include <unistd.h>
#include "config.h"
#include "main.h"
#include "printManager.h"
#include "conez_usb.h"

// ---------- Global config instance ----------
conez_config_t config;

// ---------- Descriptor table ----------
typedef enum { CFG_STR, CFG_FLOAT, CFG_INT, CFG_HEX, CFG_BOOL } cfg_type_t;

typedef struct {
    const char *section;
    const char *key;
    cfg_type_t  type;
    size_t      offset;
    size_t      size;       // for CFG_STR: max buffer length
} cfg_descriptor_t;

#define CFG_ENTRY(sec, k, typ, field) \
    { sec, k, typ, offsetof(conez_config_t, field), sizeof(((conez_config_t*)0)->field) }

static const cfg_descriptor_t cfg_table[] = {
    // [wifi]
    CFG_ENTRY("wifi",   "enabled",      CFG_BOOL,  wifi_enabled),
    CFG_ENTRY("wifi",   "ssid",         CFG_STR,   wifi_ssid),
    CFG_ENTRY("wifi",   "password",     CFG_STR,   wifi_password),
    // [gps]
    CFG_ENTRY("gps",    "origin_lat",   CFG_FLOAT, origin_lat),
    CFG_ENTRY("gps",    "origin_lon",   CFG_FLOAT, origin_lon),
    // [lora]
    CFG_ENTRY("lora",   "ssid",         CFG_STR,   lora_ssid),
    CFG_ENTRY("lora",   "frequency",    CFG_FLOAT, lora_frequency),
    CFG_ENTRY("lora",   "bandwidth",    CFG_FLOAT, lora_bandwidth),
    CFG_ENTRY("lora",   "sf",           CFG_INT,   lora_sf),
    CFG_ENTRY("lora",   "cr",           CFG_INT,   lora_cr),
    CFG_ENTRY("lora",   "preamble",     CFG_INT,   lora_preamble),
    CFG_ENTRY("lora",   "tx_power",     CFG_INT,   lora_tx_power),
    CFG_ENTRY("lora",   "sync_word",    CFG_HEX,   lora_sync_word),
    CFG_ENTRY("lora",   "callsign",     CFG_STR,   lora_callsign),
    CFG_ENTRY("lora",   "rf_mode",      CFG_STR,   lora_rf_mode),
    CFG_ENTRY("lora",   "fsk_bitrate",  CFG_FLOAT, fsk_bitrate),
    CFG_ENTRY("lora",   "fsk_freqdev",  CFG_FLOAT, fsk_freqdev),
    CFG_ENTRY("lora",   "fsk_rxbw",     CFG_FLOAT, fsk_rxbw),
    CFG_ENTRY("lora",   "fsk_shaping",  CFG_INT,   fsk_shaping),
    CFG_ENTRY("lora",   "fsk_whitening",CFG_BOOL,  fsk_whitening),
    CFG_ENTRY("lora",   "fsk_syncword", CFG_STR,   fsk_syncword),
    CFG_ENTRY("lora",   "fsk_crc",      CFG_INT,   fsk_crc),
    // [system]
    CFG_ENTRY("system", "device_name",  CFG_STR,   device_name),
    CFG_ENTRY("system", "startup_script", CFG_STR, startup_script),
    CFG_ENTRY("system", "timezone",     CFG_INT,   timezone),
    CFG_ENTRY("system", "auto_dst",    CFG_BOOL,  auto_dst),
    CFG_ENTRY("system", "cone_id",    CFG_INT,   cone_id),
    CFG_ENTRY("system", "cone_group", CFG_INT,   cone_group),
    CFG_ENTRY("system", "ntp_server",   CFG_STR,   ntp_server),
    CFG_ENTRY("system", "ntp_interval", CFG_INT,   ntp_interval),
    CFG_ENTRY("system", "cpu_max",      CFG_INT,   cpu_max),
    CFG_ENTRY("system", "cpu_min",      CFG_INT,   cpu_min),
    // [mqtt]
    CFG_ENTRY("mqtt",   "broker",       CFG_STR,   mqtt_broker),
    CFG_ENTRY("mqtt",   "enabled",      CFG_BOOL,  mqtt_enabled),
    CFG_ENTRY("mqtt",   "port",         CFG_INT,   mqtt_port),
    // [led]
    CFG_ENTRY("led",    "count1",       CFG_INT,   led_count1),
    CFG_ENTRY("led",    "count2",       CFG_INT,   led_count2),
    CFG_ENTRY("led",    "count3",       CFG_INT,   led_count3),
    CFG_ENTRY("led",    "count4",       CFG_INT,   led_count4),
    CFG_ENTRY("led",    "color1",       CFG_HEX,   led_color1),
    CFG_ENTRY("led",    "color2",       CFG_HEX,   led_color2),
    CFG_ENTRY("led",    "color3",       CFG_HEX,   led_color3),
    CFG_ENTRY("led",    "color4",       CFG_HEX,   led_color4),
    // [debug]
    CFG_ENTRY("debug",  "system",       CFG_BOOL,  dbg_system),
    CFG_ENTRY("debug",  "basic",        CFG_BOOL,  dbg_basic),
    CFG_ENTRY("debug",  "wasm",         CFG_BOOL,  dbg_wasm),
    CFG_ENTRY("debug",  "commands",     CFG_BOOL,  dbg_commands),
    CFG_ENTRY("debug",  "gps",          CFG_BOOL,  dbg_gps),
    CFG_ENTRY("debug",  "gps_raw",      CFG_BOOL,  dbg_gps_raw),
    CFG_ENTRY("debug",  "lora",         CFG_BOOL,  dbg_lora),
    CFG_ENTRY("debug",  "lora_raw",     CFG_BOOL,  dbg_lora_raw),
    CFG_ENTRY("debug",  "fsync",        CFG_BOOL,  dbg_fsync),
    CFG_ENTRY("debug",  "wifi",         CFG_BOOL,  dbg_wifi),
    CFG_ENTRY("debug",  "sensors",      CFG_BOOL,  dbg_sensors),
    CFG_ENTRY("debug",  "mqtt",         CFG_BOOL,  dbg_mqtt),
    CFG_ENTRY("debug",  "other",        CFG_BOOL,  dbg_other),
};

static const int CFG_TABLE_SIZE = sizeof(cfg_table) / sizeof(cfg_table[0]);

static const char *CONFIG_PATH = "/config.ini";

// ---------- Tab completion key list ----------
// Returns NULL-terminated array of "section.key" strings for tab completion.
// Uses a static buffer — valid until the next call.
#define CFG_KEY_NAME_MAX 32
static char         cfg_key_buf[64][CFG_KEY_NAME_MAX];
static const char  *cfg_key_ptrs[65];   // +1 for NULL terminator
static bool         cfg_keys_built = false;

const char * const * config_get_key_list(void)
{
    if (!cfg_keys_built) {
        int n = CFG_TABLE_SIZE < 64 ? CFG_TABLE_SIZE : 64;
        for (int i = 0; i < n; i++) {
            snprintf(cfg_key_buf[i], CFG_KEY_NAME_MAX, "%s.%s",
                     cfg_table[i].section, cfg_table[i].key);
            cfg_key_ptrs[i] = cfg_key_buf[i];
        }
        cfg_key_ptrs[n] = NULL;
        cfg_keys_built = true;
    }
    return cfg_key_ptrs;
}

// ---------- Tab completion section list ----------
// Returns NULL-terminated array of unique "section." strings (with trailing dot).
static char         cfg_sec_buf[16][CFG_KEY_NAME_MAX];
static const char  *cfg_sec_ptrs[17];
static bool         cfg_secs_built = false;

const char * const * config_get_section_list(void)
{
    if (!cfg_secs_built) {
        int n = 0;
        for (int i = 0; i < CFG_TABLE_SIZE && n < 16; i++) {
            // Check if section already added
            bool dup = false;
            for (int j = 0; j < n; j++) {
                // Compare without trailing dot
                int slen = strlen(cfg_table[i].section);
                if (strncmp(cfg_sec_buf[j], cfg_table[i].section, slen) == 0
                    && cfg_sec_buf[j][slen] == '.')
                    { dup = true; break; }
            }
            if (!dup) {
                snprintf(cfg_sec_buf[n], CFG_KEY_NAME_MAX, "%s.", cfg_table[i].section);
                cfg_sec_ptrs[n] = cfg_sec_buf[n];
                n++;
            }
        }
        cfg_sec_ptrs[n] = NULL;
        cfg_secs_built = true;
    }
    return cfg_sec_ptrs;
}

// ---------- Key type lookup ----------
// Returns cfg_type_t for a "section.key" string, or -1 if not found.
int config_get_key_type(const char *dotkey)
{
    for (int i = 0; i < CFG_TABLE_SIZE; i++) {
        int slen = strlen(cfg_table[i].section);
        if (strncmp(dotkey, cfg_table[i].section, slen) == 0 &&
            dotkey[slen] == '.' &&
            strcasecmp(dotkey + slen + 1, cfg_table[i].key) == 0)
            return (int)cfg_table[i].type;
    }
    return -1;
}

// ---------- Helpers ----------
static void config_fill_defaults(conez_config_t *cfg)
{
    cfg->wifi_enabled     = DEFAULT_WIFI_ENABLED;
    strlcpy(cfg->wifi_ssid,       DEFAULT_WIFI_SSID,      sizeof(cfg->wifi_ssid));
    strlcpy(cfg->wifi_password,   DEFAULT_WIFI_PASSWORD,  sizeof(cfg->wifi_password));

    cfg->origin_lat       = DEFAULT_ORIGIN_LAT;
    cfg->origin_lon       = DEFAULT_ORIGIN_LON;

    cfg->lora_frequency   = DEFAULT_LORA_FREQUENCY;
    cfg->lora_bandwidth   = DEFAULT_LORA_BANDWIDTH;
    cfg->lora_sf          = DEFAULT_LORA_SF;
    cfg->lora_cr          = DEFAULT_LORA_CR;
    cfg->lora_preamble    = DEFAULT_LORA_PREAMBLE;
    cfg->lora_tx_power    = DEFAULT_LORA_TX_POWER;
    cfg->lora_sync_word   = DEFAULT_LORA_SYNC_WORD;
    strlcpy(cfg->lora_ssid,       DEFAULT_LORA_SSID,      sizeof(cfg->lora_ssid));
    strlcpy(cfg->lora_callsign,   DEFAULT_LORA_CALLSIGN,  sizeof(cfg->lora_callsign));
    strlcpy(cfg->lora_rf_mode,    DEFAULT_LORA_MODE,      sizeof(cfg->lora_rf_mode));
    cfg->fsk_bitrate      = DEFAULT_FSK_BITRATE;
    cfg->fsk_freqdev      = DEFAULT_FSK_FREQDEV;
    cfg->fsk_rxbw         = DEFAULT_FSK_RXBW;
    cfg->fsk_shaping      = DEFAULT_FSK_SHAPING;
    cfg->fsk_whitening    = DEFAULT_FSK_WHITENING;
    strlcpy(cfg->fsk_syncword,    DEFAULT_FSK_SYNCWORD,   sizeof(cfg->fsk_syncword));
    cfg->fsk_crc          = DEFAULT_FSK_CRC;

    strlcpy(cfg->device_name,     DEFAULT_DEVICE_NAME,    sizeof(cfg->device_name));
    strlcpy(cfg->startup_script,  DEFAULT_STARTUP_SCRIPT, sizeof(cfg->startup_script));
    cfg->timezone         = DEFAULT_TIMEZONE;
    cfg->auto_dst         = DEFAULT_AUTO_DST;
    cfg->cone_id          = DEFAULT_CONE_ID;
    cfg->cone_group       = DEFAULT_CONE_GROUP;
    strlcpy(cfg->ntp_server,      DEFAULT_NTP_SERVER,     sizeof(cfg->ntp_server));
    cfg->ntp_interval     = DEFAULT_NTP_INTERVAL;
    cfg->cpu_max          = DEFAULT_CPU_MAX;
    cfg->cpu_min          = DEFAULT_CPU_MIN;

    strlcpy(cfg->mqtt_broker,     DEFAULT_MQTT_BROKER,    sizeof(cfg->mqtt_broker));
    cfg->mqtt_enabled     = DEFAULT_MQTT_ENABLED;
    cfg->mqtt_port        = DEFAULT_MQTT_PORT;

    cfg->led_count1       = DEFAULT_LED_COUNT;
    cfg->led_count2       = DEFAULT_LED_COUNT;
    cfg->led_count3       = DEFAULT_LED_COUNT;
    cfg->led_count4       = DEFAULT_LED_COUNT;
    cfg->led_color1       = DEFAULT_LED_COLOR;
    cfg->led_color2       = DEFAULT_LED_COLOR;
    cfg->led_color3       = DEFAULT_LED_COLOR;
    cfg->led_color4       = DEFAULT_LED_COLOR;

    cfg->dbg_system       = DEFAULT_DBG_SYSTEM;
    cfg->dbg_basic        = DEFAULT_DBG_BASIC;
    cfg->dbg_wasm         = DEFAULT_DBG_WASM;
    cfg->dbg_commands     = DEFAULT_DBG_COMMANDS;
    cfg->dbg_gps          = DEFAULT_DBG_GPS;
    cfg->dbg_gps_raw      = DEFAULT_DBG_GPS_RAW;
    cfg->dbg_lora         = DEFAULT_DBG_LORA;
    cfg->dbg_lora_raw     = DEFAULT_DBG_LORA_RAW;
    cfg->dbg_fsync        = DEFAULT_DBG_FSYNC;
    cfg->dbg_wifi         = DEFAULT_DBG_WIFI;
    cfg->dbg_sensors      = DEFAULT_DBG_SENSORS;
    cfg->dbg_mqtt         = DEFAULT_DBG_MQTT;
    cfg->dbg_other        = DEFAULT_DBG_OTHER;
}


// Find a descriptor by section + key (case-insensitive).  Returns NULL if not found.
static const cfg_descriptor_t *config_find(const char *section, const char *key)
{
    for (int i = 0; i < CFG_TABLE_SIZE; i++)
    {
        if (strcasecmp(cfg_table[i].section, section) == 0 &&
            strcasecmp(cfg_table[i].key, key) == 0)
            return &cfg_table[i];
    }
    return NULL;
}


// Write a value string into the config struct via a descriptor.
static void config_set_field(const cfg_descriptor_t *d, const char *value)
{
    uint8_t *base = (uint8_t *)&config;

    switch (d->type)
    {
    case CFG_STR:
        strlcpy((char *)(base + d->offset), value, d->size);
        break;
    case CFG_FLOAT:
        *(float *)(base + d->offset) = atof(value);
        break;
    case CFG_INT:
    case CFG_HEX:
        *(int *)(base + d->offset) = (int)strtol(value, NULL, 0);
        break;
    case CFG_BOOL:
        *(bool *)(base + d->offset) = (strcasecmp(value, "on") == 0 ||
                                        strcasecmp(value, "true") == 0 ||
                                        strcasecmp(value, "1") == 0);
        break;
    }
}


// Reset a single field to its compiled default.
static void config_set_default_field(const cfg_descriptor_t *d)
{
    conez_config_t defaults;
    config_fill_defaults(&defaults);
    memcpy((uint8_t *)&config + d->offset,
           (uint8_t *)&defaults + d->offset,
           d->size);
}


// Strip leading and trailing whitespace in place, return new start pointer.
static char *str_trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t')) *end-- = '\0';
    return s;
}

// ---------- INI parser ----------
static void config_parse_ini(void)
{
    char fpath[64];
    lfs_path(fpath, sizeof(fpath), CONFIG_PATH);
    FILE *f = fopen(fpath, "r");
    if (!f)
        return;

    usb_printf("Loading /config.ini...\n");

    char line[128];
    char section[16] = "";

    while (fgets(line, sizeof(line), f))
    {
        int len = strlen(line);

        // Strip trailing newline/CR
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        // Skip blank lines and comments
        if (len == 0 || line[0] == '#' || line[0] == ';')
            continue;

        // Section header?
        if (line[0] == '[')
        {
            char *end = strchr(line, ']');
            if (end)
            {
                *end = '\0';
                strlcpy(section, line + 1, sizeof(section));
            }
            continue;
        }

        // key=value
        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        const char *key   = str_trim(line);
        const char *value = str_trim(eq + 1);

        const cfg_descriptor_t *d = config_find(section, key);
        if (d)
        {
            config_set_field(d, value);
            usb_printf("  %s.%s = %s\n", section, key, value);
        }
        else
        {
            usb_printf("  Unknown key: %s.%s (ignored)\n", section, key);
        }
    }

    fclose(f);
    usb_printf("Config loaded.\n");
}


// ---------- Public API ----------
void config_init(void)
{
    config_fill_defaults(&config);

    if (littlefs_mounted && file_exists(CONFIG_PATH))
        config_parse_ini();
    else
        usb_printf("No /config.ini — using compiled defaults.\n");
}


void config_save(void)
{
    if (!littlefs_mounted)
    {
        printfnl(SOURCE_COMMANDS, "Error: LittleFS not mounted\n");
        return;
    }

    char fpath[64];
    lfs_path(fpath, sizeof(fpath), CONFIG_PATH);
    FILE *f = fopen(fpath, "w");
    if (!f)
    {
        printfnl(SOURCE_COMMANDS, "Error: cannot open %s for writing\n", CONFIG_PATH);
        return;
    }

    const char *prev_section = "";
    uint8_t *base = (uint8_t *)&config;

    for (int i = 0; i < CFG_TABLE_SIZE; i++)
    {
        const cfg_descriptor_t *d = &cfg_table[i];

        // New section header?
        if (strcmp(d->section, prev_section) != 0)
        {
            if (i > 0)
                fprintf(f, "\n");
            fprintf(f, "[%s]\n", d->section);
            prev_section = d->section;
        }

        switch (d->type)
        {
        case CFG_STR:
            fprintf(f, "%s=%s\n", d->key, (const char *)(base + d->offset));
            break;
        case CFG_FLOAT:
            fprintf(f, "%s=%.9g\n", d->key, *(float *)(base + d->offset));
            break;
        case CFG_INT:
            fprintf(f, "%s=%d\n", d->key, *(int *)(base + d->offset));
            break;
        case CFG_HEX:
            fprintf(f, "%s=0x%04X\n", d->key, *(int *)(base + d->offset));
            break;
        case CFG_BOOL:
            fprintf(f, "%s=%s\n", d->key, *(bool *)(base + d->offset) ? "on" : "off");
            break;
        }
    }

    fclose(f);
    printfnl(SOURCE_COMMANDS, "Config saved to %s\n", CONFIG_PATH);
}


void config_reset(void)
{
    if (littlefs_mounted && file_exists(CONFIG_PATH)) {
        char fpath[64];
        lfs_path(fpath, sizeof(fpath), CONFIG_PATH);
        unlink(fpath);
    }

    config_fill_defaults(&config);
    printfnl(SOURCE_COMMANDS, "Config reset to compiled defaults.\n");
}


void config_apply_debug(void)
{
    setDebugLevel(SOURCE_SYSTEM,    config.dbg_system);
    setDebugLevel(SOURCE_BASIC,     config.dbg_basic);
    setDebugLevel(SOURCE_WASM,      config.dbg_wasm);
    setDebugLevel(SOURCE_COMMANDS,  config.dbg_commands);
    setDebugLevel(SOURCE_GPS,       config.dbg_gps);
    setDebugLevel(SOURCE_GPS_RAW,   config.dbg_gps_raw);
    setDebugLevel(SOURCE_LORA,      config.dbg_lora);
    setDebugLevel(SOURCE_LORA_RAW,  config.dbg_lora_raw);
    setDebugLevel(SOURCE_FSYNC,     config.dbg_fsync);
    setDebugLevel(SOURCE_WIFI,      config.dbg_wifi);
    setDebugLevel(SOURCE_SENSORS,   config.dbg_sensors);
    setDebugLevel(SOURCE_MQTT,      config.dbg_mqtt);
    setDebugLevel(SOURCE_OTHER,     config.dbg_other);
}


// ---------- Web interface ----------

// ---- Config page buffer ----
static char cfg_page[6144];
static int cfg_pos;

static void cfg_reset() { cfg_pos = 0; cfg_page[0] = '\0'; }

static void cfg_cat(const char *s)
{
    int n = strlen(s);
    if (cfg_pos + n < (int)sizeof(cfg_page)) {
        memcpy(cfg_page + cfg_pos, s, n + 1);
        cfg_pos += n;
    }
}

__attribute__((format(printf, 1, 2)))
static void cfg_catf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(cfg_page + cfg_pos, sizeof(cfg_page) - cfg_pos, fmt, ap);
    va_end(ap);
    if (n > 0 && cfg_pos + n < (int)sizeof(cfg_page))
        cfg_pos += n;
}

static void cfg_cat_attr_escaped(const char *str)
{
    while (*str && cfg_pos < (int)sizeof(cfg_page) - 7)
    {
        if (*str == '<')       { memcpy(cfg_page + cfg_pos, "&lt;", 4); cfg_pos += 4; }
        else if (*str == '>')  { memcpy(cfg_page + cfg_pos, "&gt;", 4); cfg_pos += 4; }
        else if (*str == '&')  { memcpy(cfg_page + cfg_pos, "&amp;", 5); cfg_pos += 5; }
        else if (*str == '"')  { memcpy(cfg_page + cfg_pos, "&quot;", 6); cfg_pos += 6; }
        else                   { cfg_page[cfg_pos++] = *str; }
        str++;
    }
    cfg_page[cfg_pos] = '\0';
}


const char* config_get_html(const char *msg)
{
    cfg_reset();
    cfg_cat("<html><head><style>"
        "body{font-family:sans-serif;max-width:700px;margin:auto;padding:10px}"
        "fieldset{margin-bottom:12px} legend{font-weight:bold}"
        "label{display:inline-block;width:140px} input[type=text],input[type=password]{width:200px}"
        ".msg{padding:8px;margin-bottom:10px;background:#d4edda;border:1px solid #c3e6cb}"
        ".btn{margin-top:10px;padding:6px 16px}"
        "</style></head><body>\n");

    cfg_cat("<h2>ConeZ Configuration</h2>\n");

    if (msg && msg[0])
    {
        cfg_cat("<div class='msg'>");
        cfg_cat(msg);
        cfg_cat("</div>\n");
    }

    cfg_cat("<form method='POST' action='/config'>\n");

    const char *prev_section = "";
    uint8_t *base = (uint8_t *)&config;
    char buf[64];

    for (int i = 0; i < CFG_TABLE_SIZE; i++)
    {
        const cfg_descriptor_t *d = &cfg_table[i];

        // New section — close previous fieldset, open new one
        if (strcmp(d->section, prev_section) != 0)
        {
            if (i > 0)
                cfg_cat("</fieldset>\n");
            cfg_catf("<fieldset><legend>%s</legend>\n", d->section);
            prev_section = d->section;
        }

        // Build the form field name: section.key
        snprintf(buf, sizeof(buf), "%s.%s", d->section, d->key);

        cfg_catf("<label>%s</label> ", d->key);

        switch (d->type)
        {
        case CFG_BOOL:
        {
            bool val = *(bool *)(base + d->offset);
            cfg_catf("<input type='checkbox' name='%s' value='1'%s><br>\n",
                buf, val ? " checked" : "");
            break;
        }
        case CFG_STR:
        {
            const char *val = (const char *)(base + d->offset);
            bool is_password = (strcmp(d->section, "wifi") == 0 && strcmp(d->key, "password") == 0);
            cfg_catf("<input type='%s' name='%s' value='",
                is_password ? "password" : "text", buf);
            cfg_cat_attr_escaped(val);
            cfg_cat("'><br>\n");
            break;
        }
        case CFG_HEX:
        {
            int val = *(int *)(base + d->offset);
            cfg_catf("<input type='text' name='%s' value='0x%04X'><br>\n", buf, val);
            break;
        }
        case CFG_FLOAT:
        {
            float val = *(float *)(base + d->offset);
            cfg_catf("<input type='text' name='%s' value='%.9g'><br>\n", buf, val);
            break;
        }
        case CFG_INT:
        {
            int val = *(int *)(base + d->offset);
            cfg_catf("<input type='text' name='%s' value='%d'><br>\n", buf, val);
            break;
        }
        }
    }

    // Close last fieldset
    if (CFG_TABLE_SIZE > 0)
        cfg_cat("</fieldset>\n");

    cfg_cat("<input type='submit' value='Save' class='btn'>\n");
    cfg_cat("</form>\n");

    cfg_cat("<form method='POST' action='/config/reset' "
            "onsubmit=\"return confirm('Reset all settings to defaults?')\">\n");
    cfg_cat("<input type='submit' value='Reset to Defaults' class='btn'>\n");
    cfg_cat("</form>\n");

    cfg_cat("<br><a href='/'>Back to Home</a>\n");
    cfg_cat("</body></html>\n");

    return cfg_page;
}


// URL-decode in place (+ -> space, %XX -> byte)
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *str)
{
    char *dst = str;
    while (*str) {
        if (*str == '+') {
            *dst++ = ' ';
            str++;
        } else if (*str == '%' && str[1] && str[2]) {
            int h = hex_val(str[1]), l = hex_val(str[2]);
            if (h >= 0 && l >= 0) {
                *dst++ = (h << 4) | l;
                str += 3;
            } else {
                *dst++ = *str++;
            }
        } else {
            *dst++ = *str++;
        }
    }
    *dst = '\0';
}

void config_set_from_web(const char *body)
{
    char buf[64], val[128];
    uint8_t *base = (uint8_t *)&config;

    for (int i = 0; i < CFG_TABLE_SIZE; i++)
    {
        const cfg_descriptor_t *d = &cfg_table[i];
        snprintf(buf, sizeof(buf), "%s.%s", d->section, d->key);

        if (d->type == CFG_BOOL)
        {
            // Checkbox: absent means off, present means on
            bool checked = (httpd_query_key_value(body, buf, val, sizeof(val)) == ESP_OK);
            *(bool *)(base + d->offset) = checked;
        }
        else
        {
            if (httpd_query_key_value(body, buf, val, sizeof(val)) == ESP_OK) {
                url_decode(val);
                if (val[0])
                    config_set_field(d, val);
            }
        }
    }

    config_save();
    config_apply_debug();
}


// ---------- CLI handler ----------

// Display all current config values
static void config_show(void)
{
    const char *prev_section = "";
    uint8_t *base = (uint8_t *)&config;

    printfnl(SOURCE_COMMANDS, "Current configuration:\n");

    for (int i = 0; i < CFG_TABLE_SIZE; i++)
    {
        const cfg_descriptor_t *d = &cfg_table[i];

        if (strcmp(d->section, prev_section) != 0)
        {
            printfnl(SOURCE_COMMANDS, "\n  [%s]\n", d->section);
            prev_section = d->section;
        }

        switch (d->type)
        {
        case CFG_STR:
            printfnl(SOURCE_COMMANDS, "  %-16s = %s\n", d->key, (const char *)(base + d->offset));
            break;
        case CFG_FLOAT:
            printfnl(SOURCE_COMMANDS, "  %-16s = %.9g\n", d->key, *(float *)(base + d->offset));
            break;
        case CFG_INT:
            printfnl(SOURCE_COMMANDS, "  %-16s = %d\n", d->key, *(int *)(base + d->offset));
            break;
        case CFG_HEX:
            printfnl(SOURCE_COMMANDS, "  %-16s = 0x%04X\n", d->key, *(int *)(base + d->offset));
            break;
        case CFG_BOOL:
            printfnl(SOURCE_COMMANDS, "  %-16s = %s\n", d->key, *(bool *)(base + d->offset) ? "on" : "off");
            break;
        }
    }
    printfnl(SOURCE_COMMANDS, "\n");
}


int cmd_config(int argc, char **argv)
{
    // "config" with no args — show all
    if (argc == 1)
    {
        config_show();
        return 0;
    }

    // "config reset"
    if (argc == 2 && strcasecmp(argv[1], "reset") == 0)
    {
        config_reset();
        config_apply_debug();
        printfnl(SOURCE_COMMANDS, "Reboot to apply non-debug settings.\n");
        return 0;
    }

    // "config unset section.key" — revert one key to compiled default
    if (argc == 3 && strcasecmp(argv[1], "unset") == 0)
    {
        char buf[48];
        strlcpy(buf, argv[2], sizeof(buf));

        char *dot = strchr(buf, '.');
        if (!dot)
        {
            printfnl(SOURCE_COMMANDS, "Usage: config unset section.key\n");
            return 1;
        }
        *dot = '\0';
        const char *section = buf;
        const char *key     = dot + 1;

        const cfg_descriptor_t *d = config_find(section, key);
        if (!d)
        {
            printfnl(SOURCE_COMMANDS, "Unknown key: %s.%s\n", section, key);
            return 1;
        }

        config_set_default_field(d);
        config_save();

        if (strcasecmp(section, "debug") == 0)
        {
            config_apply_debug();
            printfnl(SOURCE_COMMANDS, "Debug setting reverted to default.\n");
        }
        else
        {
            printfnl(SOURCE_COMMANDS, "Reverted to default. Reboot to apply.\n");
        }

        return 0;
    }

    // "config set section.key value"
    // Values with spaces must be quoted: config set wifi.ssid "My Network"
    if (argc == 4 && strcasecmp(argv[1], "set") == 0)
    {
        // Split section.key
        char buf[48];
        strlcpy(buf, argv[2], sizeof(buf));

        char *dot = strchr(buf, '.');
        if (!dot)
        {
            printfnl(SOURCE_COMMANDS, "Usage: config set section.key value\n");
            return 1;
        }
        *dot = '\0';
        const char *section = buf;
        const char *key     = dot + 1;

        const cfg_descriptor_t *d = config_find(section, key);
        if (!d)
        {
            printfnl(SOURCE_COMMANDS, "Unknown key: %s.%s\n", section, key);
            return 1;
        }

        config_set_field(d, argv[3]);
        config_save();

        // Debug settings hot-apply immediately
        if (strcasecmp(section, "debug") == 0)
        {
            config_apply_debug();
            printfnl(SOURCE_COMMANDS, "Debug setting applied.\n");
        }
        else
        {
            printfnl(SOURCE_COMMANDS, "Reboot to apply.\n");
        }

        return 0;
    }

    printfnl(SOURCE_COMMANDS, "Usage:\n");
    printfnl(SOURCE_COMMANDS, "  config                         Show all settings\n");
    printfnl(SOURCE_COMMANDS, "  config set section.key value   Set a value\n");
    printfnl(SOURCE_COMMANDS, "  config unset section.key       Revert one key to default\n");
    printfnl(SOURCE_COMMANDS, "  config reset                   Revert all to defaults\n");
    return 1;
}
