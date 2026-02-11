#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include <WebServer.h>
#include "config.h"
#include "main.h"
#include "printManager.h"

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
    // [system]
    CFG_ENTRY("system", "device_name",  CFG_STR,   device_name),
    CFG_ENTRY("system", "startup_script", CFG_STR, startup_script),
    CFG_ENTRY("system", "timezone",     CFG_INT,   timezone),
    CFG_ENTRY("system", "auto_dst",    CFG_BOOL,  auto_dst),
    // [led]
    CFG_ENTRY("led",    "count1",       CFG_INT,   led_count1),
    CFG_ENTRY("led",    "count2",       CFG_INT,   led_count2),
    CFG_ENTRY("led",    "count3",       CFG_INT,   led_count3),
    CFG_ENTRY("led",    "count4",       CFG_INT,   led_count4),
    // [debug]
    CFG_ENTRY("debug",  "system",       CFG_BOOL,  dbg_system),
    CFG_ENTRY("debug",  "basic",        CFG_BOOL,  dbg_basic),
    CFG_ENTRY("debug",  "commands",     CFG_BOOL,  dbg_commands),
    CFG_ENTRY("debug",  "gps",          CFG_BOOL,  dbg_gps),
    CFG_ENTRY("debug",  "gps_raw",      CFG_BOOL,  dbg_gps_raw),
    CFG_ENTRY("debug",  "lora",         CFG_BOOL,  dbg_lora),
    CFG_ENTRY("debug",  "lora_raw",     CFG_BOOL,  dbg_lora_raw),
    CFG_ENTRY("debug",  "fsync",        CFG_BOOL,  dbg_fsync),
    CFG_ENTRY("debug",  "wifi",         CFG_BOOL,  dbg_wifi),
    CFG_ENTRY("debug",  "sensors",      CFG_BOOL,  dbg_sensors),
    CFG_ENTRY("debug",  "other",        CFG_BOOL,  dbg_other),
};

static const int CFG_TABLE_SIZE = sizeof(cfg_table) / sizeof(cfg_table[0]);

static const char *CONFIG_PATH = "/config.ini";

// ---------- Helpers ----------
static void config_fill_defaults(conez_config_t *cfg)
{
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

    strlcpy(cfg->device_name,     DEFAULT_DEVICE_NAME,    sizeof(cfg->device_name));
    strlcpy(cfg->startup_script,  DEFAULT_STARTUP_SCRIPT, sizeof(cfg->startup_script));
    cfg->timezone         = DEFAULT_TIMEZONE;
    cfg->auto_dst         = DEFAULT_AUTO_DST;

    cfg->led_count1       = DEFAULT_LED_COUNT;
    cfg->led_count2       = DEFAULT_LED_COUNT;
    cfg->led_count3       = DEFAULT_LED_COUNT;
    cfg->led_count4       = DEFAULT_LED_COUNT;

    cfg->dbg_system       = DEFAULT_DBG_SYSTEM;
    cfg->dbg_basic        = DEFAULT_DBG_BASIC;
    cfg->dbg_commands     = DEFAULT_DBG_COMMANDS;
    cfg->dbg_gps          = DEFAULT_DBG_GPS;
    cfg->dbg_gps_raw      = DEFAULT_DBG_GPS_RAW;
    cfg->dbg_lora         = DEFAULT_DBG_LORA;
    cfg->dbg_lora_raw     = DEFAULT_DBG_LORA_RAW;
    cfg->dbg_fsync        = DEFAULT_DBG_FSYNC;
    cfg->dbg_wifi         = DEFAULT_DBG_WIFI;
    cfg->dbg_sensors      = DEFAULT_DBG_SENSORS;
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
    File f = FSLINK.open(CONFIG_PATH, "r");
    if (!f)
        return;

    Serial.println("Loading /config.ini...");

    char line[128];
    char section[16] = "";

    while (f.available())
    {
        int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';

        // Trim trailing \r
        if (len > 0 && line[len - 1] == '\r')
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
            Serial.printf("  %s.%s = %s\n", section, key, value);
        }
        else
        {
            Serial.printf("  Unknown key: %s.%s (ignored)\n", section, key);
        }
    }

    f.close();
    Serial.println("Config loaded.");
}


