/*
 * preproc.c — minimal C preprocessor for c2wasm
 *
 * Handles:
 *   #include "conez_api.h"  → registers all ConeZ API imports
 *   #define NAME value      → macro table
 *   #ifdef / #ifndef / #else / #endif / #if 0
 */
#include "c2wasm.h"

static int is_ident_char(int c) { return isalnum(c) || c == '_'; }

#define MAX_IFDEF_DEPTH 32
static int ifdef_skip[MAX_IFDEF_DEPTH];
static int ifdef_depth;

static int api_registered;

void preproc_init(void) {
    ifdef_depth = 0;
    api_registered = 0;
}

int preproc_skipping(void) {
    for (int i = 0; i < ifdef_depth; i++)
        if (ifdef_skip[i]) return 1;
    return 0;
}

/* C name → import ID mapping for conez_api.h functions */
typedef struct {
    const char *c_name;
    int imp_id;
    CType ret_type;
    int nparam;
    CType params[8];
} ApiFunc;

static ApiFunc api_funcs[] = {
    {"delay_ms",        IMP_DELAY_MS,       CT_VOID,  1, {CT_INT}},
    {"millis",          IMP_MILLIS,          CT_INT,   0, {}},
    {"get_param",       IMP_GET_PARAM,       CT_INT,   1, {CT_INT}},
    {"set_param",       IMP_SET_PARAM,       CT_VOID,  2, {CT_INT, CT_INT}},
    {"should_stop",     IMP_SHOULD_STOP,     CT_INT,   0, {}},
    {"led_set_pixel",   IMP_LED_SET_PIXEL,   CT_VOID,  5, {CT_INT,CT_INT,CT_INT,CT_INT,CT_INT}},
    {"led_fill",        IMP_LED_FILL,        CT_VOID,  4, {CT_INT,CT_INT,CT_INT,CT_INT}},
    {"led_show",        IMP_LED_SHOW,        CT_VOID,  0, {}},
    {"led_count",       IMP_LED_COUNT,       CT_INT,   1, {CT_INT}},
    {"led_gamma8",      IMP_LED_GAMMA8,      CT_INT,   1, {CT_INT}},
    {"led_set_gamma",   IMP_LED_SET_GAMMA,   CT_VOID,  1, {CT_INT}},
    {"led_set_buffer",  IMP_LED_SET_BUFFER,  CT_VOID,  3, {CT_INT,CT_INT,CT_INT}},
    {"led_shift",       IMP_LED_SHIFT,       CT_VOID,  5, {CT_INT,CT_INT,CT_INT,CT_INT,CT_INT}},
    {"led_rotate",      IMP_LED_ROTATE,      CT_VOID,  2, {CT_INT,CT_INT}},
    {"led_reverse",     IMP_LED_REVERSE,     CT_VOID,  1, {CT_INT}},
    {"led_set_pixel_hsv",IMP_LED_SET_PIXEL_HSV,CT_VOID,5,{CT_INT,CT_INT,CT_INT,CT_INT,CT_INT}},
    {"led_fill_hsv",    IMP_LED_FILL_HSV,    CT_VOID,  4, {CT_INT,CT_INT,CT_INT,CT_INT}},
    {"hsv_to_rgb",      IMP_HSV_TO_RGB,      CT_INT,   3, {CT_INT,CT_INT,CT_INT}},
    {"rgb_to_hsv",      IMP_RGB_TO_HSV,      CT_INT,   3, {CT_INT,CT_INT,CT_INT}},
    {"print_i32",       IMP_PRINT_I32,       CT_VOID,  1, {CT_INT}},
    {"print_f32",       IMP_PRINT_F32,       CT_VOID,  1, {CT_FLOAT}},
    {"print_str",       IMP_PRINT_STR,       CT_VOID,  2, {CT_INT,CT_INT}},
    {"gps_valid",       IMP_GPS_VALID,       CT_INT,   0, {}},
    {"has_origin",      IMP_HAS_ORIGIN,      CT_INT,   0, {}},
    {"origin_dist",     IMP_ORIGIN_DIST,     CT_FLOAT, 0, {}},
    {"origin_bearing",  IMP_ORIGIN_BEARING,  CT_FLOAT, 0, {}},
    {"get_lat",         IMP_GET_LAT,         CT_FLOAT, 0, {}},
    {"get_lon",         IMP_GET_LON,         CT_FLOAT, 0, {}},
    {"get_alt",         IMP_GET_ALT,         CT_FLOAT, 0, {}},
    {"get_speed",       IMP_GET_SPEED,       CT_FLOAT, 0, {}},
    {"get_dir",         IMP_GET_DIR,         CT_FLOAT, 0, {}},
    {"get_second",      IMP_GET_SECOND,      CT_INT,   0, {}},
    {"get_minute",      IMP_GET_MINUTE,      CT_INT,   0, {}},
    {"get_hour",        IMP_GET_HOUR,        CT_INT,   0, {}},
    {"get_day",         IMP_GET_DAY,         CT_INT,   0, {}},
    {"get_month",       IMP_GET_MONTH,       CT_INT,   0, {}},
    {"get_year",        IMP_GET_YEAR,        CT_INT,   0, {}},
    {"get_day_of_week", IMP_GET_DAY_OF_WEEK, CT_INT,   0, {}},
    {"get_day_of_year", IMP_GET_DAY_OF_YEAR, CT_INT,   0, {}},
    {"get_is_leap_year",IMP_GET_IS_LEAP_YEAR,CT_INT,   0, {}},
    {"time_valid",      IMP_TIME_VALID,      CT_INT,   0, {}},
    {"imu_valid",       IMP_IMU_VALID,       CT_INT,   0, {}},
    {"get_pitch",       IMP_GET_PITCH,       CT_FLOAT, 0, {}},
    {"get_roll",        IMP_GET_ROLL,        CT_FLOAT, 0, {}},
    {"get_yaw",         IMP_GET_YAW,         CT_FLOAT, 0, {}},
    {"get_acc_x",       IMP_GET_ACC_X,       CT_FLOAT, 0, {}},
    {"get_acc_y",       IMP_GET_ACC_Y,       CT_FLOAT, 0, {}},
    {"get_acc_z",       IMP_GET_ACC_Z,       CT_FLOAT, 0, {}},
    {"get_temp",        IMP_GET_TEMP,        CT_FLOAT, 0, {}},
    {"get_humidity",    IMP_GET_HUMIDITY,     CT_FLOAT, 0, {}},
    {"get_brightness",  IMP_GET_BRIGHTNESS,  CT_FLOAT, 0, {}},
    {"random_int",      IMP_RANDOM_INT,      CT_INT,   2, {CT_INT,CT_INT}},
    {"sinf",            IMP_SINF,            CT_FLOAT, 1, {CT_FLOAT}},
    {"cosf",            IMP_COSF,            CT_FLOAT, 1, {CT_FLOAT}},
    {"tanf",            IMP_TANF,            CT_FLOAT, 1, {CT_FLOAT}},
    {"atan2f",          IMP_ATAN2F,          CT_FLOAT, 2, {CT_FLOAT,CT_FLOAT}},
    {"powf",            IMP_POWF,            CT_FLOAT, 2, {CT_FLOAT,CT_FLOAT}},
    {"expf",            IMP_EXPF,            CT_FLOAT, 1, {CT_FLOAT}},
    {"logf",            IMP_LOGF,            CT_FLOAT, 1, {CT_FLOAT}},
    {"log2f",           IMP_LOG2F,           CT_FLOAT, 1, {CT_FLOAT}},
    {"fmodf",           IMP_FMODF,           CT_FLOAT, 2, {CT_FLOAT,CT_FLOAT}},
    {"lut_load",        IMP_LUT_LOAD,        CT_INT,   1, {CT_INT}},
    {"lut_save",        IMP_LUT_SAVE,        CT_INT,   1, {CT_INT}},
    {"lut_check",       IMP_LUT_CHECK,       CT_INT,   1, {CT_INT}},
    {"lut_get",         IMP_LUT_GET,         CT_INT,   1, {CT_INT}},
    {"lut_set",         IMP_LUT_SET,         CT_VOID,  2, {CT_INT,CT_INT}},
    {"lut_size",        IMP_LUT_SIZE,        CT_INT,   0, {}},
    {"wait_pps",        IMP_WAIT_PPS,        CT_INT,   1, {CT_INT}},
    {"wait_param",      IMP_WAIT_PARAM,      CT_INT,   4, {CT_INT,CT_INT,CT_INT,CT_INT}},
    {"cue_playing",     IMP_CUE_PLAYING,     CT_INT,   0, {}},
    {"cue_elapsed",     IMP_CUE_ELAPSED,     CT_INT,   0, {}},
    {"get_bat_voltage", IMP_GET_BAT_VOLTAGE, CT_FLOAT, 0, {}},
    {"get_solar_voltage",IMP_GET_SOLAR_VOLTAGE,CT_FLOAT,0, {}},
    {"get_sunrise",     IMP_GET_SUNRISE,     CT_INT,   0, {}},
    {"get_sunset",      IMP_GET_SUNSET,      CT_INT,   0, {}},
    {"sun_valid",       IMP_SUN_VALID,       CT_INT,   0, {}},
    {"is_daylight",     IMP_IS_DAYLIGHT,     CT_INT,   0, {}},
    {"pin_set",         IMP_PIN_SET,         CT_VOID,  1, {CT_INT}},
    {"pin_clear",       IMP_PIN_CLEAR,       CT_VOID,  1, {CT_INT}},
    {"pin_read",        IMP_PIN_READ,        CT_INT,   1, {CT_INT}},
    {"analog_read",     IMP_ANALOG_READ,     CT_INT,   1, {CT_INT}},
    {"gps_present",     IMP_GPS_PRESENT,     CT_INT,   0, {}},
    {"imu_present",     IMP_IMU_PRESENT,     CT_INT,   0, {}},
    {"get_battery_percentage", IMP_GET_BATTERY_PERCENTAGE, CT_FLOAT, 0, {}},
    {"get_battery_runtime",    IMP_GET_BATTERY_RUNTIME,    CT_FLOAT, 0, {}},
    {"get_sun_azimuth", IMP_GET_SUN_AZIMUTH, CT_FLOAT, 0, {}},
    {"get_sun_elevation",IMP_GET_SUN_ELEVATION,CT_FLOAT,0, {}},
    {"asinf",           IMP_ASINF,           CT_FLOAT, 1, {CT_FLOAT}},
    {"acosf",           IMP_ACOSF,           CT_FLOAT, 1, {CT_FLOAT}},
    {"atanf",           IMP_ATANF,           CT_FLOAT, 1, {CT_FLOAT}},
    {"get_origin_lat",  IMP_GET_ORIGIN_LAT,  CT_FLOAT, 0, {}},
    {"get_origin_lon",  IMP_GET_ORIGIN_LON,  CT_FLOAT, 0, {}},
    {"file_open",       IMP_FILE_OPEN,       CT_INT,   3, {CT_INT,CT_INT,CT_INT}},
    {"file_close",      IMP_FILE_CLOSE,      CT_VOID,  1, {CT_INT}},
    {"file_read",       IMP_FILE_READ,       CT_INT,   3, {CT_INT,CT_INT,CT_INT}},
    {"file_write",      IMP_FILE_WRITE,      CT_INT,   3, {CT_INT,CT_INT,CT_INT}},
    {"file_size",       IMP_FILE_SIZE,       CT_INT,   1, {CT_INT}},
    {"file_seek",       IMP_FILE_SEEK,       CT_INT,   2, {CT_INT,CT_INT}},
    {"file_tell",       IMP_FILE_TELL,       CT_INT,   1, {CT_INT}},
    {"file_exists",     IMP_FILE_EXISTS,     CT_INT,   2, {CT_INT,CT_INT}},
    {"file_delete",     IMP_FILE_DELETE,     CT_INT,   2, {CT_INT,CT_INT}},
    {"file_rename",     IMP_FILE_RENAME,     CT_INT,   4, {CT_INT,CT_INT,CT_INT,CT_INT}},
    {"file_mkdir",      IMP_FILE_MKDIR,      CT_INT,   2, {CT_INT,CT_INT}},
    {"file_rmdir",      IMP_FILE_RMDIR,      CT_INT,   2, {CT_INT,CT_INT}},
    {"host_snprintf",   IMP_HOST_SNPRINTF,   CT_INT,   4, {CT_INT,CT_INT,CT_INT,CT_INT}},
    {NULL, 0, 0, 0, {}}
};

