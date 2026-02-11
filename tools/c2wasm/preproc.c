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
static int ifdef_had_else[MAX_IFDEF_DEPTH];
static int ifdef_taken[MAX_IFDEF_DEPTH];
static int ifdef_depth;
static int ifdef_overflow;  /* excess nesting beyond MAX_IFDEF_DEPTH */

static int api_registered;

void preproc_init(void) {
    ifdef_depth = 0;
    ifdef_overflow = 0;
    api_registered = 0;
}

int preproc_skipping(void) {
    if (ifdef_overflow > 0) return 1;
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
    {"print_f64",       IMP_PRINT_F64,       CT_VOID,  1, {CT_DOUBLE}},
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
    {"sin",             IMP_SIN,             CT_DOUBLE, 1, {CT_DOUBLE}},
    {"cos",             IMP_COS,             CT_DOUBLE, 1, {CT_DOUBLE}},
    {"tan",             IMP_TAN,             CT_DOUBLE, 1, {CT_DOUBLE}},
    {"asin",            IMP_ASIN,            CT_DOUBLE, 1, {CT_DOUBLE}},
    {"acos",            IMP_ACOS,            CT_DOUBLE, 1, {CT_DOUBLE}},
    {"atan",            IMP_ATAN,            CT_DOUBLE, 1, {CT_DOUBLE}},
    {"atan2",           IMP_ATAN2,           CT_DOUBLE, 2, {CT_DOUBLE,CT_DOUBLE}},
    {"pow",             IMP_POW,             CT_DOUBLE, 2, {CT_DOUBLE,CT_DOUBLE}},
    {"exp",             IMP_EXP,             CT_DOUBLE, 1, {CT_DOUBLE}},
    {"log",             IMP_LOG,             CT_DOUBLE, 1, {CT_DOUBLE}},
    {"log2",            IMP_LOG2,            CT_DOUBLE, 1, {CT_DOUBLE}},
    {"fmod",            IMP_FMOD,            CT_DOUBLE, 2, {CT_DOUBLE,CT_DOUBLE}},
    {"get_epoch_ms",    IMP_GET_EPOCH_MS,    CT_LONG_LONG, 0, {}},
    {"get_uptime_ms",   IMP_GET_UPTIME_MS,   CT_LONG_LONG, 0, {}},
    {"get_last_comm_ms",IMP_GET_LAST_COMM_MS,CT_LONG_LONG, 0, {}},
    {"print_i64",       IMP_PRINT_I64,       CT_VOID,  1, {CT_LONG_LONG}},
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
    for (;;) {
        while (src_pos < src_len && source[src_pos] != '\n' && i < max - 1)
            buf[i++] = source[src_pos++];
        /* Check for line continuation: backslash before newline */
        if (i > 0 && buf[i-1] == '\\' && src_pos < src_len && source[src_pos] == '\n') {
            i--;  /* remove backslash */
            src_pos++;  /* skip newline */
            line_num++;
            /* Skip leading whitespace on continuation line */
            while (src_pos < src_len && (source[src_pos] == ' ' || source[src_pos] == '\t'))
                src_pos++;
            continue;
        }
        break;
    }
    /* Trim trailing whitespace */
    while (i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t')) i--;
    buf[i] = 0;
}

/* ================================================================
 *  #if constant expression evaluator
 *
 *  Recursive descent with full C operator precedence:
 *    ||  &&  |  ^  &  == !=  < > <= >=  << >>  + -  * / %
 *  Unary: ! ~ - +, defined(), integer literals, macro expansion.
 *  Undefined identifiers evaluate to 0 (per C standard).
 * ================================================================ */

static void pp_skip_ws(void) {
    while (src_pos < src_len && (source[src_pos] == ' ' || source[src_pos] == '\t'))
        src_pos++;
}

static long pp_primary(void);
static long pp_unary(void);
static long pp_expr(int min_prec);

static long pp_primary(void) {
    pp_skip_ws();
    if (src_pos >= src_len || source[src_pos] == '\n') return 0;

    /* Parenthesized expression */
    if (source[src_pos] == '(') {
        src_pos++;
        long val = pp_expr(0);
        pp_skip_ws();
        if (src_pos < src_len && source[src_pos] == ')') src_pos++;
        return val;
    }

    /* Character literal: 'x' or '\n' */
    if (source[src_pos] == '\'') {
        src_pos++;
        long val = 0;
        if (src_pos < src_len && source[src_pos] == '\\') {
            src_pos++;
            if (src_pos < src_len) {
                switch (source[src_pos]) {
                case 'n': val = '\n'; break;
                case 't': val = '\t'; break;
                case 'r': val = '\r'; break;
                case '0': val = '\0'; break;
                case '\\': val = '\\'; break;
                case '\'': val = '\''; break;
                default: val = source[src_pos]; break;
                }
                src_pos++;
            }
        } else if (src_pos < src_len) {
            val = (unsigned char)source[src_pos++];
        }
        if (src_pos < src_len && source[src_pos] == '\'') src_pos++;
        return val;
    }

    /* Integer literal (decimal, hex, octal) */
    if (isdigit(source[src_pos])) {
        char nbuf[64];
        int len = 0;
        if (source[src_pos] == '0' && src_pos + 1 < src_len &&
            (source[src_pos + 1] == 'x' || source[src_pos + 1] == 'X')) {
            nbuf[len++] = source[src_pos++];
            nbuf[len++] = source[src_pos++];
            while (src_pos < src_len && isxdigit(source[src_pos]) && len < 62)
                nbuf[len++] = source[src_pos++];
        } else {
            while (src_pos < src_len && isdigit(source[src_pos]) && len < 62)
                nbuf[len++] = source[src_pos++];
        }
        nbuf[len] = 0;
        /* Skip integer suffixes */
        while (src_pos < src_len && (source[src_pos] == 'u' || source[src_pos] == 'U' ||
               source[src_pos] == 'l' || source[src_pos] == 'L'))
            src_pos++;
        return strtol(nbuf, NULL, 0);
    }

    /* Identifier: defined(), macro name, or unknown (→ 0) */
    if (isalpha(source[src_pos]) || source[src_pos] == '_') {
        char name[64];
        int ni = 0;
        while (src_pos < src_len && is_ident_char(source[src_pos]) && ni < 63)
            name[ni++] = source[src_pos++];
        name[ni] = 0;

        if (strcmp(name, "defined") == 0) {
            pp_skip_ws();
            int has_paren = 0;
            if (src_pos < src_len && source[src_pos] == '(') { has_paren = 1; src_pos++; }
            char dname[64];
            read_pp_word(dname, sizeof(dname));
            if (has_paren) {
                pp_skip_ws();
                if (src_pos < src_len && source[src_pos] == ')') src_pos++;
            }
            return (find_sym_kind(dname, SYM_DEFINE) != NULL) ? 1 : 0;
        }

        /* Macro expansion */
        Symbol *mac = find_sym_kind(name, SYM_DEFINE);
        if (mac && mac->macro_val[0])
            return strtol(mac->macro_val, NULL, 0);
        return 0;  /* undefined identifiers → 0 per C standard */
    }

    return 0;
}

static long pp_unary(void) {
    pp_skip_ws();
    if (src_pos >= src_len || source[src_pos] == '\n') return 0;

    if (source[src_pos] == '!') { src_pos++; return !pp_unary(); }
    if (source[src_pos] == '~') { src_pos++; return ~pp_unary(); }
    if (source[src_pos] == '-') { src_pos++; return -pp_unary(); }
    if (source[src_pos] == '+') { src_pos++; return pp_unary(); }
    return pp_primary();
}

/* Operator precedence table (higher = tighter binding) */
static int pp_get_prec(void) {
    pp_skip_ws();
    if (src_pos >= src_len || source[src_pos] == '\n') return -1;
    char c = source[src_pos];
    char c2 = (src_pos + 1 < src_len) ? source[src_pos + 1] : 0;
    if (c == '|' && c2 == '|') return 1;
    if (c == '&' && c2 == '&') return 2;
    if (c == '|' && c2 != '|') return 3;
    if (c == '^')              return 4;
    if (c == '&' && c2 != '&') return 5;
    if (c == '=' && c2 == '=') return 6;
    if (c == '!' && c2 == '=') return 6;
    if (c == '<' && c2 == '<') return 8;
    if (c == '>' && c2 == '>') return 8;
    if (c == '<')              return 7;  /* < or <= */
    if (c == '>')              return 7;  /* > or >= */
    if (c == '+' || c == '-')  return 9;
    if (c == '*' || c == '/' || c == '%') return 10;
    return -1;
}

/* Read and consume the operator, return an ID */
#define PP_OP_OR     1
#define PP_OP_AND    2
#define PP_OP_BIT_OR 3
#define PP_OP_XOR    4
#define PP_OP_BIT_AND 5
#define PP_OP_EQ     6
#define PP_OP_NE     7
#define PP_OP_LT     8
#define PP_OP_GT     9
#define PP_OP_LE    10
#define PP_OP_GE    11
#define PP_OP_SHL   12
#define PP_OP_SHR   13
#define PP_OP_ADD   14
#define PP_OP_SUB   15
#define PP_OP_MUL   16
#define PP_OP_DIV   17
#define PP_OP_MOD   18

static int pp_read_op(void) {
    pp_skip_ws();
    char c = source[src_pos];
    char c2 = (src_pos + 1 < src_len) ? source[src_pos + 1] : 0;
    if (c == '|' && c2 == '|') { src_pos += 2; return PP_OP_OR; }
    if (c == '&' && c2 == '&') { src_pos += 2; return PP_OP_AND; }
    if (c == '|')              { src_pos += 1; return PP_OP_BIT_OR; }
    if (c == '^')              { src_pos += 1; return PP_OP_XOR; }
    if (c == '&')              { src_pos += 1; return PP_OP_BIT_AND; }
    if (c == '=' && c2 == '=') { src_pos += 2; return PP_OP_EQ; }
    if (c == '!' && c2 == '=') { src_pos += 2; return PP_OP_NE; }
    if (c == '<' && c2 == '<') { src_pos += 2; return PP_OP_SHL; }
    if (c == '>' && c2 == '>') { src_pos += 2; return PP_OP_SHR; }
    if (c == '<' && c2 == '=') { src_pos += 2; return PP_OP_LE; }
    if (c == '>' && c2 == '=') { src_pos += 2; return PP_OP_GE; }
    if (c == '<')              { src_pos += 1; return PP_OP_LT; }
    if (c == '>')              { src_pos += 1; return PP_OP_GT; }
    if (c == '+')              { src_pos += 1; return PP_OP_ADD; }
    if (c == '-')              { src_pos += 1; return PP_OP_SUB; }
    if (c == '*')              { src_pos += 1; return PP_OP_MUL; }
    if (c == '/')              { src_pos += 1; return PP_OP_DIV; }
    if (c == '%')              { src_pos += 1; return PP_OP_MOD; }
    return -1;
}

static long pp_apply(int op, long l, long r) {
    switch (op) {
    case PP_OP_OR:      return l || r;
    case PP_OP_AND:     return l && r;
    case PP_OP_BIT_OR:  return l | r;
    case PP_OP_XOR:     return l ^ r;
    case PP_OP_BIT_AND: return l & r;
    case PP_OP_EQ:      return l == r;
    case PP_OP_NE:      return l != r;
    case PP_OP_LT:      return l < r;
    case PP_OP_GT:      return l > r;
    case PP_OP_LE:      return l <= r;
    case PP_OP_GE:      return l >= r;
    case PP_OP_SHL:     return l << r;
    case PP_OP_SHR:     return l >> r;
    case PP_OP_ADD:     return l + r;
    case PP_OP_SUB:     return l - r;
    case PP_OP_MUL:     return l * r;
    case PP_OP_DIV:     return r ? l / r : 0;
    case PP_OP_MOD:     return r ? l % r : 0;
    default:            return 0;
    }
}

static long pp_expr(int min_prec) {
    long left = pp_unary();
    for (;;) {
        int prec = pp_get_prec();
        if (prec < min_prec) break;
        int op = pp_read_op();
        if (op < 0) break;
        long right = pp_expr(prec + 1);
        left = pp_apply(op, left, right);
    }
    return left;
}

/* Evaluate the rest of the current line as a #if expression */
static long pp_eval_if(void) {
    return pp_expr(0);
}

int preproc_line(void) {
    /* We're at '#', check if it's at the start of a logical line */
    src_pos++; /* skip '#' */

    /* Skip whitespace after # */
    while (src_pos < src_len && (source[src_pos] == ' ' || source[src_pos] == '\t'))
        src_pos++;

    char directive[32];
    read_pp_word(directive, sizeof(directive));

    /* Handle skip mode (any level is skipping) */
    if (preproc_skipping()) {
        /* Check if skip is due to outer levels (not just current) */
        int outer_skip = 0;
        for (int i = 0; i < ifdef_depth - 1; i++)
            if (ifdef_skip[i]) { outer_skip = 1; break; }

        if (strcmp(directive, "ifdef") == 0 || strcmp(directive, "ifndef") == 0 ||
            strcmp(directive, "if") == 0) {
            /* Nested #if in skipped section — push another skip level */
            if (ifdef_depth >= MAX_IFDEF_DEPTH) {
                ifdef_overflow++;
            } else {
                ifdef_skip[ifdef_depth] = 1;
                ifdef_taken[ifdef_depth] = 0;
                ifdef_had_else[ifdef_depth] = 0;
                ifdef_depth++;
            }
        } else if (strcmp(directive, "elif") == 0) {
            if (ifdef_depth > 0 && !outer_skip) {
                if (ifdef_had_else[ifdef_depth - 1])
                    error_at("#elif after #else");
                if (!ifdef_taken[ifdef_depth - 1]) {
                    long val = pp_eval_if();
                    if (val) {
                        ifdef_skip[ifdef_depth - 1] = 0;
                        ifdef_taken[ifdef_depth - 1] = 1;
                    }
                } else {
                    ifdef_skip[ifdef_depth - 1] = 1;
                }
            }
        } else if (strcmp(directive, "else") == 0) {
            if (ifdef_depth > 0 && !outer_skip) {
                if (ifdef_had_else[ifdef_depth - 1])
                    error_at("#else after #else");
                ifdef_had_else[ifdef_depth - 1] = 1;
                if (!ifdef_taken[ifdef_depth - 1]) {
                    ifdef_skip[ifdef_depth - 1] = 0;
                    ifdef_taken[ifdef_depth - 1] = 1;
                } else {
                    ifdef_skip[ifdef_depth - 1] = 1;
                }
            }
        } else if (strcmp(directive, "endif") == 0) {
            if (ifdef_overflow > 0) ifdef_overflow--;
            else if (ifdef_depth > 0) ifdef_depth--;
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

    if (strcmp(directive, "undef") == 0) {
        char name[64];
        read_pp_word(name, sizeof(name));
        Symbol *existing = find_sym_kind(name, SYM_DEFINE);
        if (existing) {
            /* Clear the name so it won't be found by find_sym_kind */
            existing->name[0] = 0;
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "ifdef") == 0) {
        char name[64];
        read_pp_word(name, sizeof(name));
        int defined = (find_sym_kind(name, SYM_DEFINE) != NULL);
        if (ifdef_depth >= MAX_IFDEF_DEPTH) {
            error_at("#ifdef too deeply nested");
            ifdef_overflow++;
        } else {
            ifdef_skip[ifdef_depth] = !defined;
            ifdef_taken[ifdef_depth] = defined;
            ifdef_had_else[ifdef_depth] = 0;
            ifdef_depth++;
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "ifndef") == 0) {
        char name[64];
        read_pp_word(name, sizeof(name));
        int defined = (find_sym_kind(name, SYM_DEFINE) != NULL);
        if (ifdef_depth >= MAX_IFDEF_DEPTH) {
            error_at("#ifndef too deeply nested");
            ifdef_overflow++;
        } else {
            ifdef_skip[ifdef_depth] = defined;
            ifdef_taken[ifdef_depth] = !defined;
            ifdef_had_else[ifdef_depth] = 0;
            ifdef_depth++;
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "if") == 0) {
        long val = pp_eval_if();

        if (ifdef_depth >= MAX_IFDEF_DEPTH) {
            error_at("#if too deeply nested");
            ifdef_overflow++;
        } else {
            ifdef_skip[ifdef_depth] = (val == 0);
            ifdef_taken[ifdef_depth] = (val != 0);
            ifdef_had_else[ifdef_depth] = 0;
            ifdef_depth++;
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "elif") == 0) {
        if (ifdef_depth > 0) {
            if (ifdef_had_else[ifdef_depth - 1])
                error_at("#elif after #else");
            /* We're in normal mode = current branch is active.
             * Mark as taken and start skipping. */
            ifdef_taken[ifdef_depth - 1] = 1;
            ifdef_skip[ifdef_depth - 1] = 1;
        } else {
            error_at("#elif without matching #if");
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "else") == 0) {
        if (ifdef_depth > 0) {
            if (ifdef_had_else[ifdef_depth - 1])
                error_at("#else after #else");
            ifdef_had_else[ifdef_depth - 1] = 1;
            if (ifdef_taken[ifdef_depth - 1]) {
                ifdef_skip[ifdef_depth - 1] = 1;
            } else {
                ifdef_skip[ifdef_depth - 1] = 0;
                ifdef_taken[ifdef_depth - 1] = 1;
            }
        } else {
            error_at("#else without matching #if/#ifdef");
        }
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "endif") == 0) {
        if (ifdef_overflow > 0) ifdef_overflow--;
        else if (ifdef_depth > 0) ifdef_depth--;
        else error_at("#endif without matching #if/#ifdef");
        skip_to_eol();
        return 1;
    }

    if (strcmp(directive, "error") == 0) {
        char msg[256];
        read_pp_value(msg, sizeof(msg));
        error_fmt("#error %s", msg);
        return 1;
    }

    if (strcmp(directive, "warning") == 0) {
        char msg[256];
        read_pp_value(msg, sizeof(msg));
        fprintf(stderr, "%s:%d: warning: #warning %s\n",
                src_file ? src_file : "<input>", line_num, msg);
        return 1;
    }

    /* Unknown directive — skip */
    skip_to_eol();
    return 1;
}