// ---------- Public API ----------
void config_init(void)
{
    config_fill_defaults(&config);

    if (littlefs_mounted && FSLINK.exists(CONFIG_PATH))
        config_parse_ini();
    else
        Serial.println("No /config.ini — using compiled defaults.");
}


void config_save(void)
{
    if (!littlefs_mounted)
    {
        printfnl(SOURCE_COMMANDS, F("Error: LittleFS not mounted\n"));
        return;
    }

    File f = FSLINK.open(CONFIG_PATH, "w");
    if (!f)
    {
        printfnl(SOURCE_COMMANDS, F("Error: cannot open %s for writing\n"), CONFIG_PATH);
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
                f.print("\n");
            f.printf("[%s]\n", d->section);
            prev_section = d->section;
        }

        switch (d->type)
        {
        case CFG_STR:
            f.printf("%s=%s\n", d->key, (const char *)(base + d->offset));
            break;
        case CFG_FLOAT:
            f.printf("%s=%.9g\n", d->key, *(float *)(base + d->offset));
            break;
        case CFG_INT:
            f.printf("%s=%d\n", d->key, *(int *)(base + d->offset));
            break;
        case CFG_HEX:
            f.printf("%s=0x%04X\n", d->key, *(int *)(base + d->offset));
            break;
        case CFG_BOOL:
            f.printf("%s=%s\n", d->key, *(bool *)(base + d->offset) ? "on" : "off");
            break;
        }
    }

    f.close();
    printfnl(SOURCE_COMMANDS, F("Config saved to %s\n"), CONFIG_PATH);
}


void config_reset(void)
{
    if (littlefs_mounted && FSLINK.exists(CONFIG_PATH))
        FSLINK.remove(CONFIG_PATH);

    config_fill_defaults(&config);
    printfnl(SOURCE_COMMANDS, F("Config reset to compiled defaults.\n"));
}


void config_apply_debug(void)
{
    setDebugLevel(SOURCE_SYSTEM,    config.dbg_system);
    setDebugLevel(SOURCE_BASIC,     config.dbg_basic);
    setDebugLevel(SOURCE_COMMANDS,  config.dbg_commands);
    setDebugLevel(SOURCE_GPS,       config.dbg_gps);
    setDebugLevel(SOURCE_GPS_RAW,   config.dbg_gps_raw);
    setDebugLevel(SOURCE_LORA,      config.dbg_lora);
    setDebugLevel(SOURCE_LORA_RAW,  config.dbg_lora_raw);
    setDebugLevel(SOURCE_FSYNC,     config.dbg_fsync);
    setDebugLevel(SOURCE_WIFI,      config.dbg_wifi);
    setDebugLevel(SOURCE_SENSORS,   config.dbg_sensors);
    setDebugLevel(SOURCE_OTHER,     config.dbg_other);
}


// ---------- Web interface ----------

static String html_attr_escape(const char *str)
{
    String out;
    while (*str)
    {
        if (*str == '<')       out += "&lt;";
        else if (*str == '>')  out += "&gt;";
        else if (*str == '&')  out += "&amp;";
        else if (*str == '"')  out += "&quot;";
        else                   out += *str;
        str++;
    }
    return out;
}