void register_api_imports(void) {
    if (api_registered) return;
    api_registered = 1;

    for (ApiFunc *a = api_funcs; a->c_name; a++) {
        Symbol *s = add_sym(a->c_name, SYM_IMPORT, a->ret_type);
        s->imp_id = a->imp_id;
        s->param_count = a->nparam;
        for (int i = 0; i < a->nparam; i++)
            s->param_types[i] = a->params[i];
        s->scope = 0;
    }

    /* Register host_printf as a known import (used by printf builtin) */
    Symbol *hp = add_sym("host_printf", SYM_IMPORT, CT_INT);
    hp->imp_id = IMP_HOST_PRINTF;
    hp->param_count = 2;
    hp->param_types[0] = CT_INT;
    hp->param_types[1] = CT_INT;
}

static void skip_to_eol(void) {
    while (src_pos < src_len && source[src_pos] != '\n')
        src_pos++;
}

static void read_pp_word(char *buf, int max) {
    int i = 0;
    while (src_pos < src_len && (source[src_pos] == ' ' || source[src_pos] == '\t'))
        src_pos++;
    while (src_pos < src_len && is_ident_char(source[src_pos]) && i < max - 1)
        buf[i++] = source[src_pos++];
    buf[i] = 0;
}

static void read_pp_value(char *buf, int max) {
    int i = 0;
    while (src_pos < src_len && (source[src_pos] == ' ' || source[src_pos] == '\t'))
        src_pos++;
    while (src_pos < src_len && source[src_pos] != '\n' && i < max - 1)
        buf[i++] = source[src_pos++];
    /* Trim trailing whitespace */
    while (i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t')) i--;
    buf[i] = 0;
}

int preproc_line(void) {
    /* We're at '#', check if it's at the start of a logical line */
    src_pos++; /* skip '#' */

    /* Skip whitespace after # */
    while (src_pos < src_len && (source[src_pos] == ' ' || source[src_pos] == '\t'))
        src_pos++;

    char directive[32];
    read_pp_word(directive, sizeof(directive));

    /* Handle ifdef skip mode */
    if (ifdef_depth > 0 && ifdef_skip[ifdef_depth - 1]) {
        if (strcmp(directive, "ifdef") == 0 || strcmp(directive, "ifndef") == 0 ||
            strcmp(directive, "if") == 0) {
            /* Nested ifdef in skipped section — push another skip */
            if (ifdef_depth >= MAX_IFDEF_DEPTH) { error_at("#ifdef too deeply nested"); }
            else { ifdef_skip[ifdef_depth] = 1; ifdef_depth++; }
        } else if (strcmp(directive, "else") == 0) {
            if (ifdef_depth > 0) ifdef_skip[ifdef_depth - 1] = !ifdef_skip[ifdef_depth - 1];
        } else if (strcmp(directive, "endif") == 0) {
            if (ifdef_depth > 0) ifdef_depth--;
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "include") == 0) {
        /* Skip whitespace, read filename */
        while (src_pos < src_len && (source[src_pos] == ' ' || source[src_pos] == '\t'))
            src_pos++;
        if (source[src_pos] == '"') {
            src_pos++;
            char fname[128]; int i = 0;
            while (src_pos < src_len && source[src_pos] != '"' && i < 127)
                fname[i++] = source[src_pos++];
            fname[i] = 0;
            if (source[src_pos] == '"') src_pos++;

            if (strcmp(fname, "conez_api.h") == 0) {
                register_api_imports();
            } else if (strcmp(fname, "stdint.h") == 0 ||
                       strcmp(fname, "stdbool.h") == 0) {
                /* silently ignore standard headers */
            } else {
                error_fmt("unsupported #include \"%s\"", fname);
            }
        } else if (source[src_pos] == '<') {
            /* Skip <...> includes silently */
            while (src_pos < src_len && source[src_pos] != '>' && source[src_pos] != '\n')
                src_pos++;
            if (source[src_pos] == '>') src_pos++;
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "define") == 0) {
        char name[64];
        read_pp_word(name, sizeof(name));
        /* Check for function-like macro — skip if '(' follows immediately */
        if (src_pos < src_len && source[src_pos] == '(') {
            /* Function-like macro — skip the whole thing */
            skip_to_eol();
            return 1;
        }
        char value[128];
        read_pp_value(value, sizeof(value));

        /* Check if already defined */
        Symbol *existing = find_sym_kind(name, SYM_DEFINE);
        if (existing) {
            snprintf(existing->macro_val, sizeof(existing->macro_val), "%s", value);
        } else {
            Symbol *s = add_sym(name, SYM_DEFINE, CT_INT);
            snprintf(s->macro_val, sizeof(s->macro_val), "%s", value);
            s->scope = 0;
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "ifdef") == 0) {
        char name[64];
        read_pp_word(name, sizeof(name));
        int defined = (find_sym_kind(name, SYM_DEFINE) != NULL);
        if (ifdef_depth >= MAX_IFDEF_DEPTH) error_at("#ifdef too deeply nested");
        else { ifdef_skip[ifdef_depth] = !defined; ifdef_depth++; }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "ifndef") == 0) {
        char name[64];
        read_pp_word(name, sizeof(name));
        int defined = (find_sym_kind(name, SYM_DEFINE) != NULL);
        if (ifdef_depth >= MAX_IFDEF_DEPTH) error_at("#ifdef too deeply nested");
        else { ifdef_skip[ifdef_depth] = defined; ifdef_depth++; }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "if") == 0) {
        /* Support #if <integer> — evaluates as boolean (0 = skip, nonzero = keep) */
        while (src_pos < src_len && (source[src_pos] == ' ' || source[src_pos] == '\t'))
            src_pos++;
        int val = 0;
        while (src_pos < src_len && source[src_pos] >= '0' && source[src_pos] <= '9')
            val = val * 10 + (source[src_pos++] - '0');
        if (ifdef_depth >= MAX_IFDEF_DEPTH) error_at("#if too deeply nested");
        else { ifdef_skip[ifdef_depth] = (val == 0); ifdef_depth++; }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "else") == 0) {
        if (ifdef_depth > 0) ifdef_skip[ifdef_depth - 1] = !ifdef_skip[ifdef_depth - 1];
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "endif") == 0) {
        if (ifdef_depth > 0) ifdef_depth--;
        skip_to_eol();
        return 1;
    }

    /* Unknown directive — skip */
    skip_to_eol();
    return 1;
}