String config_get_html(const char *msg)
{
    String page = "<html><head><style>"
        "body{font-family:sans-serif;max-width:700px;margin:auto;padding:10px}"
        "fieldset{margin-bottom:12px} legend{font-weight:bold}"
        "label{display:inline-block;width:140px} input[type=text],input[type=password]{width:200px}"
        ".msg{padding:8px;margin-bottom:10px;background:#d4edda;border:1px solid #c3e6cb}"
        ".btn{margin-top:10px;padding:6px 16px}"
        "</style></head><body>\n";

    page += "<h2>ConeZ Configuration</h2>\n";

    if (msg && msg[0])
    {
        page += "<div class='msg'>";
        page += msg;
        page += "</div>\n";
    }

    page += "<form method='POST' action='/config'>\n";

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
                page += "</fieldset>\n";
            page += "<fieldset><legend>";
            page += d->section;
            page += "</legend>\n";
            prev_section = d->section;
        }

        // Build the form field name: section.key
        snprintf(buf, sizeof(buf), "%s.%s", d->section, d->key);

        page += "<label>";
        page += d->key;
        page += "</label> ";

        switch (d->type)
        {
        case CFG_BOOL:
        {
            bool val = *(bool *)(base + d->offset);
            page += "<input type='checkbox' name='";
            page += buf;
            page += "' value='1'";
            if (val) page += " checked";
            page += "><br>\n";
            break;
        }
        case CFG_STR:
        {
            const char *val = (const char *)(base + d->offset);
            bool is_password = (strcmp(d->section, "wifi") == 0 && strcmp(d->key, "password") == 0);
            page += "<input type='";
            page += is_password ? "password" : "text";
            page += "' name='";
            page += buf;
            page += "' value='";
            page += html_attr_escape(val);
            page += "'><br>\n";
            break;
        }
        case CFG_HEX:
        {
            int val = *(int *)(base + d->offset);
            char hexbuf[16];
            snprintf(hexbuf, sizeof(hexbuf), "0x%04X", val);
            page += "<input type='text' name='";
            page += buf;
            page += "' value='";
            page += hexbuf;
            page += "'><br>\n";
            break;
        }
        case CFG_FLOAT:
        {
            float val = *(float *)(base + d->offset);
            char fbuf[32];
            snprintf(fbuf, sizeof(fbuf), "%.9g", val);
            page += "<input type='text' name='";
            page += buf;
            page += "' value='";
            page += fbuf;
            page += "'><br>\n";
            break;
        }
        case CFG_INT:
        {
            int val = *(int *)(base + d->offset);
            char ibuf[16];
            snprintf(ibuf, sizeof(ibuf), "%d", val);
            page += "<input type='text' name='";
            page += buf;
            page += "' value='";
            page += ibuf;
            page += "'><br>\n";
            break;
        }
        }
    }

    // Close last fieldset
    if (CFG_TABLE_SIZE > 0)
        page += "</fieldset>\n";

    page += "<input type='submit' value='Save' class='btn'>\n";
    page += "</form>\n";

    page += "<form method='POST' action='/config/reset' "
            "onsubmit=\"return confirm('Reset all settings to defaults?')\">\n";
    page += "<input type='submit' value='Reset to Defaults' class='btn'>\n";
    page += "</form>\n";

    page += "<br><a href='/'>Back to Home</a>\n";
    page += "</body></html>\n";

    return page;
}


void config_set_from_web(WebServer &srv)
{
    char buf[64];
    uint8_t *base = (uint8_t *)&config;

    for (int i = 0; i < CFG_TABLE_SIZE; i++)
    {
        const cfg_descriptor_t *d = &cfg_table[i];
        snprintf(buf, sizeof(buf), "%s.%s", d->section, d->key);

        if (d->type == CFG_BOOL)
        {
            // Checkbox: absent means off, present means on
            bool checked = srv.hasArg(buf);
            *(bool *)(base + d->offset) = checked;
        }
        else
        {
            String val = srv.arg(buf);
            if (val.length() > 0)
                config_set_field(d, val.c_str());
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

    printfnl(SOURCE_COMMANDS, F("Current configuration:\n"));

    for (int i = 0; i < CFG_TABLE_SIZE; i++)
    {
        const cfg_descriptor_t *d = &cfg_table[i];

        if (strcmp(d->section, prev_section) != 0)
        {
            printfnl(SOURCE_COMMANDS, F("\n  [%s]\n"), d->section);
            prev_section = d->section;
        }

        switch (d->type)
        {
        case CFG_STR:
            printfnl(SOURCE_COMMANDS, F("  %-16s = %s\n"), d->key, (const char *)(base + d->offset));
            break;
        case CFG_FLOAT:
            printfnl(SOURCE_COMMANDS, F("  %-16s = %.9g\n"), d->key, *(float *)(base + d->offset));
            break;
        case CFG_INT:
            printfnl(SOURCE_COMMANDS, F("  %-16s = %d\n"), d->key, *(int *)(base + d->offset));
            break;
        case CFG_HEX:
            printfnl(SOURCE_COMMANDS, F("  %-16s = 0x%04X\n"), d->key, *(int *)(base + d->offset));
            break;
        case CFG_BOOL:
            printfnl(SOURCE_COMMANDS, F("  %-16s = %s\n"), d->key, *(bool *)(base + d->offset) ? "on" : "off");
            break;
        }
    }
    printfnl(SOURCE_COMMANDS, F("\n"));
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
        printfnl(SOURCE_COMMANDS, F("Reboot to apply non-debug settings.\n"));
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
            printfnl(SOURCE_COMMANDS, F("Usage: config unset section.key\n"));
            return 1;
        }
        *dot = '\0';
        const char *section = buf;
        const char *key     = dot + 1;

        const cfg_descriptor_t *d = config_find(section, key);
        if (!d)
        {
            printfnl(SOURCE_COMMANDS, F("Unknown key: %s.%s\n"), section, key);
            return 1;
        }

        config_set_default_field(d);
        config_save();

        if (strcasecmp(section, "debug") == 0)
        {
            config_apply_debug();
            printfnl(SOURCE_COMMANDS, F("Debug setting reverted to default.\n"));
        }
        else
        {
            printfnl(SOURCE_COMMANDS, F("Reverted to default. Reboot to apply.\n"));
        }

        return 0;
    }

    // "config set section.key value"
    if (argc >= 4 && strcasecmp(argv[1], "set") == 0)
    {
        // Split section.key
        char buf[48];
        strlcpy(buf, argv[2], sizeof(buf));

        char *dot = strchr(buf, '.');
        if (!dot)
        {
            printfnl(SOURCE_COMMANDS, F("Usage: config set section.key value\n"));
            return 1;
        }
        *dot = '\0';
        const char *section = buf;
        const char *key     = dot + 1;

        const cfg_descriptor_t *d = config_find(section, key);
        if (!d)
        {
            printfnl(SOURCE_COMMANDS, F("Unknown key: %s.%s\n"), section, key);
            return 1;
        }

        // Reassemble value from argv[3..] to allow spaces
        // (SimpleSerialShell splits on spaces, so "config set wifi.ssid My Net" gives argc=5)
        char value[128] = "";
        for (int i = 3; i < argc; i++)
        {
            if (i > 3) strlcat(value, " ", sizeof(value));
            strlcat(value, argv[i], sizeof(value));
        }

        config_set_field(d, value);
        config_save();

        // Debug settings hot-apply immediately
        if (strcasecmp(section, "debug") == 0)
        {
            config_apply_debug();
            printfnl(SOURCE_COMMANDS, F("Debug setting applied.\n"));
        }
        else
        {
            printfnl(SOURCE_COMMANDS, F("Reboot to apply.\n"));
        }

        return 0;
    }

    printfnl(SOURCE_COMMANDS, F("Usage:\n"));
    printfnl(SOURCE_COMMANDS, F("  config                         Show all settings\n"));
    printfnl(SOURCE_COMMANDS, F("  config set section.key value   Set a value\n"));
    printfnl(SOURCE_COMMANDS, F("  config unset section.key       Revert one key to default\n"));
    printfnl(SOURCE_COMMANDS, F("  config reset                   Revert all to defaults\n"));
    return 1;
}
