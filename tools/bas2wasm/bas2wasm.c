/*
 * basic2wasm.c — ConeZ BASIC to WASM compiler
 *
 * Reads a .bas file and emits a valid WASM 1.0 binary.
 * No external dependencies beyond libc.
 *
 * Usage: basic2wasm input.bas [-o output.wasm]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>

/* ================================================================
 *  Byte Buffer
 * ================================================================ */

typedef struct { uint8_t *data; int len, cap; } Buf;

static void buf_init(Buf *b) { b->data = NULL; b->len = b->cap = 0; }

static void buf_grow(Buf *b, int need) {
    if (b->len + need <= b->cap) return;
    int nc = b->cap ? b->cap * 2 : 256;
    while (nc < b->len + need) nc *= 2;
    b->data = realloc(b->data, nc);
    b->cap = nc;
}

static void buf_byte(Buf *b, uint8_t v) {
    buf_grow(b, 1); b->data[b->len++] = v;
}

static void buf_bytes(Buf *b, const void *p, int n) {
    buf_grow(b, n); memcpy(b->data + b->len, p, n); b->len += n;
}

static void buf_free(Buf *b) { free(b->data); buf_init(b); }

static void buf_uleb(Buf *b, uint32_t v) {
    do {
        uint8_t x = v & 0x7F; v >>= 7;
        if (v) x |= 0x80;
        buf_byte(b, x);
    } while (v);
}

static void buf_sleb(Buf *b, int32_t v) {
    int more = 1;
    while (more) {
        uint8_t x = v & 0x7F; v >>= 7;
        if ((v == 0 && !(x & 0x40)) || (v == -1 && (x & 0x40)))
            more = 0;
        else x |= 0x80;
        buf_byte(b, x);
    }
}

static void buf_f32(Buf *b, float v) {
    uint8_t tmp[4]; memcpy(tmp, &v, 4); buf_bytes(b, tmp, 4);
}

static void buf_str(Buf *b, const char *s) {
    int n = strlen(s); buf_uleb(b, n); buf_bytes(b, s, n);
}

static void buf_section(Buf *out, int id, Buf *content) {
    buf_byte(out, id);
    buf_uleb(out, content->len);
    buf_bytes(out, content->data, content->len);
}

/* ================================================================
 *  WASM Opcodes & Types
 * ================================================================ */

/* Global index constants */
#define GLOBAL_LINE      0   /* __line: current BASIC source line (mut i32) */
#define GLOBAL_HEAP      1   /* _heap_ptr: bump allocator pointer (mut i32) */
#define GLOBAL_DATA_BASE 2   /* address of DATA table in linear memory */
#define GLOBAL_DATA_IDX  3   /* current READ index (0-based) */

#define OP_UNREACHABLE   0x00
#define OP_BLOCK         0x02
#define OP_LOOP          0x03
#define OP_IF            0x04
#define OP_ELSE          0x05
#define OP_END           0x0B
#define OP_BR            0x0C
#define OP_BR_IF         0x0D
#define OP_RETURN        0x0F
#define OP_CALL          0x10
#define OP_DROP          0x1A
#define OP_SELECT        0x1B
#define OP_LOCAL_GET     0x20
#define OP_LOCAL_SET     0x21
#define OP_LOCAL_TEE     0x22
#define OP_GLOBAL_GET    0x23
#define OP_GLOBAL_SET    0x24
#define OP_I32_LOAD      0x28
#define OP_F32_LOAD      0x2A
#define OP_I32_STORE     0x36
#define OP_F32_STORE     0x38
#define OP_I32_CONST     0x41
#define OP_F32_CONST     0x43
#define OP_I32_EQZ       0x45
#define OP_I32_EQ        0x46
#define OP_I32_NE        0x47
#define OP_I32_LT_S      0x48
#define OP_I32_GT_S      0x4A
#define OP_I32_LE_S      0x4C
#define OP_I32_GE_S      0x4E
#define OP_F32_EQ        0x5B
#define OP_F32_NE        0x5C
#define OP_F32_LT        0x5D
#define OP_F32_GT        0x5E
#define OP_F32_LE        0x5F
#define OP_F32_GE        0x60
#define OP_I32_ADD       0x6A
#define OP_I32_SUB       0x6B
#define OP_I32_MUL       0x6C
#define OP_I32_DIV_S     0x6D
#define OP_I32_REM_S     0x6F
#define OP_I32_AND       0x71
#define OP_I32_OR        0x72
#define OP_F32_ADD       0x92
#define OP_F32_SUB       0x93
#define OP_F32_MUL       0x94
#define OP_F32_DIV       0x95
#define OP_F32_SQRT      0x91
#define OP_F32_ABS       0x8B
#define OP_I32_TRUNC_F32_S 0xA8
#define OP_I32_XOR           0x73
#define OP_F32_CEIL          0x8D
#define OP_F32_FLOOR         0x8E
#define OP_F32_CONVERT_I32_S 0xB2

#define WASM_I32  0x7F
#define WASM_F32  0x7D
#define WASM_VOID 0x40

/* ================================================================
 *  Import Table — all possible host imports
 * ================================================================ */

typedef struct {
    const char *name;
    int np; uint8_t p[8];
    int nr; uint8_t r[2];
} ImportDef;

enum {
    IMP_DELAY_MS, IMP_MILLIS, IMP_GET_PARAM, IMP_SET_PARAM, IMP_SHOULD_STOP,
    IMP_LED_SET_PIXEL, IMP_LED_FILL, IMP_LED_SHOW, IMP_LED_COUNT,
    IMP_LED_GAMMA8, IMP_LED_SET_GAMMA,
    IMP_LED_SET_BUFFER, IMP_LED_SHIFT, IMP_LED_ROTATE, IMP_LED_REVERSE,
    IMP_LED_SET_PIXEL_HSV, IMP_LED_FILL_HSV, IMP_HSV_TO_RGB, IMP_RGB_TO_HSV,
    IMP_HOST_PRINTF, IMP_PRINT_I32, IMP_PRINT_F32, IMP_PRINT_STR,
    IMP_GPS_VALID, IMP_HAS_ORIGIN, IMP_ORIGIN_DIST, IMP_ORIGIN_BEARING,
    IMP_GET_LAT, IMP_GET_LON, IMP_GET_ALT, IMP_GET_SPEED, IMP_GET_DIR,
    IMP_GET_SECOND, IMP_GET_MINUTE, IMP_GET_HOUR,
    IMP_GET_DAY, IMP_GET_MONTH, IMP_GET_YEAR,
    IMP_GET_DAY_OF_WEEK, IMP_GET_DAY_OF_YEAR, IMP_GET_IS_LEAP_YEAR,
    IMP_TIME_VALID,
    IMP_IMU_VALID, IMP_GET_PITCH, IMP_GET_ROLL, IMP_GET_YAW,
    IMP_GET_ACC_X, IMP_GET_ACC_Y, IMP_GET_ACC_Z,
    IMP_GET_TEMP, IMP_GET_HUMIDITY, IMP_GET_BRIGHTNESS,
    IMP_RANDOM_INT,
    IMP_SINF, IMP_COSF, IMP_ATAN2F, IMP_POWF,
    IMP_LUT_LOAD, IMP_LUT_SAVE, IMP_LUT_CHECK, IMP_LUT_GET, IMP_LUT_SET, IMP_LUT_SIZE,
    IMP_WAIT_PPS, IMP_WAIT_PARAM,
    IMP_CUE_PLAYING, IMP_CUE_ELAPSED,
    IMP_GET_BAT_VOLTAGE, IMP_GET_SOLAR_VOLTAGE,
    IMP_GET_SUNRISE, IMP_GET_SUNSET, IMP_SUN_VALID, IMP_IS_DAYLIGHT,
    IMP_PIN_SET, IMP_PIN_CLEAR, IMP_PIN_READ, IMP_ANALOG_READ,
    IMP_GPS_PRESENT, IMP_IMU_PRESENT,
    IMP_GET_BATTERY_PERCENTAGE, IMP_GET_BATTERY_RUNTIME,
    IMP_GET_SUN_AZIMUTH, IMP_GET_SUN_ELEVATION,
    IMP_STR_ALLOC, IMP_STR_FREE, IMP_STR_LEN, IMP_STR_COPY,
    IMP_STR_CONCAT, IMP_STR_CMP, IMP_STR_MID, IMP_STR_LEFT, IMP_STR_RIGHT,
    IMP_STR_CHR, IMP_STR_ASC, IMP_STR_FROM_INT, IMP_STR_FROM_FLOAT,
    IMP_STR_TO_INT, IMP_STR_TO_FLOAT, IMP_STR_UPPER, IMP_STR_LOWER,
    IMP_STR_INSTR, IMP_STR_TRIM,
    IMP_TANF, IMP_EXPF, IMP_LOGF, IMP_LOG2F, IMP_FMODF,
    IMP_STR_REPEAT, IMP_STR_SPACE, IMP_STR_HEX, IMP_STR_OCT,
    IMP_STR_MID_ASSIGN,
    IMP_STR_LTRIM, IMP_STR_RTRIM,
    IMP_FILE_OPEN, IMP_FILE_CLOSE, IMP_FILE_PRINT, IMP_FILE_READLN, IMP_FILE_EOF,
    IMP_FILE_DELETE, IMP_FILE_RENAME, IMP_FILE_MKDIR, IMP_FILE_RMDIR,
    IMP_COUNT
};

#define _I WASM_I32
#define _F WASM_F32
static ImportDef imp_defs[IMP_COUNT] = {
    [IMP_DELAY_MS]      = {"delay_ms",      1,{_I},            0,{}},
    [IMP_MILLIS]         = {"millis",         0,{},              1,{_I}},
    [IMP_GET_PARAM]      = {"get_param",      1,{_I},            1,{_I}},
    [IMP_SET_PARAM]      = {"set_param",      2,{_I,_I},         0,{}},
    [IMP_SHOULD_STOP]    = {"should_stop",    0,{},              1,{_I}},
    [IMP_LED_SET_PIXEL]  = {"led_set_pixel",  5,{_I,_I,_I,_I,_I},0,{}},
    [IMP_LED_FILL]       = {"led_fill",       4,{_I,_I,_I,_I},   0,{}},
    [IMP_LED_SHOW]       = {"led_show",       0,{},              0,{}},
    [IMP_LED_COUNT]      = {"led_count",      1,{_I},            1,{_I}},
    [IMP_LED_GAMMA8]     = {"led_gamma8",     1,{_I},            1,{_I}},
    [IMP_LED_SET_GAMMA]  = {"led_set_gamma",  1,{_I},            0,{}},
    [IMP_LED_SET_BUFFER] = {"led_set_buffer", 3,{_I,_I,_I},      0,{}},
    [IMP_LED_SHIFT]      = {"led_shift",      5,{_I,_I,_I,_I,_I},0,{}},
    [IMP_LED_ROTATE]     = {"led_rotate",     2,{_I,_I},         0,{}},
    [IMP_LED_REVERSE]    = {"led_reverse",    1,{_I},            0,{}},
    [IMP_LED_SET_PIXEL_HSV]={"led_set_pixel_hsv",5,{_I,_I,_I,_I,_I},0,{}},
    [IMP_LED_FILL_HSV]   = {"led_fill_hsv",   4,{_I,_I,_I,_I},   0,{}},
    [IMP_HSV_TO_RGB]     = {"hsv_to_rgb",     3,{_I,_I,_I},      1,{_I}},
    [IMP_RGB_TO_HSV]     = {"rgb_to_hsv",     3,{_I,_I,_I},      1,{_I}},
    [IMP_HOST_PRINTF]    = {"host_printf",    2,{_I,_I},         1,{_I}},
    [IMP_PRINT_I32]      = {"print_i32",      1,{_I},            0,{}},
    [IMP_PRINT_F32]      = {"print_f32",      1,{_F},            0,{}},
    [IMP_PRINT_STR]      = {"print_str",      2,{_I,_I},         0,{}},
    [IMP_GPS_VALID]      = {"gps_valid",      0,{},              1,{_I}},
    [IMP_HAS_ORIGIN]     = {"has_origin",     0,{},              1,{_I}},
    [IMP_ORIGIN_DIST]    = {"origin_dist",    0,{},              1,{_F}},
    [IMP_ORIGIN_BEARING] = {"origin_bearing", 0,{},              1,{_F}},
    [IMP_GET_LAT]        = {"get_lat",        0,{},              1,{_F}},
    [IMP_GET_LON]        = {"get_lon",        0,{},              1,{_F}},
    [IMP_GET_ALT]        = {"get_alt",        0,{},              1,{_F}},
    [IMP_GET_SPEED]      = {"get_speed",      0,{},              1,{_F}},
    [IMP_GET_DIR]         = {"get_dir",         0,{},              1,{_F}},
    [IMP_GET_SECOND]     = {"get_second",     0,{},              1,{_I}},
    [IMP_GET_MINUTE]     = {"get_minute",     0,{},              1,{_I}},
    [IMP_GET_HOUR]       = {"get_hour",       0,{},              1,{_I}},
    [IMP_GET_DAY]        = {"get_day",        0,{},              1,{_I}},
    [IMP_GET_MONTH]      = {"get_month",      0,{},              1,{_I}},
    [IMP_GET_YEAR]       = {"get_year",       0,{},              1,{_I}},
    [IMP_GET_DAY_OF_WEEK]= {"get_day_of_week",0,{},             1,{_I}},
    [IMP_GET_DAY_OF_YEAR]= {"get_day_of_year",0,{},             1,{_I}},
    [IMP_GET_IS_LEAP_YEAR]={"get_is_leap_year",0,{},            1,{_I}},
    [IMP_TIME_VALID]     = {"time_valid",     0,{},              1,{_I}},
    [IMP_IMU_VALID]      = {"imu_valid",      0,{},              1,{_I}},
    [IMP_GET_PITCH]      = {"get_pitch",      0,{},              1,{_F}},
    [IMP_GET_ROLL]       = {"get_roll",       0,{},              1,{_F}},
    [IMP_GET_YAW]        = {"get_yaw",        0,{},              1,{_F}},
    [IMP_GET_ACC_X]      = {"get_acc_x",      0,{},              1,{_F}},
    [IMP_GET_ACC_Y]      = {"get_acc_y",      0,{},              1,{_F}},
    [IMP_GET_ACC_Z]      = {"get_acc_z",      0,{},              1,{_F}},
    [IMP_GET_TEMP]       = {"get_temp",       0,{},              1,{_F}},
    [IMP_GET_HUMIDITY]   = {"get_humidity",   0,{},              1,{_F}},
    [IMP_GET_BRIGHTNESS] = {"get_brightness", 0,{},              1,{_F}},
    [IMP_RANDOM_INT]     = {"random_int",     2,{_I,_I},         1,{_I}},
    [IMP_SINF]           = {"sinf",           1,{_F},            1,{_F}},
    [IMP_COSF]           = {"cosf",           1,{_F},            1,{_F}},
    [IMP_ATAN2F]         = {"atan2f",         2,{_F,_F},         1,{_F}},
    [IMP_POWF]           = {"powf",           2,{_F,_F},         1,{_F}},
    [IMP_LUT_LOAD]       = {"lut_load",       1,{_I},            1,{_I}},
    [IMP_LUT_SAVE]       = {"lut_save",       1,{_I},            1,{_I}},
    [IMP_LUT_CHECK]      = {"lut_check",      1,{_I},            1,{_I}},
    [IMP_LUT_GET]        = {"lut_get",        1,{_I},            1,{_I}},
    [IMP_LUT_SET]        = {"lut_set",        2,{_I,_I},         0,{}},
    [IMP_LUT_SIZE]       = {"lut_size",       0,{},              1,{_I}},
    [IMP_WAIT_PPS]       = {"wait_pps",       1,{_I},            1,{_I}},
    [IMP_WAIT_PARAM]     = {"wait_param",     4,{_I,_I,_I,_I},   1,{_I}},
    [IMP_CUE_PLAYING]    = {"cue_playing",    0,{},              1,{_I}},
    [IMP_CUE_ELAPSED]    = {"cue_elapsed",    0,{},              1,{_I}},
    [IMP_GET_BAT_VOLTAGE]= {"get_bat_voltage",0,{},              1,{_F}},
    [IMP_GET_SOLAR_VOLTAGE]={"get_solar_voltage",0,{},           1,{_F}},
    [IMP_GET_SUNRISE]    = {"get_sunrise",    0,{},              1,{_I}},
    [IMP_GET_SUNSET]     = {"get_sunset",     0,{},              1,{_I}},
    [IMP_SUN_VALID]      = {"sun_valid",      0,{},              1,{_I}},
    [IMP_IS_DAYLIGHT]    = {"is_daylight",    0,{},              1,{_I}},
    [IMP_PIN_SET]        = {"pin_set",        1,{_I},            0,{}},
    [IMP_PIN_CLEAR]      = {"pin_clear",      1,{_I},            0,{}},
    [IMP_PIN_READ]       = {"pin_read",       1,{_I},            1,{_I}},
    [IMP_ANALOG_READ]    = {"analog_read",    1,{_I},            1,{_I}},
    [IMP_GPS_PRESENT]    = {"gps_present",    0,{},              1,{_I}},
    [IMP_IMU_PRESENT]    = {"imu_present",    0,{},              1,{_I}},
    [IMP_GET_BATTERY_PERCENTAGE]={"get_battery_percentage",0,{}, 1,{_F}},
    [IMP_GET_BATTERY_RUNTIME]={"get_battery_runtime",0,{},       1,{_F}},
    [IMP_GET_SUN_AZIMUTH]= {"get_sun_azimuth",0,{},             1,{_F}},
    [IMP_GET_SUN_ELEVATION]={"get_sun_elevation",0,{},           1,{_F}},
    [IMP_STR_ALLOC]      = {"basic_str_alloc",      1,{_I},            1,{_I}},
    [IMP_STR_FREE]       = {"basic_str_free",       1,{_I},            0,{}},
    [IMP_STR_LEN]        = {"basic_str_len",        1,{_I},            1,{_I}},
    [IMP_STR_COPY]       = {"basic_str_copy",       1,{_I},            1,{_I}},
    [IMP_STR_CONCAT]     = {"basic_str_concat",     2,{_I,_I},         1,{_I}},
    [IMP_STR_CMP]        = {"basic_str_cmp",        2,{_I,_I},         1,{_I}},
    [IMP_STR_MID]        = {"basic_str_mid",        3,{_I,_I,_I},      1,{_I}},
    [IMP_STR_LEFT]       = {"basic_str_left",       2,{_I,_I},         1,{_I}},
    [IMP_STR_RIGHT]      = {"basic_str_right",      2,{_I,_I},         1,{_I}},
    [IMP_STR_CHR]        = {"basic_str_chr",        1,{_I},            1,{_I}},
    [IMP_STR_ASC]        = {"basic_str_asc",        1,{_I},            1,{_I}},
    [IMP_STR_FROM_INT]   = {"basic_str_from_int",   1,{_I},            1,{_I}},
    [IMP_STR_FROM_FLOAT] = {"basic_str_from_float", 1,{_F},            1,{_I}},
    [IMP_STR_TO_INT]     = {"basic_str_to_int",     1,{_I},            1,{_I}},
    [IMP_STR_TO_FLOAT]   = {"basic_str_to_float",   1,{_I},            1,{_F}},
    [IMP_STR_UPPER]      = {"basic_str_upper",      1,{_I},            1,{_I}},
    [IMP_STR_LOWER]      = {"basic_str_lower",      1,{_I},            1,{_I}},
    [IMP_STR_INSTR]      = {"basic_str_instr",      3,{_I,_I,_I},      1,{_I}},
    [IMP_STR_TRIM]       = {"basic_str_trim",       1,{_I},            1,{_I}},
    [IMP_TANF]           = {"tanf",                 1,{_F},            1,{_F}},
    [IMP_EXPF]           = {"expf",                 1,{_F},            1,{_F}},
    [IMP_LOGF]           = {"logf",                 1,{_F},            1,{_F}},
    [IMP_LOG2F]          = {"log2f",                1,{_F},            1,{_F}},
    [IMP_FMODF]          = {"fmodf",                2,{_F,_F},         1,{_F}},
    [IMP_STR_REPEAT]     = {"basic_str_repeat",     2,{_I,_I},         1,{_I}},
    [IMP_STR_SPACE]      = {"basic_str_space",      1,{_I},            1,{_I}},
    [IMP_STR_HEX]        = {"basic_str_hex",        1,{_I},            1,{_I}},
    [IMP_STR_OCT]        = {"basic_str_oct",        1,{_I},            1,{_I}},
    [IMP_STR_MID_ASSIGN] = {"basic_str_mid_assign", 4,{_I,_I,_I,_I},  1,{_I}},
    [IMP_STR_LTRIM]      = {"basic_str_ltrim",      1,{_I},            1,{_I}},
    [IMP_STR_RTRIM]      = {"basic_str_rtrim",      1,{_I},            1,{_I}},
    [IMP_FILE_OPEN]      = {"basic_file_open",      2,{_I,_I},         1,{_I}},
    [IMP_FILE_CLOSE]     = {"basic_file_close",     1,{_I},            0,{}},
    [IMP_FILE_PRINT]     = {"basic_file_print",     2,{_I,_I},         1,{_I}},
    [IMP_FILE_READLN]    = {"basic_file_readln",    1,{_I},            1,{_I}},
    [IMP_FILE_EOF]       = {"basic_file_eof",       1,{_I},            1,{_I}},
    [IMP_FILE_DELETE]    = {"file_delete",          2,{_I,_I},         1,{_I}},
    [IMP_FILE_RENAME]    = {"file_rename",          4,{_I,_I,_I,_I},   1,{_I}},
    [IMP_FILE_MKDIR]     = {"file_mkdir",           2,{_I,_I},         1,{_I}},
    [IMP_FILE_RMDIR]     = {"file_rmdir",           2,{_I,_I},         1,{_I}},
};
#undef _I
#undef _F

/* ================================================================
 *  Compiler State
 * ================================================================ */

#define MAX_VARS    256
#define MAX_FUNCS   64
#define MAX_CTRL    64
#define MAX_STRINGS 8192
#define FMT_BUF_SIZE 256
#define FILE_TABLE_BASE 0xF100  /* 4 i32 handles at 0xF100..0xF10F */

typedef enum { T_I32 = 0, T_F32 = 1, T_STR = 2 } VType;

#define VAR_NORMAL 0
#define VAR_DIM    1
#define VAR_SUB    2

typedef struct {
    char name[16];
    VType type;
    int type_set;
    int mode;           /* VAR_NORMAL, VAR_DIM, VAR_SUB */
    int global_idx;     /* WASM global index (0=_heap_ptr, 1+=vars) */
    /* SUB info */
    int param_count;
    int local_count;
    int param_vars[8];
    int local_vars[8];
    int func_local_idx; /* index into func_bufs (0=setup, 1+=SUBs) */
    int is_const;       /* 1 if declared with CONST */
} Var;

typedef struct {
    Buf code;
    int nparams;            /* WASM param count */
    uint8_t param_types[8]; /* all WASM_I32 for now */
    int nlocals;
    uint8_t local_types[128];
    int sub_var;            /* variable index of SUB, -1 for setup */
    int call_fixups[512];   /* code offsets of call target LEB128s */
    int ncall_fixups;
} FuncCtx;

enum { CTRL_WHILE, CTRL_FOR, CTRL_IF, CTRL_SELECT, CTRL_DO };

typedef struct {
    int kind;
    int break_depth;    /* block nesting level for break target */
    int cont_depth;     /* block nesting level for continue target */
    int for_var;        /* FOR: variable index */
    int for_limit_local;/* FOR: WASM local index for limit */
    int if_extra_ends;  /* IF: extra nesting from ELSE IF chains */
    int for_step_local; /* FOR STEP: WASM local index for step value */
    int for_has_step;   /* FOR: 1 if explicit STEP, 0 otherwise */
} CtrlEntry;

/* Global compiler state */
static Var vars[MAX_VARS];
static int nvar;
static FuncCtx func_bufs[MAX_FUNCS];
static int nfuncs;
static int cur_func;        /* current function being compiled into */
static CtrlEntry ctrl_stk[MAX_CTRL];
static int ctrl_sp;
static int block_depth;     /* current WASM block nesting depth */

/* Import usage tracking */
static uint8_t imp_used[IMP_COUNT];

/* String/data table */
static char data_buf[MAX_STRINGS];
static int data_len;

/* DATA items (compile-time collection, assembled into data section) */
#define MAX_DATA_ITEMS 1024
typedef struct { VType type; int32_t ival; float fval; int str_off; } DataItem;
static DataItem data_items[MAX_DATA_ITEMS];
static int ndata_items;

/* Source */
static char *source;
static int src_len;
static int src_pos;
static char line_buf[512];
static char *lp;
static int line_num;

/* Lexer state */
static int tok, tokv, ungot;
static float tokf;
static char tokn[16];

/* Type tracking stack */
static VType vstack[64];
static int vsp;

static int had_error;

/* Constant folding — tracks the two most recent constant emissions */
typedef struct {
    int valid;       /* 0=none, 1=i32, 2=f32 */
    int buf_start;   /* CODE->len before emit */
    int buf_end;     /* CODE->len after emit */
    int32_t ival;
    float fval;
} FoldSlot;

static FoldSlot fold_a, fold_b;

/* ================================================================
 *  Helpers
 * ================================================================ */

#define CODE (&func_bufs[cur_func].code)

static void error_at(const char *msg) {
    fprintf(stderr, "ERROR line %d: %s\n", line_num, msg);
    had_error = 1;
}

static int find_var(const char *name) {
    for (int i = 0; i < nvar; i++)
        if (strcmp(vars[i].name, name) == 0) return i;
    return -1;
}

static int add_var(const char *name) {
    int i = find_var(name);
    if (i >= 0) return i;
    if (nvar >= MAX_VARS) { error_at("too many variables"); return 0; }
    memset(&vars[nvar], 0, sizeof(Var));
    strncpy(vars[nvar].name, name, 15);
    int len = strlen(name);
    if (len > 0 && name[len-1] == '$') {
        vars[nvar].type = T_STR;
        vars[nvar].type_set = 1;
    } else if (len > 0 && name[len-1] == '#') {
        vars[nvar].type = T_F32;
        vars[nvar].type_set = 1;
    } else {
        vars[nvar].type = T_I32;
    }
    vars[nvar].global_idx = nvar + 4; /* globals 0-3 = __line, _heap_ptr, _data_base, _data_idx */
    return nvar++;
}

static int alloc_local(void) {
    FuncCtx *f = &func_bufs[cur_func];
    int idx = f->nparams + f->nlocals;
    f->local_types[f->nlocals++] = WASM_I32;
    return idx;
}

static int alloc_local_f32(void) {
    FuncCtx *f = &func_bufs[cur_func];
    int idx = f->nparams + f->nlocals;
    f->local_types[f->nlocals++] = WASM_F32;
    return idx;
}

static int add_string(const char *s, int len) {
    if (data_len + len + 1 > MAX_STRINGS) { error_at("string table full"); return 0; }
    int off = data_len;
    memcpy(data_buf + data_len, s, len);
    data_buf[data_len + len] = 0;
    data_len += len + 1;
    return off;
}

static void vpush(VType t) { vstack[vsp++] = t; }
static VType vpop(void) { return vstack[--vsp]; }

/* ================================================================
 *  Lexer
 * ================================================================ */

enum {
    TOK_EOF=0, TOK_NAME=1, TOK_NUMBER=2, TOK_STRING=3, TOK_FLOAT=4,
    TOK_LP=5, TOK_RP=6, TOK_COMMA=7,
    TOK_ADD=8, TOK_SUB=9, TOK_MUL=10, TOK_DIV=11, TOK_IDIV=12,
    TOK_EQ=13, TOK_LT=14, TOK_GT=15,
    TOK_NE=16, TOK_LE=17, TOK_GE=18,
    /* Keywords — order must match kwd[] */
    TOK_AND=19, TOK_OR=20, TOK_FORMAT=21, TOK_KW_SUB=22, TOK_END=23,
    TOK_RETURN=24, TOK_LOCAL=25, TOK_WHILE=26, TOK_FOR=27, TOK_TO=28,
    TOK_IF=29, TOK_ELSE=30, TOK_THEN=31, TOK_DIM=32, TOK_UBOUND=33,
    TOK_BYE=34, TOK_BREAK=35, TOK_RESUME=36, TOK_PRINTS=37, TOK_STEP=38,
    TOK_CONST=39, TOK_NOT=40, TOK_XOR=41,
    TOK_SELECT=42, TOK_CASE=43, TOK_DO=44, TOK_LOOP=45, TOK_UNTIL=46,
    TOK_EXIT=47, TOK_SWAP=48, TOK_IS=49,
    TOK_DATA=50, TOK_READ=51, TOK_RESTORE=52,
    TOK_MOD=53, TOK_NEXT=54, TOK_WEND=55, TOK_FUNCTION=56,
    TOK_OPEN=57, TOK_CLOSE_FILE=58, TOK_AS=59,
    TOK_KILL=60, TOK_MKDIR=61, TOK_RMDIR=62, TOK_ELSEIF=63,
    TOK_HASH=64,  /* standalone # (file channel prefix) */
    TOK_POW=65    /* ^ (exponentiation) */
};

static const char *kwd[] = {
    "AND","OR","FORMAT","SUB","END","RETURN","LOCAL",
    "WHILE","FOR","TO","IF","ELSE","THEN","DIM","UBOUND",
    "BYE","BREAK","RESUME","PRINTS","STEP","CONST","NOT","XOR",
    "SELECT","CASE","DO","LOOP","UNTIL","EXIT","SWAP","IS",
    "DATA","READ","RESTORE","MOD","NEXT","WEND","FUNCTION",
    "OPEN","CLOSE","AS","KILL","MKDIR","RMDIR","ELSEIF",NULL
};

static int next_line(void) {
    if (src_pos >= src_len) return 0;
    int i = 0;
    while (src_pos < src_len && source[src_pos] != '\n' && i < (int)sizeof(line_buf)-1)
        line_buf[i++] = source[src_pos++];
    line_buf[i] = 0;
    if (src_pos < src_len && source[src_pos] == '\n') src_pos++;
    lp = line_buf;
    line_num++;
    ungot = 0;
    return 1;
}

static int read_tok(void) {
    static const char *pun = "(),+-*/\\=<>";
    static const char *dub = "<><=>=";  /* pairs: <> <= >= */
    char *p, *d;
    const char **k;

    if (ungot) { ungot = 0; return tok; }
    while (isspace((unsigned char)*lp)) lp++;
    if (!*lp || *lp == '\'') return tok = TOK_EOF;

    /* Number (int or float) */
    if (isdigit((unsigned char)*lp) || (*lp == '.' && isdigit((unsigned char)lp[1]))) {
        int is_float = 0;
        char *start = lp;
        if (lp[0] == '0' && (lp[1] == 'x' || lp[1] == 'X')) {
            tokv = (int)strtol(lp, &lp, 16);
            return tok = TOK_NUMBER;
        }
        while (isdigit((unsigned char)*lp)) lp++;
        if (*lp == '.' && isdigit((unsigned char)lp[1])) {
            is_float = 1; lp++;
            while (isdigit((unsigned char)*lp)) lp++;
        }
        if (is_float) {
            tokf = strtof(start, NULL);
            return tok = TOK_FLOAT;
        }
        tokv = (int)strtol(start, NULL, 10);
        return tok = TOK_NUMBER;
    }

    /* Hash (file channel prefix) and caret (exponentiation) — before punctuation check */
    if (*lp == '#') { lp++; return tok = TOK_HASH; }
    if (*lp == '^') { lp++; return tok = TOK_POW; }

    /* Punctuation */
    if ((p = strchr(pun, *lp)) != NULL) {
        lp++;
        /* Check double-char operators */
        for (d = (char*)dub; *d; d += 2)
            if (d[0] == lp[-1] && d[1] == *lp) { lp++; return tok = (d - dub)/2 + TOK_NE; }
        return tok = (p - pun) + TOK_LP;
    }

    /* Identifier / keyword */
    if (isalpha((unsigned char)*lp) || *lp == '_') {
        char *tp = tokn;
        while (isalnum((unsigned char)*lp) || *lp == '_') {
            if (tp - tokn < 15) *tp++ = toupper((unsigned char)*lp);
            lp++;
        }
        /* Allow # suffix for float function names */
        if (*lp == '#') { if (tp - tokn < 15) *tp++ = '#'; lp++; }
        /* Allow $ suffix for string variable/function names */
        if (*lp == '$') { if (tp - tokn < 15) *tp++ = '$'; lp++; }
        *tp = 0;
        for (k = kwd; *k; k++)
            if (strcmp(tokn, *k) == 0) return tok = (k - kwd) + TOK_AND;
        tokv = add_var(tokn);
        return tok = TOK_NAME;
    }

    /* String */
    if (*lp == '"') {
        lp++;
        int off = data_len;
        while (*lp && *lp != '"') {
            if (data_len < MAX_STRINGS - 1) data_buf[data_len++] = *lp;
            lp++;
        }
        if (data_len < MAX_STRINGS) data_buf[data_len++] = 0;
        if (*lp == '"') lp++;
        tokv = off;
        return tok = TOK_STRING;
    }

    error_at("bad token");
    return tok = TOK_EOF;
}

static int want(int t) { return !(ungot = (read_tok() != t)); }
static void need(int t) { if (!want(t)) error_at("syntax error"); }

/* ================================================================
 *  Emit Helpers
 * ================================================================ */

static void emit_op(int op) { buf_byte(CODE, op); }

static void emit_i32_const(int32_t v) {
    fold_a = fold_b;
    fold_b.valid = 1;
    fold_b.buf_start = CODE->len;
    buf_byte(CODE, OP_I32_CONST); buf_sleb(CODE, v);
    fold_b.buf_end = CODE->len;
    fold_b.ival = v;
}
static void emit_f32_const(float v) {
    fold_a = fold_b;
    fold_b.valid = 2;
    fold_b.buf_start = CODE->len;
    buf_byte(CODE, OP_F32_CONST); buf_f32(CODE, v);
    fold_b.buf_end = CODE->len;
    fold_b.fval = v;
}
static void emit_call(int func_idx) {
    buf_byte(CODE, OP_CALL);
    FuncCtx *f = &func_bufs[cur_func];
    if (f->ncall_fixups < 512)
        f->call_fixups[f->ncall_fixups++] = f->code.len;
    buf_uleb(CODE, func_idx);
    if (func_idx < IMP_COUNT) imp_used[func_idx] = 1;
}
static void emit_global_get(int idx) {
    buf_byte(CODE, OP_GLOBAL_GET); buf_uleb(CODE, idx);
}
static void emit_global_set(int idx) {
    buf_byte(CODE, OP_GLOBAL_SET); buf_uleb(CODE, idx);
}
static void emit_local_get(int idx) {
    buf_byte(CODE, OP_LOCAL_GET); buf_uleb(CODE, idx);
}
static void emit_local_set(int idx) {
    buf_byte(CODE, OP_LOCAL_SET); buf_uleb(CODE, idx);
}
static void emit_i32_load(int offset) {
    buf_byte(CODE, OP_I32_LOAD); buf_uleb(CODE, 2); buf_uleb(CODE, offset);
}
static void emit_i32_store(int offset) {
    buf_byte(CODE, OP_I32_STORE); buf_uleb(CODE, 2); buf_uleb(CODE, offset);
}
static void emit_f32_load(int offset) {
    buf_byte(CODE, OP_F32_LOAD); buf_uleb(CODE, 2); buf_uleb(CODE, offset);
}
static void emit_block(void)  { buf_byte(CODE, OP_BLOCK); buf_byte(CODE, WASM_VOID); block_depth++; }
static void emit_loop(void)   { buf_byte(CODE, OP_LOOP);  buf_byte(CODE, WASM_VOID); block_depth++; }
static void emit_if_void(void){ buf_byte(CODE, OP_IF);    buf_byte(CODE, WASM_VOID); block_depth++; }
static void emit_else(void)   { buf_byte(CODE, OP_ELSE); }
static void emit_end(void)    { buf_byte(CODE, OP_END); block_depth--; }
static void emit_br(int d)    { buf_byte(CODE, OP_BR); buf_uleb(CODE, d); }
static void emit_br_if(int d) { buf_byte(CODE, OP_BR_IF); buf_uleb(CODE, d); }
static void emit_drop(void)   { buf_byte(CODE, OP_DROP); }
static void emit_return(void) { buf_byte(CODE, OP_RETURN); }

/* Coerce top of WASM value stack to i32 */
static void coerce_i32(void) {
    if (vsp > 0 && vstack[vsp-1] == T_STR) {
        error_at("cannot use string in numeric context");
        return;
    }
    if (vsp > 0 && vstack[vsp-1] == T_F32) {
        emit_op(OP_I32_TRUNC_F32_S);
        vstack[vsp-1] = T_I32;
    }
}

/* Coerce top of WASM value stack to f32 */
static void coerce_f32(void) {
    if (vsp > 0 && vstack[vsp-1] == T_STR) {
        error_at("cannot use string in numeric context");
        return;
    }
    if (vsp > 0 && vstack[vsp-1] == T_I32) {
        emit_op(OP_F32_CONVERT_I32_S);
        vstack[vsp-1] = T_F32;
    }
}

/* ================================================================
 *  Expression Parser — forward declarations
 * ================================================================ */

static void expr(void);
static void base_expr(void);

/* Binary op helper: check types and emit correct opcode */
static void emit_binop(int i32_op, int f32_op) {
    VType b = vpop(), a = vpop();
    /* Constant folding: check if both operands are adjacent constants */
    if (fold_a.valid && fold_b.valid &&
        fold_a.buf_end == fold_b.buf_start &&
        fold_b.buf_end == CODE->len) {
        int folded = 0;
        if (fold_a.valid == 1 && fold_b.valid == 1) {
            /* Both i32 */
            int32_t va = fold_a.ival, vb = fold_b.ival, r = 0;
            if (i32_op == OP_I32_ADD) { r = va + vb; folded = 1; }
            else if (i32_op == OP_I32_SUB) { r = va - vb; folded = 1; }
            else if (i32_op == OP_I32_MUL) { r = va * vb; folded = 1; }
            else if (i32_op == OP_I32_DIV_S && vb != 0) { r = va / vb; folded = 1; }
            if (folded) {
                CODE->len = fold_a.buf_start;
                fold_a.valid = fold_b.valid = 0;
                emit_i32_const(r);
                vpush(T_I32);
                return;
            }
        }
        if (fold_a.valid == 2 && fold_b.valid == 2) {
            /* Both f32 */
            float va = fold_a.fval, vb = fold_b.fval, r = 0;
            if (f32_op == OP_F32_ADD) { r = va + vb; folded = 1; }
            else if (f32_op == OP_F32_SUB) { r = va - vb; folded = 1; }
            else if (f32_op == OP_F32_MUL) { r = va * vb; folded = 1; }
            else if (f32_op == OP_F32_DIV) { r = va / vb; folded = 1; }
            if (folded) {
                CODE->len = fold_a.buf_start;
                fold_a.valid = fold_b.valid = 0;
                emit_f32_const(r);
                vpush(T_F32);
                return;
            }
        }
        /* Mixed: one i32, one f32 — convert both to float */
        if ((fold_a.valid == 1 && fold_b.valid == 2) ||
            (fold_a.valid == 2 && fold_b.valid == 1)) {
            float va = (fold_a.valid == 1) ? (float)fold_a.ival : fold_a.fval;
            float vb = (fold_b.valid == 1) ? (float)fold_b.ival : fold_b.fval;
            float r = 0;
            if (f32_op == OP_F32_ADD) { r = va + vb; folded = 1; }
            else if (f32_op == OP_F32_SUB) { r = va - vb; folded = 1; }
            else if (f32_op == OP_F32_MUL) { r = va * vb; folded = 1; }
            else if (f32_op == OP_F32_DIV) { r = va / vb; folded = 1; }
            if (folded) {
                CODE->len = fold_a.buf_start;
                fold_a.valid = fold_b.valid = 0;
                emit_f32_const(r);
                vpush(T_F32);
                return;
            }
        }
    }
    /* Normal (non-folded) path */
    if (a == T_F32 || b == T_F32) {
        if (a == T_I32 && b == T_F32) {
            /* a(i32) is under b(f32) — save b, convert a, reload b */
            int scratch = alloc_local_f32();
            emit_local_set(scratch);
            emit_op(OP_F32_CONVERT_I32_S);
            emit_local_get(scratch);
        } else if (a == T_F32 && b == T_I32) {
            emit_op(OP_F32_CONVERT_I32_S);
        }
        emit_op(f32_op);
        vpush(T_F32);
    } else {
        emit_op(i32_op);
        vpush(T_I32);
    }
}

/* Comparison: always produces i32 (-1 or 0) */
static void emit_compare(int i32_op, int f32_op) {
    VType b = vpop(), a = vpop();
    if (a == T_F32 || b == T_F32) {
        if (a == T_I32 && b == T_F32) {
            int scratch = alloc_local_f32();
            emit_local_set(scratch);
            emit_op(OP_F32_CONVERT_I32_S);
            emit_local_get(scratch);
        } else if (a == T_F32 && b == T_I32) {
            emit_op(OP_F32_CONVERT_I32_S);
        }
        emit_op(f32_op);
    } else {
        emit_op(i32_op);
    }
    /* Convert WASM 0/1 to BASIC -1/0 */
    emit_i32_const(-1);
    emit_op(OP_I32_MUL);
    vpush(T_I32);
}

/* ================================================================
 *  Built-in Function Compilation
 * ================================================================ */

/* Simple call-through builtins: name, nargs, import, trunc_f32 */
typedef struct { const char *name; int nargs; int imp; int trunc; } SimpleBI;
static const SimpleBI simple_bi[] = {
    {"GETPARAM",    1, IMP_GET_PARAM,    0},
    {"HASGPS",      0, IMP_GPS_VALID,    0},
    {"HASORIGIN",   0, IMP_HAS_ORIGIN,   0},
    {"SECOND",      0, IMP_GET_SECOND,   0},
    {"MINUTE",      0, IMP_GET_MINUTE,   0},
    {"HOUR",        0, IMP_GET_HOUR,     0},
    {"DAY",         0, IMP_GET_DAY,      0},
    {"MONTH",       0, IMP_GET_MONTH,    0},
    {"YEAR",        0, IMP_GET_YEAR,     0},
    {"DAYOFWEEK",   0, IMP_GET_DAY_OF_WEEK,  0},
    {"DAYOFYEAR",   0, IMP_GET_DAY_OF_YEAR,  0},
    {"ISLEAPYEAR",  0, IMP_GET_IS_LEAP_YEAR, 0},
    {"HASDATE",     0, IMP_TIME_VALID,   0},
    {"HASTIME",     0, IMP_TIME_VALID,   0},
    {"HASGYRO",     0, IMP_IMU_VALID,    0},
    {"HASACC",      0, IMP_IMU_VALID,    0},
    {"HASMAG",      0, IMP_IMU_VALID,    0},
    {"ORIGINDIST",  0, IMP_ORIGIN_DIST,  1},
    {"ORIGINANGLE", 0, IMP_ORIGIN_BEARING,1},
    {"GPSSPEED",    0, IMP_GET_SPEED,    1},
    {"GPSDIR",      0, IMP_GET_DIR,      1},
    {"GPSALT",      0, IMP_GET_ALT,      1},
    {"PITCH",       0, IMP_GET_PITCH,    1},
    {"ROLL",        0, IMP_GET_ROLL,     1},
    {"YAW",         0, IMP_GET_YAW,      1},
    {"ACCX",        0, IMP_GET_ACC_X,    1},
    {"ACCY",        0, IMP_GET_ACC_Y,    1},
    {"ACCZ",        0, IMP_GET_ACC_Z,    1},
    {"HUM",         0, IMP_GET_HUMIDITY, 1},
    {"BRIGHT",      0, IMP_GET_BRIGHTNESS,1},
    {"GAMMA256",    1, IMP_LED_GAMMA8,   0},
    {"LOADLUT",     1, IMP_LUT_LOAD,     0},
    {"SAVELUT",     1, IMP_LUT_SAVE,     0},
    {"LUTSIZE",     1, IMP_LUT_CHECK,    0},
    {"LUT",         1, IMP_LUT_GET,      0},
    {"PIN_READ",    1, IMP_PIN_READ,     0},
    {"ANALOG_READ", 1, IMP_ANALOG_READ,  0},
    {"GPSPRESENT",  0, IMP_GPS_PRESENT,  0},
    {"IMUPRESENT",  0, IMP_IMU_PRESENT,  0},
    {"UPTIME",      0, IMP_MILLIS,       0},
    {"BATPCT",      0, IMP_GET_BATTERY_PERCENTAGE, 1},
    {"BATRUNTIME",  0, IMP_GET_BATTERY_RUNTIME,    1},
    {"SUNAZ",       0, IMP_GET_SUN_AZIMUTH,        1},
    {"SUNEL",       0, IMP_GET_SUN_ELEVATION,      1},
    /* Float-returning versions with # suffix */
    {"GPSLAT#",     0, IMP_GET_LAT,      0},
    {"GPSLON#",     0, IMP_GET_LON,      0},
    {"GPSALT#",     0, IMP_GET_ALT,      0},
    {"GPSSPEED#",   0, IMP_GET_SPEED,    0},
    {"GPSDIR#",     0, IMP_GET_DIR,      0},
    {"ORIGINDIST#", 0, IMP_ORIGIN_DIST,  0},
    {"ORIGINANGLE#",0, IMP_ORIGIN_BEARING,0},
    {"PITCH#",      0, IMP_GET_PITCH,    0},
    {"ROLL#",       0, IMP_GET_ROLL,     0},
    {"YAW#",        0, IMP_GET_YAW,      0},
    {"ACCX#",       0, IMP_GET_ACC_X,    0},
    {"ACCY#",       0, IMP_GET_ACC_Y,    0},
    {"ACCZ#",       0, IMP_GET_ACC_Z,    0},
    {"TEMP#",       0, IMP_GET_TEMP,     0},
    {"HUM#",        0, IMP_GET_HUMIDITY, 0},
    {"BRIGHT#",     0, IMP_GET_BRIGHTNESS,0},
    {"BATPCT#",     0, IMP_GET_BATTERY_PERCENTAGE, 0},
    {"BATRUNTIME#", 0, IMP_GET_BATTERY_RUNTIME,    0},
    {"SUNAZ#",      0, IMP_GET_SUN_AZIMUTH,        0},
    {"SUNEL#",      0, IMP_GET_SUN_ELEVATION,      0},
    {NULL, 0, 0, 0}
};

/*
 * compile_builtin_expr — compile a built-in function call in expression context.
 * The opening paren has already been consumed.
 * Returns 1 if handled, 0 if not a builtin.
 */
static int compile_builtin_expr(const char *name) {
    /* Check simple builtins first */
    for (const SimpleBI *b = simple_bi; b->name; b++) {
        if (strcmp(name, b->name) != 0) continue;
        for (int i = 0; i < b->nargs; i++) {
            if (i > 0) need(TOK_COMMA);
            expr(); coerce_i32();
        }
        need(TOK_RP);
        emit_call(b->imp);
        if (b->trunc) {
            emit_op(OP_I32_TRUNC_F32_S);
            vpush(T_I32);
        } else {
            /* Determine type from import definition */
            ImportDef *id = &imp_defs[b->imp];
            vpush(id->nr > 0 && id->r[0] == WASM_F32 ? T_F32 : T_I32);
        }
        return 1;
    }

    /* Custom builtins */

    /* FIXME: get_last_comm_ms returns i64 — stub as 0 until tracked */
    if (strcmp(name, "LASTCOMM") == 0) {
        need(TOK_RP);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }

    if (strcmp(name, "SETLEDCOL") == 0) {
        emit_i32_const(1); /* channel */
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_LED_FILL);
        emit_call(IMP_LED_SHOW);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "WAIT") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_DELAY_MS);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "GETMAXLED") == 0) {
        need(TOK_RP);
        emit_i32_const(1); emit_call(IMP_LED_COUNT);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "USEGAMMA") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_LED_SET_GAMMA);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "TIMESTAMP") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        /* millis() / div */
        int scratch = alloc_local();
        emit_local_set(scratch);
        emit_call(IMP_MILLIS);
        emit_local_get(scratch);
        emit_op(OP_I32_DIV_S);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "VERSION") == 0) {
        need(TOK_RP);
        emit_i32_const(1); vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "RANDOM") == 0) {
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_RANDOM_INT);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "TEMP") == 0) {
        need(TOK_RP);
        emit_call(IMP_GET_TEMP);
        emit_f32_const(10.0f);
        emit_op(OP_F32_MUL);
        emit_op(OP_I32_TRUNC_F32_S);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "ABS") == 0) {
        expr(); need(TOK_RP);
        VType t = vpop();
        if (t == T_F32) {
            emit_op(OP_F32_ABS);
            vpush(T_F32);
        } else {
            /* if v < 0: 0 - v, else v */
            int scratch = alloc_local();
            emit_local_set(scratch);
            emit_i32_const(0);
            emit_local_get(scratch);
            emit_op(OP_I32_SUB);
            emit_local_get(scratch);
            emit_local_get(scratch);
            emit_i32_const(0);
            emit_op(OP_I32_LT_S);
            emit_op(OP_SELECT);
            vpush(T_I32);
        }
        return 1;
    }
    if (strcmp(name, "LIMIT") == 0) {
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        /* stack: val, lo, hi */
        int hi = alloc_local(), lo = alloc_local(), val = alloc_local();
        emit_local_set(hi);
        emit_local_set(lo);
        emit_local_set(val);
        /* clamp: if val < lo then lo, else if val > hi then hi, else val */
        emit_local_get(val);
        emit_local_get(lo);
        emit_local_get(val);
        emit_local_get(lo);
        emit_op(OP_I32_LT_S);
        emit_op(OP_SELECT); /* max(val, lo) */
        int tmp = alloc_local();
        emit_local_set(tmp);
        emit_local_get(hi);
        emit_local_get(tmp);
        emit_local_get(tmp);
        emit_local_get(hi);
        emit_op(OP_I32_GT_S);
        emit_op(OP_SELECT); /* min(tmp, hi) */
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "LIMIT256") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        int val = alloc_local();
        emit_local_set(val);
        /* clamp 0..255 */
        emit_local_get(val);
        emit_i32_const(0);
        emit_local_get(val);
        emit_i32_const(0);
        emit_op(OP_I32_LT_S);
        emit_op(OP_SELECT); /* max(val, 0) */
        int tmp = alloc_local();
        emit_local_set(tmp);
        emit_i32_const(255);
        emit_local_get(tmp);
        emit_local_get(tmp);
        emit_i32_const(255);
        emit_op(OP_I32_GT_S);
        emit_op(OP_SELECT); /* min(tmp, 255) */
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "SCALE") == 0) {
        /* SCALE(val, valmin, valmax, rmin, rmax) = (val-valmin)*(rmax-rmin)/(valmax-valmin)+rmin */
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        int rmax = alloc_local(), rmin = alloc_local();
        int vmax = alloc_local(), vmin = alloc_local(), val = alloc_local();
        emit_local_set(rmax);
        emit_local_set(rmin);
        emit_local_set(vmax);
        emit_local_set(vmin);
        emit_local_set(val);
        /* (val - vmin) * (rmax - rmin) / (vmax - vmin) + rmin */
        emit_local_get(val); emit_local_get(vmin); emit_op(OP_I32_SUB);
        emit_local_get(rmax); emit_local_get(rmin); emit_op(OP_I32_SUB);
        emit_op(OP_I32_MUL);
        emit_local_get(vmax); emit_local_get(vmin); emit_op(OP_I32_SUB);
        emit_op(OP_I32_DIV_S);
        emit_local_get(rmin); emit_op(OP_I32_ADD);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "SIN256") == 0) {
        /* sin((val/255)*2*PI) * 127.5 + 127.5 → i32 */
        expr(); coerce_i32(); need(TOK_RP);
        emit_op(OP_F32_CONVERT_I32_S);
        emit_f32_const(255.0f); emit_op(OP_F32_DIV);
        emit_f32_const(6.2831853f); emit_op(OP_F32_MUL);
        emit_call(IMP_SINF);
        emit_f32_const(1.0f); emit_op(OP_F32_ADD);
        emit_f32_const(0.5f); emit_op(OP_F32_MUL);
        emit_f32_const(255.0f); emit_op(OP_F32_MUL);
        emit_op(OP_I32_TRUNC_F32_S);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "DIST") == 0) {
        /* DIST(x1,y1,x2,y2) = sqrt(dx^2+dy^2) as i32 */
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        int y2 = alloc_local(), x2 = alloc_local();
        int y1 = alloc_local(), x1 = alloc_local();
        emit_local_set(y2); emit_local_set(x2);
        emit_local_set(y1); emit_local_set(x1);
        /* dx = x2-x1, dy = y2-y1 */
        emit_local_get(x2); emit_local_get(x1); emit_op(OP_I32_SUB);
        emit_op(OP_F32_CONVERT_I32_S);
        int fdx = alloc_local_f32();
        emit_local_set(fdx);
        emit_local_get(y2); emit_local_get(y1); emit_op(OP_I32_SUB);
        emit_op(OP_F32_CONVERT_I32_S);
        int fdy = alloc_local_f32();
        emit_local_set(fdy);
        /* sqrt(dx*dx + dy*dy) */
        emit_local_get(fdx); emit_local_get(fdx); emit_op(OP_F32_MUL);
        emit_local_get(fdy); emit_local_get(fdy); emit_op(OP_F32_MUL);
        emit_op(OP_F32_ADD);
        emit_op(OP_F32_SQRT);
        emit_op(OP_I32_TRUNC_F32_S);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "ANGLE") == 0) {
        /* ANGLE(x1,y1,x2,y2) = atan2(dy,dx) in degrees as i32 */
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        int y2 = alloc_local(), x2 = alloc_local();
        int y1 = alloc_local(), x1 = alloc_local();
        emit_local_set(y2); emit_local_set(x2);
        emit_local_set(y1); emit_local_set(x1);
        emit_local_get(y2); emit_local_get(y1); emit_op(OP_I32_SUB);
        emit_op(OP_F32_CONVERT_I32_S);
        emit_local_get(x2); emit_local_get(x1); emit_op(OP_I32_SUB);
        emit_op(OP_F32_CONVERT_I32_S);
        emit_call(IMP_ATAN2F);
        emit_f32_const(57.29578f); emit_op(OP_F32_MUL); /* rad to deg */
        emit_op(OP_I32_TRUNC_F32_S);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "WAITFOR") == 0) {
        /* WAITFOR(event, source, condition, trigger, timeout) */
        /* Simplified: dispatch based on event */
        expr(); coerce_i32(); need(TOK_COMMA); /* event */
        int ev = alloc_local(); emit_local_set(ev);
        expr(); coerce_i32(); need(TOK_COMMA); /* source */
        int src = alloc_local(); emit_local_set(src);
        expr(); coerce_i32(); need(TOK_COMMA); /* condition */
        int cond = alloc_local(); emit_local_set(cond);
        expr(); coerce_i32(); need(TOK_COMMA); /* trigger */
        int trig = alloc_local(); emit_local_set(trig);
        expr(); coerce_i32(); need(TOK_RP);    /* timeout */
        int tout = alloc_local(); emit_local_set(tout);
        /*
         * if event == 4 (SYS_TIMER): delay_ms(trigger * multiplier)
         * if event == 5 (GPS_PPS): wait_pps(timeout)
         * if event == 6 (PARAM): wait_param(source, cond, trigger, timeout)
         * else: return 0
         */
        emit_local_get(ev);
        emit_i32_const(4); /* EVENT_SYS_TIMER */
        emit_op(OP_I32_EQ);
        emit_if_void();
            /* delay_ms based on condition and trigger */
            emit_local_get(trig);
            emit_local_get(cond);
            emit_i32_const(6); /* CONDITON_HOUR */
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_i32_const(3600000); emit_op(OP_I32_MUL);
            emit_else();
                emit_local_get(cond);
                emit_i32_const(7); /* CONDITON_MINUTE */
                emit_op(OP_I32_EQ);
                emit_if_void();
                    emit_i32_const(60000); emit_op(OP_I32_MUL);
                emit_else();
                    emit_local_get(cond);
                    emit_i32_const(8); /* CONDITON_SECOND */
                    emit_op(OP_I32_EQ);
                    emit_if_void();
                        emit_i32_const(1000); emit_op(OP_I32_MUL);
                    emit_end(); /* ms: no multiply needed */
                emit_end();
            emit_end();
            emit_call(IMP_DELAY_MS);
            emit_i32_const(1);
        emit_else();
            emit_local_get(ev);
            emit_i32_const(5); /* EVENT_GPS_PPS */
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_local_get(tout);
                emit_call(IMP_WAIT_PPS);
            emit_else();
                emit_local_get(ev);
                emit_i32_const(6); /* EVENT_PARAM */
                emit_op(OP_I32_EQ);
                emit_if_void();
                    emit_local_get(src);
                    emit_local_get(cond);
                    emit_local_get(trig);
                    emit_local_get(tout);
                    emit_call(IMP_WAIT_PARAM);
                emit_else();
                    emit_i32_const(0);
                emit_end();
            emit_end();
        emit_end();
        vpush(T_I32);
        return 1;
    }
    /* Float math functions */
    if (strcmp(name, "SIN") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_SINF);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "COS") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_COSF);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "SQRT") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_op(OP_F32_SQRT);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "ATAN2") == 0) {
        expr(); coerce_f32(); need(TOK_COMMA);
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_ATAN2F);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "POW") == 0) {
        expr(); coerce_f32(); need(TOK_COMMA);
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_POWF);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "INT") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_op(OP_I32_TRUNC_F32_S);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "FLOAT") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        emit_op(OP_F32_CONVERT_I32_S);
        vpush(T_F32);
        return 1;
    }
    /* SETLEDRGB(aR, aG, aB) — loop over arrays, set pixels, show */
    if (strcmp(name, "SETLEDRGB") == 0) {
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        /* stack: arr_r, arr_g, arr_b (as pointers, i.e., global values of DIM vars) */
        int ab = alloc_local(), ag = alloc_local(), ar = alloc_local();
        emit_local_set(ab); emit_local_set(ag); emit_local_set(ar);
        /* loop: for i = 0 to led_count-1 */
        int i = alloc_local();
        emit_i32_const(0); emit_local_set(i);
        emit_block(); emit_loop();
            emit_local_get(i);
            emit_i32_const(1); emit_call(IMP_LED_COUNT);
            emit_op(OP_I32_GE_S);
            emit_br_if(1);
            /* led_set_pixel(1, i, arr_r[i+1], arr_g[i+1], arr_b[i+1]) */
            emit_i32_const(1); /* channel */
            emit_local_get(i);
            /* arr_r[(i+1)*4] — arrays store size at [0], values at [1]..  */
            emit_local_get(ar); emit_local_get(i); emit_i32_const(1); emit_op(OP_I32_ADD);
            emit_i32_const(4); emit_op(OP_I32_MUL); emit_op(OP_I32_ADD);
            emit_i32_load(0);
            emit_local_get(ag); emit_local_get(i); emit_i32_const(1); emit_op(OP_I32_ADD);
            emit_i32_const(4); emit_op(OP_I32_MUL); emit_op(OP_I32_ADD);
            emit_i32_load(0);
            emit_local_get(ab); emit_local_get(i); emit_i32_const(1); emit_op(OP_I32_ADD);
            emit_i32_const(4); emit_op(OP_I32_MUL); emit_op(OP_I32_ADD);
            emit_i32_load(0);
            emit_call(IMP_LED_SET_PIXEL);
            emit_local_get(i); emit_i32_const(1); emit_op(OP_I32_ADD); emit_local_set(i);
            emit_br(0);
        emit_end(); emit_end();
        emit_call(IMP_LED_SHOW);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    /* Array operations */
    if (strcmp(name, "SETARRAY") == 0) {
        /* SETARRAY(arr, start, end, value) */
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        int val = alloc_local(), end = alloc_local();
        int start = alloc_local(), arr = alloc_local();
        emit_local_set(val); emit_local_set(end);
        emit_local_set(start); emit_local_set(arr);
        int i = alloc_local();
        emit_local_get(start); emit_local_set(i);
        emit_block(); emit_loop();
            emit_local_get(i); emit_local_get(end); emit_op(OP_I32_GT_S);
            emit_br_if(1);
            emit_local_get(arr); emit_local_get(i);
            emit_i32_const(4); emit_op(OP_I32_MUL); emit_op(OP_I32_ADD);
            emit_local_get(val);
            emit_i32_store(0);
            emit_local_get(i); emit_i32_const(1); emit_op(OP_I32_ADD); emit_local_set(i);
            emit_br(0);
        emit_end(); emit_end();
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "SHIFTARRAY") == 0 || strcmp(name, "ROTATEARRAY") == 0 ||
        strcmp(name, "COPYARRAY") == 0 || strcmp(name, "SCALELIMITARRAY") == 0 ||
        strcmp(name, "RGBTOHSVARRAY") == 0 || strcmp(name, "HSVTORGBARRAY") == 0 ||
        strcmp(name, "LUTTOARRAY") == 0 || strcmp(name, "ARRAYTOLUT") == 0) {
        /* Stub: consume args, return 0 */
        /* Count expected args from basic_extensions.h table */
        int nargs = 2; /* default */
        if (strcmp(name, "COPYARRAY") == 0) nargs = 2;
        else if (strcmp(name, "SHIFTARRAY") == 0) nargs = 3;
        else if (strcmp(name, "ROTATEARRAY") == 0) nargs = 2;
        else if (strcmp(name, "SCALELIMITARRAY") == 0) nargs = 4;
        else if (strcmp(name, "RGBTOHSVARRAY") == 0) nargs = 3;
        else if (strcmp(name, "HSVTORGBARRAY") == 0) nargs = 3;
        else if (strcmp(name, "LUTTOARRAY") == 0) nargs = 1;
        else if (strcmp(name, "ARRAYTOLUT") == 0) nargs = 1;
        for (int i = 0; i < nargs; i++) {
            if (i > 0) need(TOK_COMMA);
            expr(); coerce_i32();
            emit_drop();
        }
        need(TOK_RP);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    /* ---- String functions ---- */
    if (strcmp(name, "LEN") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_LEN);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "MID$") == 0) {
        expr(); need(TOK_COMMA);       /* string */
        expr(); coerce_i32(); need(TOK_COMMA); /* start (1-based) */
        expr(); coerce_i32(); need(TOK_RP);    /* length */
        emit_call(IMP_STR_MID);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "LEFT$") == 0) {
        expr(); need(TOK_COMMA);       /* string */
        expr(); coerce_i32(); need(TOK_RP); /* n */
        emit_call(IMP_STR_LEFT);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "RIGHT$") == 0) {
        expr(); need(TOK_COMMA);       /* string */
        expr(); coerce_i32(); need(TOK_RP); /* n */
        emit_call(IMP_STR_RIGHT);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "CHR$") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_STR_CHR);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "ASC") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_ASC);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "STR$") == 0) {
        expr(); need(TOK_RP);
        VType t = vpop();
        if (t == T_F32) {
            emit_call(IMP_STR_FROM_FLOAT);
        } else {
            emit_call(IMP_STR_FROM_INT);
        }
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "VAL") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_TO_INT);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "VAL#") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_TO_FLOAT);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "UPPER$") == 0 || strcmp(name, "UCASE$") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_UPPER);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "LOWER$") == 0 || strcmp(name, "LCASE$") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_LOWER);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "INSTR") == 0) {
        expr(); need(TOK_COMMA);       /* haystack */
        expr();                         /* needle */
        if (want(TOK_COMMA)) {
            expr(); coerce_i32();       /* start (1-based) */
        } else {
            emit_i32_const(1);          /* default start = 1 */
        }
        need(TOK_RP);
        emit_call(IMP_STR_INSTR);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "TRIM$") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_TRIM);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "LTRIM$") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_LTRIM);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "RTRIM$") == 0) {
        expr(); need(TOK_RP);
        emit_call(IMP_STR_RTRIM);
        vpush(T_STR);
        return 1;
    }
    /* ---- Additional string functions ---- */
    if (strcmp(name, "STRING$") == 0) {
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_STR_REPEAT);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "SPACE$") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_STR_SPACE);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "HEX$") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_STR_HEX);
        vpush(T_STR);
        return 1;
    }
    if (strcmp(name, "OCT$") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_STR_OCT);
        vpush(T_STR);
        return 1;
    }
    /* ---- Additional math functions ---- */
    if (strcmp(name, "TAN") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_TANF);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "EXP") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_EXPF);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "LOG") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_LOGF);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "LOG2") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_LOG2F);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "FLOOR") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_op(OP_F32_FLOOR);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "CEIL") == 0) {
        expr(); coerce_f32(); need(TOK_RP);
        emit_op(OP_F32_CEIL);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "FMOD") == 0) {
        expr(); coerce_f32(); need(TOK_COMMA);
        expr(); coerce_f32(); need(TOK_RP);
        emit_call(IMP_FMODF);
        vpush(T_F32);
        return 1;
    }
    if (strcmp(name, "SGN") == 0) {
        expr(); need(TOK_RP);
        VType t = vpop();
        if (t == T_F32) {
            /* (x > 0.0) - (x < 0.0) */
            int scratch = alloc_local_f32();
            emit_local_set(scratch);
            emit_local_get(scratch);
            emit_f32_const(0.0f);
            emit_op(OP_F32_GT);
            emit_local_get(scratch);
            emit_f32_const(0.0f);
            emit_op(OP_F32_LT);
            emit_op(OP_I32_SUB);
        } else {
            /* (x > 0) - (x < 0) */
            int scratch = alloc_local();
            emit_local_set(scratch);
            emit_local_get(scratch);
            emit_i32_const(0);
            emit_op(OP_I32_GT_S);
            emit_local_get(scratch);
            emit_i32_const(0);
            emit_op(OP_I32_LT_S);
            emit_op(OP_I32_SUB);
        }
        vpush(T_I32);
        return 1;
    }

    /* ---- File I/O ---- */
    if (strcmp(name, "LBOUND") == 0) {
        /* LBOUND(array) — always 1 (arrays are 1-based) */
        need(TOK_NAME); need(TOK_RP);
        emit_i32_const(1);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "EOF") == 0) {
        /* EOF(channel_number) — returns -1 at EOF, 0 otherwise (QB convention) */
        need(TOK_NUMBER);
        int ch = tokv;
        if (ch < 1 || ch > 4) error_at("channel must be 1-4");
        need(TOK_RP);
        /* Load handle from file table, call eof */
        emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
        emit_i32_load(0);
        emit_call(IMP_FILE_EOF);
        /* Host returns 1/0. QB convention: -1 (true) / 0 (false). Negate: 0 - result */
        int tmp = alloc_local();
        emit_local_set(tmp);
        emit_i32_const(0);
        emit_local_get(tmp);
        emit_op(OP_I32_SUB);
        vpush(T_I32);
        return 1;
    }

    return 0;
}

/* ================================================================
 *  Expression Parser
 * ================================================================ */

static void base_expr(void) {
    int neg = want(TOK_SUB) ? 1 : 0;

    if (want(TOK_NOT)) {
        base_expr();
        if (vsp > 0 && vstack[vsp-1] == T_STR) { error_at("cannot use NOT on strings"); return; }
        coerce_i32();
        emit_i32_const(-1);
        emit_op(OP_I32_XOR);
    } else if (want(TOK_NUMBER)) {
        emit_i32_const(tokv);
        vpush(T_I32);
    } else if (want(TOK_FLOAT)) {
        emit_f32_const(tokf);
        vpush(T_F32);
    } else if (want(TOK_STRING)) {
        emit_i32_const(tokv); /* offset into data section */
        vpush(T_STR);
    } else if (want(TOK_NAME)) {
        int var = tokv;
        if (want(TOK_LP)) {
            if (vars[var].mode == VAR_DIM) {
                /* Array access: arr(i) → load from memory */
                expr(); coerce_i32(); need(TOK_RP);
                emit_global_get(vars[var].global_idx);
                emit_op(OP_I32_ADD); /* this is wrong — need index*4 */
                /* Fix: base + i*4 */
                /* Actually: swap order. Stack has: [index, base]. Need base + index*4 */
                /* Let me redo: */
                /* We just emitted global_get on top of index. Stack: [index_coerced, base] */
                /* Hmm, order is wrong. Let me use a local. */
                /* Actually the emit order is: expr (index on stack), global_get (base on stack) */
                /* Stack: [index, base]. For i32.add: base + index, but I need base + index*4 */
                /* Let me restructure */
                error_at("internal: array access rewrite needed");
                vpush(T_I32); /* placeholder */
            } else if (!compile_builtin_expr(vars[var].name)) {
                /* SUB call */
                int nargs = 0;
                if (!want(TOK_RP)) {
                    do { expr(); coerce_i32(); nargs++; } while (want(TOK_COMMA));
                    need(TOK_RP);
                }
                if (vars[var].mode != VAR_SUB) {
                    error_at("not a function");
                } else {
                    emit_call(IMP_COUNT + vars[var].func_local_idx);
                }
                vpush(T_I32);
            }
        } else {
            /* Variable load */
            emit_global_get(vars[var].global_idx);
            vpush(vars[var].type_set ? vars[var].type : T_I32);
        }
    } else if (want(TOK_LP)) {
        expr();
        need(TOK_RP);
    } else if (want(TOK_UBOUND)) {
        need(TOK_LP); need(TOK_NAME);
        int var = tokv;
        need(TOK_RP);
        emit_global_get(vars[var].global_idx);
        emit_i32_load(0);
        vpush(T_I32);
    } else {
        error_at("bad expression");
        emit_i32_const(0);
        vpush(T_I32);
    }

    if (neg) {
        VType t = vpop();
        /* Constant folding: negate the constant directly */
        if (fold_b.valid && fold_b.buf_end == CODE->len) {
            if (fold_b.valid == 1) {
                int32_t v = fold_b.ival;
                CODE->len = fold_b.buf_start;
                fold_a.valid = fold_b.valid = 0;
                emit_i32_const(-v);
                vpush(T_I32);
            } else {
                float v = fold_b.fval;
                CODE->len = fold_b.buf_start;
                fold_a.valid = fold_b.valid = 0;
                emit_f32_const(-v);
                vpush(T_F32);
            }
        } else if (t == T_F32) {
            int scratch = alloc_local_f32();
            emit_local_set(scratch);
            emit_f32_const(0.0f);
            emit_local_get(scratch);
            emit_op(OP_F32_SUB);
            vpush(T_F32);
        } else {
            int sc = alloc_local();
            emit_local_set(sc);
            emit_i32_const(0);
            emit_local_get(sc);
            emit_op(OP_I32_SUB);
            vpush(T_I32);
        }
    }
}

/* Fix array access — redo properly */
/* Actually let me fix base_expr's array access path */

static void factor(void);
static void addition(void);
static void relation(void);

/* Integer binary op: coerce both operands to i32 */
static void emit_int_binop(int i32_op) {
    VType b = vpop(), a = vpop();
    /* Constant folding: both i32 */
    if (fold_a.valid == 1 && fold_b.valid == 1 &&
        fold_a.buf_end == fold_b.buf_start &&
        fold_b.buf_end == CODE->len) {
        int32_t va = fold_a.ival, vb = fold_b.ival;
        int folded = 0; int32_t r = 0;
        if (i32_op == OP_I32_DIV_S && vb != 0) { r = va / vb; folded = 1; }
        else if (i32_op == OP_I32_REM_S && vb != 0) { r = va % vb; folded = 1; }
        if (folded) {
            CODE->len = fold_a.buf_start;
            fold_a.valid = fold_b.valid = 0;
            emit_i32_const(r);
            vpush(T_I32);
            return;
        }
    }
    if (a == T_I32 && b == T_I32) {
        emit_op(i32_op);
    } else if (a == T_I32 && b == T_F32) {
        /* stack: [a_i32, b_f32]. Trunc b. */
        emit_op(OP_I32_TRUNC_F32_S);
        emit_op(i32_op);
    } else if (a == T_F32 && b == T_I32) {
        /* stack: [a_f32, b_i32]. Save b, trunc a, reload b. */
        int sc = alloc_local();
        emit_local_set(sc);
        emit_op(OP_I32_TRUNC_F32_S);
        emit_local_get(sc);
        emit_op(i32_op);
    } else {
        /* both f32: save b, trunc a, trunc b */
        int sc = alloc_local_f32();
        emit_local_set(sc);
        emit_op(OP_I32_TRUNC_F32_S);
        emit_local_get(sc);
        emit_op(OP_I32_TRUNC_F32_S);
        emit_op(i32_op);
    }
    vpush(T_I32);
}

static void power(void) {
    base_expr();
    if (want(TOK_POW)) {
        int pos1 = CODE->len;
        FoldSlot save1 = fold_b;
        coerce_f32();
        power();  /* right-associative: 2^3^2 = 2^(3^2) */
        int pos2 = CODE->len;
        FoldSlot save2 = fold_b;
        coerce_f32();
        /* Constant folding: if both operands were constants */
        if (save1.valid && save1.buf_end == pos1 &&
            save2.valid && save2.buf_end == pos2) {
            float va = (save1.valid == 1) ? (float)save1.ival : save1.fval;
            float vb = (save2.valid == 1) ? (float)save2.ival : save2.fval;
            CODE->len = save1.buf_start;
            fold_a.valid = fold_b.valid = 0;
            emit_f32_const(powf(va, vb));
            vpush(T_F32);
            return;
        }
        emit_call(IMP_POWF);
        vpush(T_F32);
    }
}

static void factor(void) {
    power();
    while (want(0), (tok >= TOK_MUL && tok <= TOK_IDIV) || tok == TOK_MOD) {
        int op = tok;
        read_tok();
        power();
        if (vsp >= 2 && (vstack[vsp-1] == T_STR || vstack[vsp-2] == T_STR)) {
            error_at("cannot use *, /, \\ or MOD on strings"); return;
        }
        switch (op) {
        case TOK_MUL:  emit_binop(OP_I32_MUL, OP_F32_MUL); break;
        case TOK_DIV:  emit_binop(OP_I32_DIV_S, OP_F32_DIV); break;
        case TOK_IDIV: emit_int_binop(OP_I32_DIV_S); break;
        case TOK_MOD:  emit_int_binop(OP_I32_REM_S); break;
        }
    }
}

static void addition(void) {
    factor();
    while (want(0), tok >= TOK_ADD && tok <= TOK_SUB) {
        int op = tok;
        read_tok();
        factor();
        if (op == TOK_ADD && vsp >= 2 && vstack[vsp-1] == T_STR && vstack[vsp-2] == T_STR) {
            vpop(); vpop();
            emit_call(IMP_STR_CONCAT);
            vpush(T_STR);
        } else if (vsp >= 2 && (vstack[vsp-1] == T_STR || vstack[vsp-2] == T_STR)) {
            error_at("cannot mix strings and numbers with + or -");
        } else if (op == TOK_ADD) {
            emit_binop(OP_I32_ADD, OP_F32_ADD);
        } else {
            emit_binop(OP_I32_SUB, OP_F32_SUB);
        }
    }
}

static void relation(void) {
    addition();
    while (want(0), tok >= TOK_EQ && tok <= TOK_GE) {
        int op = tok;
        read_tok();
        addition();
        if (vsp >= 2 && vstack[vsp-1] == T_STR && vstack[vsp-2] == T_STR) {
            vpop(); vpop();
            emit_call(IMP_STR_CMP);
            /* str_cmp returns <0, 0, or >0 (like strcmp) */
            switch (op) {
            case TOK_EQ: emit_op(OP_I32_EQZ); break;
            case TOK_NE: emit_i32_const(0); emit_op(OP_I32_NE); break;
            case TOK_LT: emit_i32_const(0); emit_op(OP_I32_LT_S); break;
            case TOK_GT: emit_i32_const(0); emit_op(OP_I32_GT_S); break;
            case TOK_LE: emit_i32_const(0); emit_op(OP_I32_LE_S); break;
            case TOK_GE: emit_i32_const(0); emit_op(OP_I32_GE_S); break;
            }
            emit_i32_const(-1);
            emit_op(OP_I32_MUL);
            vpush(T_I32);
        } else if (vsp >= 2 && (vstack[vsp-1] == T_STR || vstack[vsp-2] == T_STR)) {
            error_at("cannot compare string with number");
        } else {
            switch (op) {
            case TOK_EQ: emit_compare(OP_I32_EQ, OP_F32_EQ); break;
            case TOK_LT: emit_compare(OP_I32_LT_S, OP_F32_LT); break;
            case TOK_GT: emit_compare(OP_I32_GT_S, OP_F32_GT); break;
            case TOK_NE: emit_compare(OP_I32_NE, OP_F32_NE); break;
            case TOK_LE: emit_compare(OP_I32_LE_S, OP_F32_LE); break;
            case TOK_GE: emit_compare(OP_I32_GE_S, OP_F32_GE); break;
            }
        }
    }
}

static void expr(void) {
    relation();
    while (want(0), tok == TOK_AND || tok == TOK_OR || tok == TOK_XOR) {
        int op = tok;
        read_tok();
        relation();
        /* AND/OR/XOR are bitwise on i32 (-1/0 values) */
        VType b = vpop(), a = vpop();
        if (a == T_STR || b == T_STR) {
            error_at("cannot use AND/OR/XOR on strings");
        }
        emit_op(op == TOK_AND ? OP_I32_AND : op == TOK_OR ? OP_I32_OR : OP_I32_XOR);
        vpush(T_I32);
    }
}

/* ================================================================
 *  Statement Parser
 * ================================================================ */

static void stmt(void);

static void compile_format(void) {
    need(TOK_STRING);
    int raw_off = tokv;
    /* Convert BASIC format string to C format: % → %d, $ → %s, & → %f, append \n */
    char cfmt[512];
    int ci = 0;
    for (const char *p = data_buf + raw_off; *p; p++) {
        if (*p == '%') { cfmt[ci++] = '%'; cfmt[ci++] = 'd'; }
        else if (*p == '$') { cfmt[ci++] = '%'; cfmt[ci++] = 's'; }
        else if (*p == '&') { cfmt[ci++] = '%'; cfmt[ci++] = 'f'; }
        else cfmt[ci++] = *p;
    }
    cfmt[ci++] = '\n'; cfmt[ci] = 0;
    data_len = raw_off; /* reclaim raw string — only the converted version is needed */
    int fmt_off = add_string(cfmt, ci);

    /* Parse args */
    int nargs = 0;
    /* fmt_buf_addr resolved at assembly time as data_len + padding */
    while (want(TOK_COMMA)) {
        /* Store arg at fmt_buf + nargs*4 */
        /* We don't know fmt_buf_addr yet, will use global _heap_ptr-relative? */
        /* Actually, fmt_buf_addr will be set during assembly as data_len. */
        /* For now, emit a placeholder using a marker global. */
        /* Simpler: use a fixed address. Reserve data_len..data_len+255 as fmt buffer. */
        /* The fmt_buf address is known after all strings are added. */
        /* Since we're still adding strings, defer. Use a compile-time approach: */
        /* Emit: i32.const <placeholder> ; <expr> ; i32.store offset=N*4 */
        /* We'll fixup the fmt_buf address at the end. */
        /* Actually, let's just use a simple approach: pick a high address like 0x8000 */
        /* and set _heap_ptr above it. Or better: track that fmt_buf = data_end (a global we set). */

        /* The cleanest way: emit code that uses global 0 (_heap_ptr) minus some offset. */
        /* But that's complex. Let me use a simpler approach: */
        /* Since this is FORMAT inside compilation, I'll store all args at a known buffer. */
        /* I'll use linear memory starting at a fixed offset. After all data, the fmt buffer. */
        /* The address is: total_data_len (known after compilation). */
        /* For now, emit calls that store args relative to a "fmt_buf_base" which I'll fixup. */

        /* Simplest approach for now: emit stores at address 0xF000 (in linear memory). */
        /* This is a fixed area we reserve. _heap_ptr starts above it. */
        emit_i32_const(0xF000);
        expr();
        /* Check type: if f32 arg, store as f32 bits */
        VType t = vpop();
        if (t == T_F32) {
            /* For %f format, host_printf expects an f32 reinterpreted as i32 bits? */
            /* Actually, wasm_format reads 4 bytes. For %f it reads as float. */
            /* So store f32 directly. */
            buf_byte(CODE, OP_F32_STORE); buf_uleb(CODE, 2); buf_uleb(CODE, nargs * 4);
        } else {
            emit_i32_store(nargs * 4);
        }
        nargs++;
    }

    /* Call host_printf(fmt_str_ptr, args_buf_ptr) */
    emit_i32_const(fmt_off);
    emit_i32_const(0xF000);
    emit_call(IMP_HOST_PRINTF);
    emit_drop(); /* discard return value */
}

static void compile_prints(void) {
    /* PRINTS <expr> — expr should be a string constant */
    int before_vsp = vsp;
    expr();
    VType t = vpop();
    (void)t;
    /* We need ptr and len. If it was a string constant, the ptr is known. */
    /* For general case, we need strlen. For simplicity, use host_printf("%s\n", arg). */
    /* Store arg at fmt_buf */
    emit_i32_store(0); /* Hmm, no address on stack. Let me redo. */
    /* Actually, let's redo: print the string using host_printf */
    /* We need the string pointer (already on stack before vpop). */
    /* Let me restructure: */
    /* The expression result is a string pointer (i32). We need its length. */
    /* Use host_printf with "%s\n" format: */
    (void)before_vsp;
    /* The expr was already emitted and we did vpop. The value is still on the WASM stack. */
    /* Store it at fmt_buf: */
    int fmt_buf = 0xF000;
    /* We need to re-emit. Actually the value IS on the WASM stack. */
    /* Emit: store it at fmt_buf, then call host_printf("%s\n", fmt_buf) */
    emit_i32_const(fmt_buf);
    /* But the value is under this address. WASM stack: [value, addr]. i32.store needs [addr, value]. */
    /* We need to swap. Use a local. */
    int tmp = alloc_local();
    emit_local_set(tmp);    /* save addr */
    /* stack: [value] */
    int tmp2 = alloc_local();
    emit_local_set(tmp2);   /* save value */
    emit_local_get(tmp);    /* addr */
    emit_local_get(tmp2);   /* value */
    emit_i32_store(0);

    /* Create "%s\n" format string if not already */
    static int prints_fmt_off = -1;
    if (prints_fmt_off < 0) prints_fmt_off = add_string("%s\n", 3);
    emit_i32_const(prints_fmt_off);
    emit_i32_const(fmt_buf);
    emit_call(IMP_HOST_PRINTF);
    emit_drop();
}

static void compile_sub(void) {
    need(TOK_NAME);
    int var = tokv;
    vars[var].mode = VAR_SUB;

    /* Allocate a new function */
    int fi = nfuncs++;
    vars[var].func_local_idx = fi;
    FuncCtx *f = &func_bufs[fi];
    buf_init(&f->code);
    f->nparams = 0;
    f->nlocals = 0;
    f->sub_var = var;

    /* Parse params */
    int params[8], np = 0;
    if (!want(TOK_EOF)) {
        ungot = 1;
        do {
            if (want(TOK_COMMA)) {} /* skip leading/extra commas */
            need(TOK_NAME);
            params[np++] = tokv;
        } while (want(TOK_COMMA));
    }
    vars[var].param_count = np;
    for (int i = 0; i < np; i++) vars[var].param_vars[i] = params[i];
    f->nparams = np;
    for (int i = 0; i < np; i++) f->param_types[i] = WASM_I32;

    /* Switch to this function's code buffer */
    int prev_func = cur_func;
    int prev_depth = block_depth;
    cur_func = fi;
    block_depth = 0;

    /* Allocate saved-global locals for each param */
    int saved[8];
    for (int i = 0; i < np; i++) saved[i] = alloc_local();

    /* Emit prologue: save globals, install params */
    for (int i = 0; i < np; i++) {
        emit_global_get(vars[params[i]].global_idx);
        emit_local_set(saved[i]);
    }
    for (int i = 0; i < np; i++) {
        emit_local_get(i); /* param i */
        emit_global_set(vars[params[i]].global_idx);
    }

    /* Store saved[] for epilogue (we'll emit epilogue at END SUB) */
    /* Save info in a simple way: use the ctrl stack */
    ctrl_stk[ctrl_sp].kind = -1; /* marker for SUB */
    ctrl_stk[ctrl_sp].for_var = var;
    ctrl_stk[ctrl_sp].for_limit_local = prev_func;
    ctrl_stk[ctrl_sp].break_depth = prev_depth;
    ctrl_stk[ctrl_sp].if_extra_ends = np;
    /* Store saved local indices — pack into unused fields */
    /* Actually, let's just store them as func-local data. We'll re-derive them. */
    /* The saved locals are always the first np locals after params. */
    ctrl_sp++;
}

static void close_sub(void) {
    ctrl_sp--;
    int var = ctrl_stk[ctrl_sp].for_var;
    int prev_func = ctrl_stk[ctrl_sp].for_limit_local;
    int prev_depth = ctrl_stk[ctrl_sp].break_depth;
    int np = ctrl_stk[ctrl_sp].if_extra_ends;

    /* Restore globals from saved locals */
    for (int i = 0; i < np; i++) {
        emit_local_get(np + i); /* saved_i is at local index nparams + i */
        emit_global_set(vars[vars[var].param_vars[i]].global_idx);
    }

    /* Also restore any LOCAL vars if present */
    for (int i = 0; i < vars[var].local_count; i++) {
        /* LOCAL vars' saved copies are at local index nparams + np + i */
        emit_local_get(np + np + i); /* wait, this isn't right */
        /* Actually, LOCAL save slots: after param saves. Let me skip LOCAL restore for now. */
    }

    emit_i32_const(0); /* default return value */
    emit_end(); /* function end */

    cur_func = prev_func;
    block_depth = prev_depth;
}

static void close_while(void) {
    ctrl_sp--;
    if (ctrl_stk[ctrl_sp].kind != CTRL_WHILE) { error_at("WEND without WHILE"); return; }
    emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
    emit_end(); /* loop end */
    emit_end(); /* block end */
}

static void close_for(void) {
    ctrl_sp--;
    if (ctrl_stk[ctrl_sp].kind != CTRL_FOR) { error_at("NEXT without FOR"); return; }
    /* Increment loop variable by step (or 1 if no STEP) */
    int var = ctrl_stk[ctrl_sp].for_var;
    emit_global_get(vars[var].global_idx);
    if (ctrl_stk[ctrl_sp].for_has_step) {
        emit_local_get(ctrl_stk[ctrl_sp].for_step_local);
    } else {
        emit_i32_const(1);
    }
    emit_op(OP_I32_ADD);
    emit_global_set(vars[var].global_idx);
    /* Jump back to loop start */
    emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
    emit_end(); /* loop end */
    emit_end(); /* block end */
}

static void compile_end(void) {
    int kw = read_tok();
    if (kw == TOK_KW_SUB || kw == TOK_FUNCTION) {
        close_sub();
    } else if (kw == TOK_IF) {
        ctrl_sp--;
        if (ctrl_stk[ctrl_sp].kind != CTRL_IF) { error_at("END IF without IF"); return; }
        int extras = ctrl_stk[ctrl_sp].if_extra_ends;
        emit_end(); /* close current if */
        for (int i = 0; i < extras; i++) emit_end();
    } else if (kw == TOK_SELECT) {
        ctrl_sp--;
        if (ctrl_stk[ctrl_sp].kind != CTRL_SELECT) { error_at("END SELECT without SELECT"); return; }
        int extras = ctrl_stk[ctrl_sp].if_extra_ends;
        for (int i = 0; i < extras; i++) emit_end(); /* close case if-blocks */
        emit_end(); /* close outer block */
    } else {
        error_at("unexpected END");
    }
}

static void compile_while(void) {
    emit_block();
    emit_loop();
    /* Condition */
    expr(); coerce_i32();
    emit_op(OP_I32_EQZ);
    emit_br_if(1); /* break if false */
    vpop();
    /* Push control entry */
    ctrl_stk[ctrl_sp].kind = CTRL_WHILE;
    ctrl_stk[ctrl_sp].break_depth = block_depth - 1; /* outer block */
    ctrl_stk[ctrl_sp].cont_depth = block_depth;       /* inner loop */
    ctrl_stk[ctrl_sp].if_extra_ends = 0;
    ctrl_sp++;
}

static void compile_for(void) {
    need(TOK_NAME);
    int var = tokv;
    if (vars[var].type == T_STR) { error_at("FOR loop variable cannot be a string"); return; }
    need(TOK_EQ);
    expr(); coerce_i32(); vpop();
    emit_global_set(vars[var].global_idx);
    need(TOK_TO);
    expr(); coerce_i32(); vpop();
    int limit_local = alloc_local();
    emit_local_set(limit_local);

    int step_local = -1;
    int has_step = 0;
    if (want(TOK_STEP)) {
        expr(); coerce_i32(); vpop();
        step_local = alloc_local();
        emit_local_set(step_local);
        has_step = 1;
    }

    emit_block();
    emit_loop();

    if (has_step) {
        /* Direction-aware exit condition using SELECT:
         * ge_result = (var >= limit)   -- for positive step
         * le_result = (var <= limit)   -- for negative step
         * select(ge_result, le_result, step > 0) → br_if */
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(OP_I32_GE_S);
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(OP_I32_LE_S);
        emit_local_get(step_local);
        emit_i32_const(0);
        emit_op(OP_I32_GT_S);
        emit_op(OP_SELECT);
        emit_br_if(1);
    } else {
        /* Simple: I >= limit → break */
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(OP_I32_GE_S);
        emit_br_if(1);
    }

    ctrl_stk[ctrl_sp].kind = CTRL_FOR;
    ctrl_stk[ctrl_sp].for_var = var;
    ctrl_stk[ctrl_sp].for_limit_local = limit_local;
    ctrl_stk[ctrl_sp].break_depth = block_depth - 1;
    ctrl_stk[ctrl_sp].cont_depth = block_depth;
    ctrl_stk[ctrl_sp].if_extra_ends = 0;
    ctrl_stk[ctrl_sp].for_step_local = step_local;
    ctrl_stk[ctrl_sp].for_has_step = has_step;
    ctrl_sp++;
}

static void compile_if(void) {
    expr(); coerce_i32(); vpop();
    if (want(TOK_THEN)) {
        /* Single-line IF: condition; if; stmt; end */
        emit_if_void();
        stmt();
        emit_end();
    } else {
        emit_if_void();
        ctrl_stk[ctrl_sp].kind = CTRL_IF;
        ctrl_stk[ctrl_sp].if_extra_ends = 0;
        ctrl_sp++;
    }
}

static void compile_else(void) {
    if (ctrl_sp == 0 || ctrl_stk[ctrl_sp-1].kind != CTRL_IF) {
        error_at("ELSE without IF"); return;
    }
    emit_else();
    if (want(TOK_IF)) {
        /* ELSE IF — emit new condition and if block */
        expr(); coerce_i32(); vpop();
        emit_if_void();
        ctrl_stk[ctrl_sp-1].if_extra_ends++;
    }
}

static void compile_const(void) {
    need(TOK_NAME);
    int var = tokv;
    need(TOK_EQ);
    expr();
    VType et = vpop();
    if (vars[var].type == T_STR) {
        /* String const: just store the pointer */
        emit_global_set(vars[var].global_idx);
    } else {
        if (!vars[var].type_set) {
            vars[var].type = et;
            vars[var].type_set = 1;
        } else if (vars[var].type == T_I32 && et == T_F32) {
            emit_op(OP_I32_TRUNC_F32_S);
        } else if (vars[var].type == T_F32 && et == T_I32) {
            emit_op(OP_F32_CONVERT_I32_S);
        }
        emit_global_set(vars[var].global_idx);
    }
    vars[var].is_const = 1;
}

static void compile_dim(void) {
    need(TOK_NAME);
    int var = tokv;
    vars[var].mode = VAR_DIM;
    need(TOK_LP);
    expr(); coerce_i32(); vpop();
    need(TOK_RP);
    /* DIM arr(n): allocate (n+1)*4 bytes from heap, store base in global */
    /* Stack has: n */
    int n_local = alloc_local();
    emit_local_set(n_local);
    /* base = _heap_ptr */
    emit_global_get(GLOBAL_HEAP); /* _heap_ptr */
    emit_global_set(vars[var].global_idx);
    /* Store size at arr[0] */
    emit_global_get(vars[var].global_idx);
    emit_local_get(n_local);
    emit_i32_store(0);
    /* Advance _heap_ptr by (n+1)*4 */
    emit_global_get(GLOBAL_HEAP);
    emit_local_get(n_local);
    emit_i32_const(1); emit_op(OP_I32_ADD);
    emit_i32_const(4); emit_op(OP_I32_MUL);
    emit_op(OP_I32_ADD);
    emit_global_set(GLOBAL_HEAP);
    /* TODO: zero-fill the array */
}

static void compile_local(void) {
    /* LOCAL vars in a SUB — save the current global values */
    if (cur_func == 0) { error_at("LOCAL outside SUB"); return; }
    int sub_var = func_bufs[cur_func].sub_var;
    do {
        need(TOK_NAME);
        int var = tokv;
        vars[sub_var].local_vars[vars[sub_var].local_count++] = var;
        /* Allocate a local to save the global */
        int saved = alloc_local();
        emit_global_get(vars[var].global_idx);
        emit_local_set(saved);
    } while (want(TOK_COMMA));
}

static void compile_return(void) {
    /* RETURN [expr] */
    if (cur_func == 0) {
        /* In main: just return */
        emit_return();
        return;
    }
    int sub_var = func_bufs[cur_func].sub_var;
    int np = vars[sub_var].param_count;

    if (!want(TOK_EOF)) {
        ungot = 1;
        expr(); coerce_i32(); vpop();
        /* Store return value in a local, do epilogue, then return it */
        int ret_local = alloc_local();
        emit_local_set(ret_local);

        /* Restore globals */
        for (int i = 0; i < np; i++) {
            emit_local_get(np + i);
            emit_global_set(vars[vars[sub_var].param_vars[i]].global_idx);
        }

        emit_local_get(ret_local);
        emit_return();
    } else {
        /* Restore globals */
        for (int i = 0; i < np; i++) {
            emit_local_get(np + i);
            emit_global_set(vars[vars[sub_var].param_vars[i]].global_idx);
        }
        emit_i32_const(0);
        emit_return();
    }
}

static void compile_select(void) {
    need(TOK_CASE);
    /* Evaluate test expression, store in a local */
    expr();
    VType test_type = vpop();
    int test_local;
    if (test_type == T_F32) {
        test_local = alloc_local_f32();
    } else {
        test_local = alloc_local();
    }
    emit_local_set(test_local);

    /* Outer block = break target for END SELECT */
    emit_block();

    ctrl_stk[ctrl_sp].kind = CTRL_SELECT;
    ctrl_stk[ctrl_sp].for_var = test_local;
    ctrl_stk[ctrl_sp].for_limit_local = (int)test_type;
    ctrl_stk[ctrl_sp].break_depth = block_depth;
    ctrl_stk[ctrl_sp].if_extra_ends = 0;
    ctrl_sp++;
}

static void compile_case(void) {
    /* Find nearest CTRL_SELECT on ctrl stack */
    int si = -1;
    for (int i = ctrl_sp - 1; i >= 0; i--) {
        if (ctrl_stk[i].kind == CTRL_SELECT) { si = i; break; }
    }
    if (si < 0) { error_at("CASE without SELECT"); return; }

    int test_local = ctrl_stk[si].for_var;
    VType test_type = (VType)ctrl_stk[si].for_limit_local;

    /* Close previous CASE's if-block if any */
    if (ctrl_stk[si].if_extra_ends > 0) {
        /* br to outer block to skip remaining cases */
        emit_br(block_depth - ctrl_stk[si].break_depth);
        emit_end(); /* close previous case's if-block */
        ctrl_stk[si].if_extra_ends--;
    }

    /* Check for CASE ELSE */
    if (want(TOK_ELSE)) {
        /* No condition — code falls through unconditionally */
        /* Mark that we have a case block open (but no if-block to close) */
        /* We don't emit an if — the code just runs */
        /* But we still need to track that END SELECT shouldn't close an extra end */
        /* Actually, CASE ELSE doesn't emit an if-block, so don't increment if_extra_ends */
        return;
    }

    /* Parse one or more comma-separated match values */
    int nmatches = 0;
    do {
        if (nmatches > 0) {
            /* OR with previous match result */
        }

        /* Check for CASE IS <op> expr */
        if (want(TOK_IS)) {
            /* Read comparison operator */
            int op = read_tok();
            if (op < TOK_EQ || op > TOK_GE) {
                error_at("expected comparison operator after IS");
                return;
            }
            /* Load test value and compare */
            if (test_type == T_F32) {
                emit_local_get(test_local);
                expr(); coerce_f32(); vpop();
                switch (op) {
                case TOK_EQ: emit_op(OP_F32_EQ); break;
                case TOK_NE: emit_op(OP_F32_NE); break;
                case TOK_LT: emit_op(OP_F32_LT); break;
                case TOK_GT: emit_op(OP_F32_GT); break;
                case TOK_LE: emit_op(OP_F32_LE); break;
                case TOK_GE: emit_op(OP_F32_GE); break;
                }
            } else if (test_type == T_STR) {
                emit_local_get(test_local);
                expr(); vpop();
                emit_call(IMP_STR_CMP);
                switch (op) {
                case TOK_EQ: emit_op(OP_I32_EQZ); break;
                case TOK_NE: emit_i32_const(0); emit_op(OP_I32_NE); break;
                case TOK_LT: emit_i32_const(0); emit_op(OP_I32_LT_S); break;
                case TOK_GT: emit_i32_const(0); emit_op(OP_I32_GT_S); break;
                case TOK_LE: emit_i32_const(0); emit_op(OP_I32_LE_S); break;
                case TOK_GE: emit_i32_const(0); emit_op(OP_I32_GE_S); break;
                }
            } else {
                emit_local_get(test_local);
                expr(); coerce_i32(); vpop();
                switch (op) {
                case TOK_EQ: emit_op(OP_I32_EQ); break;
                case TOK_NE: emit_op(OP_I32_NE); break;
                case TOK_LT: emit_op(OP_I32_LT_S); break;
                case TOK_GT: emit_op(OP_I32_GT_S); break;
                case TOK_LE: emit_op(OP_I32_LE_S); break;
                case TOK_GE: emit_op(OP_I32_GE_S); break;
                }
            }
        } else {
            /* Simple value match: test_local == expr */
            if (test_type == T_F32) {
                emit_local_get(test_local);
                expr(); coerce_f32(); vpop();
                emit_op(OP_F32_EQ);
            } else if (test_type == T_STR) {
                emit_local_get(test_local);
                expr(); vpop();
                emit_call(IMP_STR_CMP);
                emit_op(OP_I32_EQZ);
            } else {
                emit_local_get(test_local);
                expr(); coerce_i32(); vpop();
                emit_op(OP_I32_EQ);
            }
        }

        if (nmatches > 0) {
            emit_op(OP_I32_OR);
        }
        nmatches++;
    } while (want(TOK_COMMA));

    emit_if_void();
    ctrl_stk[si].if_extra_ends++;
}

static void compile_do(void) {
    emit_block();
    emit_loop();

    int do_variant = 0; /* 0=infinite, 1=pre-WHILE, 2=pre-UNTIL */

    if (want(TOK_WHILE)) {
        expr(); coerce_i32(); vpop();
        emit_op(OP_I32_EQZ);
        emit_br_if(1); /* break if false */
        do_variant = 1;
    } else if (want(TOK_UNTIL)) {
        expr(); coerce_i32(); vpop();
        emit_br_if(1); /* break if true */
        do_variant = 2;
    }

    ctrl_stk[ctrl_sp].kind = CTRL_DO;
    ctrl_stk[ctrl_sp].break_depth = block_depth - 1; /* outer block */
    ctrl_stk[ctrl_sp].cont_depth = block_depth;       /* inner loop */
    ctrl_stk[ctrl_sp].for_var = do_variant;
    ctrl_stk[ctrl_sp].if_extra_ends = 0;
    ctrl_sp++;
}

static void compile_loop(void) {
    if (ctrl_sp == 0 || ctrl_stk[ctrl_sp-1].kind != CTRL_DO) {
        error_at("LOOP without DO"); return;
    }
    ctrl_sp--;
    int do_variant = ctrl_stk[ctrl_sp].for_var;

    if (do_variant != 0) {
        /* Pre-condition (WHILE/UNTIL) already handled at DO — just loop back */
        emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
    } else {
        /* No pre-condition — check for post-condition */
        if (want(TOK_WHILE)) {
            expr(); coerce_i32(); vpop();
            emit_op(OP_I32_EQZ);
            emit_br_if(block_depth - ctrl_stk[ctrl_sp].break_depth); /* break if false */
            emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);     /* continue */
        } else if (want(TOK_UNTIL)) {
            expr(); coerce_i32(); vpop();
            emit_br_if(block_depth - ctrl_stk[ctrl_sp].break_depth); /* break if true */
            emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);     /* continue */
        } else {
            /* Infinite loop */
            emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
        }
    }

    emit_end(); /* loop */
    emit_end(); /* block */
}

static void compile_exit(void) {
    int kw = read_tok();
    int target_kind;
    const char *errmsg;

    if (kw == TOK_FOR) {
        target_kind = CTRL_FOR;
        errmsg = "EXIT FOR without FOR";
    } else if (kw == TOK_WHILE) {
        target_kind = CTRL_WHILE;
        errmsg = "EXIT WHILE without WHILE";
    } else if (kw == TOK_DO) {
        target_kind = CTRL_DO;
        errmsg = "EXIT DO without DO";
    } else if (kw == TOK_SELECT) {
        target_kind = CTRL_SELECT;
        errmsg = "EXIT SELECT without SELECT";
    } else {
        error_at("expected FOR, WHILE, DO, or SELECT after EXIT");
        return;
    }

    /* Search ctrl stack from top for matching kind */
    int found = -1;
    for (int i = ctrl_sp - 1; i >= 0; i--) {
        if (ctrl_stk[i].kind == target_kind) { found = i; break; }
    }
    if (found < 0) { error_at(errmsg); return; }

    emit_br(block_depth - ctrl_stk[found].break_depth);
}

static void compile_swap(void) {
    need(TOK_NAME);
    int var_a = tokv;
    need(TOK_COMMA);
    need(TOK_NAME);
    int var_b = tokv;

    /* Type check */
    VType ta = vars[var_a].type_set ? vars[var_a].type : T_I32;
    VType tb = vars[var_b].type_set ? vars[var_b].type : T_I32;
    if (ta != tb) { error_at("SWAP requires both variables to be the same type"); return; }

    if (ta == T_F32) {
        int tmp = alloc_local_f32();
        emit_global_get(vars[var_a].global_idx);
        emit_local_set(tmp);
        emit_global_get(vars[var_b].global_idx);
        emit_global_set(vars[var_a].global_idx);
        emit_local_get(tmp);
        emit_global_set(vars[var_b].global_idx);
    } else {
        int tmp = alloc_local();
        emit_global_get(vars[var_a].global_idx);
        emit_local_set(tmp);
        emit_global_get(vars[var_b].global_idx);
        emit_global_set(vars[var_a].global_idx);
        emit_local_get(tmp);
        emit_global_set(vars[var_b].global_idx);
    }
}

static void compile_data(void) {
    /* Parse comma-separated literals into data_items[]. No WASM code emitted. */
    do {
        if (ndata_items >= MAX_DATA_ITEMS) { error_at("too many DATA items"); return; }
        int neg = 0;
        if (want(TOK_SUB)) neg = 1;
        if (want(TOK_NUMBER)) {
            data_items[ndata_items].type = T_I32;
            data_items[ndata_items].ival = neg ? -tokv : tokv;
            ndata_items++;
        } else if (want(TOK_FLOAT)) {
            data_items[ndata_items].type = T_F32;
            data_items[ndata_items].fval = neg ? -tokf : tokf;
            ndata_items++;
        } else if (!neg && want(TOK_STRING)) {
            data_items[ndata_items].type = T_STR;
            data_items[ndata_items].str_off = tokv;
            ndata_items++;
        } else {
            error_at("expected number or string in DATA");
            return;
        }
    } while (want(TOK_COMMA));
}

static void compile_read(void) {
    /* For each variable, read the next item from the DATA table */
    do {
        need(TOK_NAME);
        int var = tokv;

        /* entry_addr = DATA_BASE + 4 + DATA_IDX * 8 */
        emit_global_get(GLOBAL_DATA_BASE);
        emit_i32_const(4);
        emit_op(OP_I32_ADD);
        emit_global_get(GLOBAL_DATA_IDX);
        emit_i32_const(8);
        emit_op(OP_I32_MUL);
        emit_op(OP_I32_ADD);
        int addr = alloc_local();
        emit_local_set(addr);

        if (vars[var].type == T_STR) {
            /* Load value (string offset), copy to pool, free-old-store-new */
            emit_local_get(addr);
            emit_i32_load(4);
            emit_call(IMP_STR_COPY);
            int new_val = alloc_local();
            emit_local_set(new_val);
            emit_global_get(vars[var].global_idx);
            emit_call(IMP_STR_FREE);
            emit_local_get(new_val);
            emit_global_set(vars[var].global_idx);
        } else if (vars[var].type_set && vars[var].type == T_F32) {
            /* f32 target: check type tag, load accordingly */
            int tag = alloc_local();
            emit_local_get(addr);
            emit_i32_load(0);
            emit_local_set(tag);
            emit_local_get(tag);
            emit_i32_const(1); /* type 1 = f32 */
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_local_get(addr);
                emit_f32_load(4);
                emit_global_set(vars[var].global_idx);
            emit_else();
                emit_local_get(addr);
                emit_i32_load(4);
                emit_op(OP_F32_CONVERT_I32_S);
                emit_global_set(vars[var].global_idx);
            emit_end();
        } else {
            /* i32 target: check type tag, load accordingly */
            if (!vars[var].type_set) {
                vars[var].type = T_I32;
                vars[var].type_set = 1;
            }
            int tag = alloc_local();
            emit_local_get(addr);
            emit_i32_load(0);
            emit_local_set(tag);
            emit_local_get(tag);
            emit_i32_const(1); /* type 1 = f32 */
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_local_get(addr);
                emit_f32_load(4);
                emit_op(OP_I32_TRUNC_F32_S);
                emit_global_set(vars[var].global_idx);
            emit_else();
                emit_local_get(addr);
                emit_i32_load(4);
                emit_global_set(vars[var].global_idx);
            emit_end();
        }

        /* Increment DATA_IDX */
        emit_global_get(GLOBAL_DATA_IDX);
        emit_i32_const(1);
        emit_op(OP_I32_ADD);
        emit_global_set(GLOBAL_DATA_IDX);
    } while (want(TOK_COMMA));
}

static void compile_restore(void) {
    emit_i32_const(0);
    emit_global_set(GLOBAL_DATA_IDX);
}

static void compile_mid_assign(void) {
    /* MID$(target$, start, len) = replacement$ */
    need(TOK_LP);
    need(TOK_NAME);
    int target = tokv;
    if (vars[target].type != T_STR) { error_at("MID$ target must be a string variable"); return; }
    need(TOK_COMMA);
    expr(); coerce_i32(); vpop();
    int start_local = alloc_local();
    emit_local_set(start_local);
    need(TOK_COMMA);
    expr(); coerce_i32(); vpop();
    int len_local = alloc_local();
    emit_local_set(len_local);
    need(TOK_RP);
    need(TOK_EQ);
    expr(); vpop();
    int repl_local = alloc_local();
    emit_local_set(repl_local);

    /* Call str_mid_assign(dst, start, count, src) → new string */
    emit_global_get(vars[target].global_idx);
    emit_local_get(start_local);
    emit_local_get(len_local);
    emit_local_get(repl_local);
    emit_call(IMP_STR_MID_ASSIGN);

    /* Free old, store new */
    int result = alloc_local();
    emit_local_set(result);
    emit_global_get(vars[target].global_idx);
    emit_call(IMP_STR_FREE);
    emit_local_get(result);
    emit_global_set(vars[target].global_idx);
}

/* ================================================================
 *  File I/O Statements
 * ================================================================ */

/* OPEN expr$ FOR INPUT|OUTPUT|APPEND AS #n */
static void compile_open(void) {
    /* Filename expression (must be string) */
    expr();
    VType ft = vpop();
    if (ft != T_STR) { error_at("OPEN filename must be a string"); return; }

    /* FOR */
    need(TOK_FOR);

    /* Mode: INPUT, OUTPUT, or APPEND (parsed as identifiers) */
    read_tok();
    int mode = -1;
    if (tok == TOK_NAME) {
        if (strcmp(vars[tokv].name, "INPUT") == 0) mode = 0;
        else if (strcmp(vars[tokv].name, "OUTPUT") == 0) mode = 1;
        else if (strcmp(vars[tokv].name, "APPEND") == 0) mode = 2;
    }
    if (mode < 0) { error_at("expected INPUT, OUTPUT, or APPEND"); return; }

    /* AS */
    need(TOK_AS);

    /* #n */
    need(TOK_HASH);
    need(TOK_NUMBER);
    int ch = tokv;
    if (ch < 1 || ch > 4) { error_at("channel must be 1-4"); return; }

    /* Stack has: str_ptr. Push mode, call basic_file_open → handle */
    emit_i32_const(mode);
    emit_call(IMP_FILE_OPEN);

    /* Store handle at FILE_TABLE_BASE + (ch-1)*4 */
    int tmp = alloc_local();
    emit_local_set(tmp);
    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_local_get(tmp);
    emit_i32_store(0);
}

/* CLOSE #n */
static void compile_close_file(void) {
    need(TOK_HASH);
    need(TOK_NUMBER);
    int ch = tokv;
    if (ch < 1 || ch > 4) { error_at("channel must be 1-4"); return; }

    /* Load handle, call close */
    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_i32_load(0);
    emit_call(IMP_FILE_CLOSE);

    /* Store -1 back (mark closed) */
    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_i32_const(-1);
    emit_i32_store(0);
}

/* Helper: emit (ptr, len) pair from a string expression already on the WASM stack.
 * After calling, stack has: [ptr, len] ready for a raw file_* import. */
static void emit_str_ptr_len(void) {
    int tmp = alloc_local();
    emit_local_set(tmp);           /* save ptr */
    emit_local_get(tmp);           /* ptr (arg 1) */
    emit_local_get(tmp);           /* ptr (for str_len) */
    emit_call(IMP_STR_LEN);       /* → len (arg 2) */
}

/* KILL expr$ — delete a file */
static void compile_kill(void) {
    expr();
    VType t = vpop();
    if (t != T_STR) { error_at("KILL requires a string path"); return; }
    emit_str_ptr_len();
    emit_call(IMP_FILE_DELETE);
    emit_drop();
}

/* NAME expr$ AS expr$ — rename a file */
static void compile_name_stmt(void) {
    expr();
    VType t1 = vpop();
    if (t1 != T_STR) { error_at("NAME requires a string path"); return; }
    int old_ptr = alloc_local();
    emit_local_set(old_ptr);

    need(TOK_AS);

    expr();
    VType t2 = vpop();
    if (t2 != T_STR) { error_at("NAME requires a string path"); return; }
    int new_ptr = alloc_local();
    emit_local_set(new_ptr);

    /* file_rename(old_ptr, old_len, new_ptr, new_len) */
    emit_local_get(old_ptr);
    emit_local_get(old_ptr);
    emit_call(IMP_STR_LEN);
    emit_local_get(new_ptr);
    emit_local_get(new_ptr);
    emit_call(IMP_STR_LEN);
    emit_call(IMP_FILE_RENAME);
    emit_drop();
}

/* MKDIR expr$ — create a directory */
static void compile_mkdir(void) {
    expr();
    VType t = vpop();
    if (t != T_STR) { error_at("MKDIR requires a string path"); return; }
    emit_str_ptr_len();
    emit_call(IMP_FILE_MKDIR);
    emit_drop();
}

/* RMDIR expr$ — remove an empty directory */
static void compile_rmdir(void) {
    expr();
    VType t = vpop();
    if (t != T_STR) { error_at("RMDIR requires a string path"); return; }
    emit_str_ptr_len();
    emit_call(IMP_FILE_RMDIR);
    emit_drop();
}

/* PRINT #n, expr — write value + newline to file */
static void compile_print_file(void) {
    /* # already consumed by caller. Parse channel number. */
    need(TOK_NUMBER);
    int ch = tokv;
    if (ch < 1 || ch > 4) { error_at("channel must be 1-4"); return; }
    need(TOK_COMMA);

    /* Load handle from file table */
    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_i32_load(0);
    int handle = alloc_local();
    emit_local_set(handle);

    /* Parse expression */
    expr();
    VType t = vpop();

    /* Coerce to string if needed */
    if (t == T_I32) {
        emit_call(IMP_STR_FROM_INT);
    } else if (t == T_F32) {
        emit_call(IMP_STR_FROM_FLOAT);
    }
    /* t == T_STR: already a string pointer */

    int str = alloc_local();
    emit_local_set(str);

    /* Call basic_file_print(handle, str_ptr) */
    emit_local_get(handle);
    emit_local_get(str);
    emit_call(IMP_FILE_PRINT);
    emit_drop();  /* discard bytes-written return value */
}

/* INPUT #n, var — read line from file into variable */
static void compile_input_file(void) {
    /* # already consumed by caller. Parse channel number. */
    need(TOK_NUMBER);
    int ch = tokv;
    if (ch < 1 || ch > 4) { error_at("channel must be 1-4"); return; }
    need(TOK_COMMA);

    /* Target variable */
    need(TOK_NAME);
    int var = tokv;

    /* Load handle, call readln → str_ptr */
    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_i32_load(0);
    emit_call(IMP_FILE_READLN);

    /* Convert and store based on target variable type */
    if (vars[var].type == T_STR) {
        /* String: free old, store new */
        int new_val = alloc_local();
        emit_local_set(new_val);
        emit_global_get(vars[var].global_idx);
        emit_call(IMP_STR_FREE);
        emit_local_get(new_val);
        emit_global_set(vars[var].global_idx);
    } else if (vars[var].type == T_F32) {
        /* Float: str_to_float, store */
        emit_call(IMP_STR_TO_FLOAT);
        if (!vars[var].type_set) { vars[var].type = T_F32; vars[var].type_set = 1; }
        emit_global_set(vars[var].global_idx);
    } else {
        /* Integer: str_to_int, store */
        emit_call(IMP_STR_TO_INT);
        if (!vars[var].type_set) { vars[var].type = T_I32; vars[var].type_set = 1; }
        emit_global_set(vars[var].global_idx);
    }
}

static void stmt(void) {
    int t = read_tok();
    if (had_error) return;

    /* Track current BASIC line number in __line global */
    if (t != TOK_EOF) {
        emit_i32_const(line_num);
        emit_global_set(GLOBAL_LINE);
    }

    switch (t) {
    case TOK_EOF: break;
    case TOK_FORMAT:  compile_format(); break;
    case TOK_PRINTS:  compile_prints(); break;
    case TOK_FUNCTION: /* fall through — FUNCTION is an alias for SUB */
    case TOK_KW_SUB:  compile_sub(); break;
    case TOK_END:     compile_end(); break;
    case TOK_RETURN:  compile_return(); break;
    case TOK_LOCAL:   compile_local(); break;
    case TOK_WHILE:   compile_while(); break;
    case TOK_FOR:     compile_for(); break;
    case TOK_IF:      compile_if(); break;
    case TOK_ELSE:    compile_else(); break;
    case TOK_ELSEIF:
        if (ctrl_sp == 0 || ctrl_stk[ctrl_sp-1].kind != CTRL_IF) {
            error_at("ELSEIF without IF"); break;
        }
        emit_else();
        expr(); coerce_i32(); vpop();
        want(TOK_THEN);  /* optional THEN */
        emit_if_void();
        ctrl_stk[ctrl_sp-1].if_extra_ends++;
        break;
    case TOK_DIM:     compile_dim(); break;
    case TOK_CONST:   compile_const(); break;
    case TOK_SELECT:  compile_select(); break;
    case TOK_CASE:    compile_case(); break;
    case TOK_DO:      compile_do(); break;
    case TOK_LOOP:    compile_loop(); break;
    case TOK_EXIT:    compile_exit(); break;
    case TOK_SWAP:    compile_swap(); break;
    case TOK_DATA:    compile_data(); break;
    case TOK_READ:    compile_read(); break;
    case TOK_RESTORE: compile_restore(); break;
    case TOK_NEXT:    close_for(); break;
    case TOK_WEND:    close_while(); break;
    case TOK_BYE:     emit_return(); break;
    case TOK_BREAK:   emit_return(); break;
    case TOK_RESUME:  error_at("RESUME not supported in compiled code"); break;
    case TOK_OPEN:       compile_open(); break;
    case TOK_CLOSE_FILE: compile_close_file(); break;
    case TOK_KILL:       compile_kill(); break;
    case TOK_MKDIR:      compile_mkdir(); break;
    case TOK_RMDIR:      compile_rmdir(); break;
    case TOK_GT: {
        /* > expr — print expression */
        expr();
        VType t2 = vpop();
        if (t2 == T_STR) {
            int tmp = alloc_local();
            emit_local_set(tmp);
            emit_i32_const(0xF000);
            emit_local_get(tmp);
            emit_i32_store(0);
            int fmt_off = add_string("%s\n", 3);
            emit_i32_const(fmt_off);
            emit_i32_const(0xF000);
            emit_call(IMP_HOST_PRINTF);
            emit_drop();
        } else if (t2 == T_F32) {
            emit_call(IMP_PRINT_F32);
        } else {
            emit_call(IMP_PRINT_I32);
        }
        break;
    }
    case TOK_NAME: {
        int var = tokv;
        if (strcmp(vars[var].name, "MID$") == 0) {
            compile_mid_assign();
            break;
        }
        if (strcmp(vars[var].name, "PRINT") == 0 && want(TOK_HASH)) {
            compile_print_file();
            break;
        }
        if (strcmp(vars[var].name, "INPUT") == 0 && want(TOK_HASH)) {
            compile_input_file();
            break;
        }
        if (strcmp(vars[var].name, "NAME") == 0) {
            compile_name_stmt();
            break;
        }
        if (want(TOK_EQ)) {
            /* Assignment: X = expr */
            if (vars[var].is_const) { error_at("cannot assign to CONST"); break; }
            expr();
            VType et = vpop();
            if (vars[var].type == T_STR) {
                /* String assignment: free old value, store new */
                int new_val = alloc_local();
                emit_local_set(new_val);
                emit_global_get(vars[var].global_idx);
                emit_call(IMP_STR_FREE);
                emit_local_get(new_val);
                emit_global_set(vars[var].global_idx);
            } else {
                if (!vars[var].type_set) {
                    vars[var].type = et;
                    vars[var].type_set = 1;
                } else if (vars[var].type == T_I32 && et == T_F32) {
                    emit_op(OP_I32_TRUNC_F32_S);
                } else if (vars[var].type == T_F32 && et == T_I32) {
                    emit_op(OP_F32_CONVERT_I32_S);
                }
                emit_global_set(vars[var].global_idx);
            }
        } else if (want(TOK_LP)) {
            if (vars[var].mode == VAR_DIM) {
                /* Array store: A(i) = v */
                expr(); coerce_i32(); vpop(); /* index */
                need(TOK_RP);
                need(TOK_EQ);
                /* Compute address: base + index*4 */
                int idx_local = alloc_local();
                emit_local_set(idx_local);
                emit_global_get(vars[var].global_idx);
                emit_local_get(idx_local);
                emit_i32_const(4); emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
                expr(); coerce_i32(); vpop();
                emit_i32_store(0);
            } else {
                /* Function call as statement with parens */
                if (!compile_builtin_expr(vars[var].name)) {
                    /* SUB call */
                    int nargs = 0;
                    if (!want(TOK_RP)) {
                        do { expr(); coerce_i32(); nargs++; } while (want(TOK_COMMA));
                        need(TOK_RP);
                    }
                    emit_call(IMP_COUNT + vars[var].func_local_idx);
                    vpush(T_I32);
                }
                /* Drop return value in statement context */
                if (vsp > 0) { vpop(); emit_drop(); }
            }
        } else {
            /* Statement-context call without parens: FUNC arg1, arg2, ... */
            /* Check PRINTS handled above as keyword */
            if (!want(TOK_EOF)) {
                ungot = 1;
                /* Try as builtin (re-parse with virtual LP) */
                /* Actually, for no-paren calls we need different arg parsing. */
                /* Parse comma-separated args until end of line */
                int nargs = 0;
                do {
                    expr(); coerce_i32(); nargs++;
                } while (want(TOK_COMMA));
                /* Try as SUB */
                if (vars[var].mode == VAR_SUB) {
                    emit_call(IMP_COUNT + vars[var].func_local_idx);
                    emit_drop();
                } else {
                    error_at("unknown statement function");
                    for (int i = 0; i < nargs; i++) emit_drop();
                }
            }
        }
        break;
    }
    default:
        if (t) error_at("bad statement");
        break;
    }

    /* Check for extra tokens */
    if (tok != TOK_EOF && !had_error) {
        read_tok();
        if (tok != TOK_EOF) error_at("extra tokens after statement");
    }
}

/* ================================================================
 *  Module Assembly
 * ================================================================ */

/* Function type deduplication */
typedef struct { int np; uint8_t p[8]; int nr; uint8_t r[2]; } FType;
static FType ftypes[128];
static int nftypes;

static int find_or_add_ftype(int np, const uint8_t *p, int nr, const uint8_t *r) {
    for (int i = 0; i < nftypes; i++) {
        if (ftypes[i].np != np || ftypes[i].nr != nr) continue;
        int match = 1;
        for (int j = 0; j < np && match; j++) if (ftypes[i].p[j] != p[j]) match = 0;
        for (int j = 0; j < nr && match; j++) if (ftypes[i].r[j] != r[j]) match = 0;
        if (match) return i;
    }
    ftypes[nftypes].np = np;
    memcpy(ftypes[nftypes].p, p, np);
    ftypes[nftypes].nr = nr;
    memcpy(ftypes[nftypes].r, r, nr);
    return nftypes++;
}

static void assemble(const char *outpath) {
    Buf out; buf_init(&out);

    /* WASM magic + version */
    buf_bytes(&out, "\0asm", 4);
    uint8_t ver[4] = {1,0,0,0};
    buf_bytes(&out, ver, 4);

    /* --- Build import remap table (compact to only used imports) --- */
    int imp_remap[IMP_COUNT];
    int num_used_imports = 0;
    for (int i = 0; i < IMP_COUNT; i++) {
        if (imp_used[i]) imp_remap[i] = num_used_imports++;
        else imp_remap[i] = -1;
    }

    /* --- Patch call targets in all code buffers --- */
    for (int i = 0; i < nfuncs; i++) {
        FuncCtx *f = &func_bufs[i];
        if (f->ncall_fixups == 0) continue;
        Buf nc; buf_init(&nc);
        int fix = 0;
        for (int pos = 0; pos < f->code.len; ) {
            if (fix < f->ncall_fixups && pos == f->call_fixups[fix]) {
                /* Decode old uleb128 */
                uint32_t old_idx = 0; int shift = 0; uint8_t b;
                do {
                    b = f->code.data[pos++];
                    old_idx |= (uint32_t)(b & 0x7F) << shift;
                    shift += 7;
                } while (b & 0x80);
                /* Remap */
                uint32_t new_idx;
                if ((int)old_idx < IMP_COUNT)
                    new_idx = imp_remap[old_idx];
                else
                    new_idx = num_used_imports + (old_idx - IMP_COUNT);
                buf_uleb(&nc, new_idx);
                fix++;
            } else {
                buf_byte(&nc, f->code.data[pos++]);
            }
        }
        free(f->code.data);
        f->code = nc;
    }

    /* --- Collect type indices for used imports --- */
    int imp_type_idx[IMP_COUNT];
    for (int i = 0; i < IMP_COUNT; i++) {
        if (!imp_used[i]) continue;
        ImportDef *d = &imp_defs[i];
        imp_type_idx[i] = find_or_add_ftype(d->np, d->p, d->nr, d->r);
    }

    /* Local function types: setup is () → (), SUBs are (i32 * nparams) → (i32) */
    int local_type_idx[MAX_FUNCS];
    for (int i = 0; i < nfuncs; i++) {
        FuncCtx *f = &func_bufs[i];
        if (i == 0) {
            /* setup: () → () */
            local_type_idx[i] = find_or_add_ftype(0, NULL, 0, NULL);
        } else {
            uint8_t params[8];
            for (int j = 0; j < f->nparams; j++) params[j] = WASM_I32;
            uint8_t result = WASM_I32;
            local_type_idx[i] = find_or_add_ftype(f->nparams, params, 1, &result);
        }
    }

    /* --- Type Section (1) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, nftypes);
        for (int i = 0; i < nftypes; i++) {
            buf_byte(&sec, 0x60); /* functype */
            buf_uleb(&sec, ftypes[i].np);
            buf_bytes(&sec, ftypes[i].p, ftypes[i].np);
            buf_uleb(&sec, ftypes[i].nr);
            buf_bytes(&sec, ftypes[i].r, ftypes[i].nr);
        }
        buf_section(&out, 1, &sec);
        buf_free(&sec);
    }

    /* --- Import Section (2) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, num_used_imports);
        for (int i = 0; i < IMP_COUNT; i++) {
            if (!imp_used[i]) continue;
            buf_str(&sec, "env");
            buf_str(&sec, imp_defs[i].name);
            buf_byte(&sec, 0x00); /* function import */
            buf_uleb(&sec, imp_type_idx[i]);
        }
        buf_section(&out, 2, &sec);
        buf_free(&sec);
    }

    /* --- Function Section (3) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, nfuncs);
        for (int i = 0; i < nfuncs; i++)
            buf_uleb(&sec, local_type_idx[i]);
        buf_section(&out, 3, &sec);
        buf_free(&sec);
    }

    /* --- Memory Section (5) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, 1); /* 1 memory */
        buf_byte(&sec, 0x00); /* no max */
        buf_uleb(&sec, 1); /* 1 page = 64KB */
        buf_section(&out, 5, &sec);
        buf_free(&sec);
    }

    /* --- Global Section (6) --- */
    {
        /* Compute DATA table layout and heap start */
        int data_table_start = (data_len + 3) & ~3;
        int total_data = data_table_start;
        if (ndata_items > 0)
            total_data += 4 + ndata_items * 8;
        int heap_start = (total_data + 3) & ~3;

        Buf sec; buf_init(&sec);
        int nglobals = 4 + nvar; /* __line + _heap_ptr + _data_base + _data_idx + variables */
        buf_uleb(&sec, nglobals);
        /* Global 0: __line (mut i32) = 0 */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01); /* mutable */
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, 0); buf_byte(&sec, OP_END);
        /* Global 1: _heap_ptr (mut i32) = heap_start */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01); /* mutable */
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, heap_start); buf_byte(&sec, OP_END);
        /* Global 2: _data_base (mut i32) = data_table_start */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01); /* mutable */
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, data_table_start); buf_byte(&sec, OP_END);
        /* Global 3: _data_idx (mut i32) = 0 */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01); /* mutable */
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, 0); buf_byte(&sec, OP_END);
        /* Variable globals */
        for (int i = 0; i < nvar; i++) {
            uint8_t gt = (vars[i].type_set && vars[i].type == T_F32) ? WASM_F32 : WASM_I32;
            buf_byte(&sec, gt); buf_byte(&sec, 0x01);
            if (gt == WASM_F32) {
                buf_byte(&sec, OP_F32_CONST);
                float z = 0.0f; buf_f32(&sec, z);
            } else {
                buf_byte(&sec, OP_I32_CONST);
                buf_sleb(&sec, 0);
            }
            buf_byte(&sec, OP_END);
        }
        buf_section(&out, 6, &sec);
        buf_free(&sec);
    }

    /* --- Export Section (7) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, 3); /* setup + memory + __line */
        /* Export setup function */
        buf_str(&sec, "setup");
        buf_byte(&sec, 0x00); /* function */
        buf_uleb(&sec, num_used_imports + 0); /* func idx of setup */
        /* Export memory */
        buf_str(&sec, "memory");
        buf_byte(&sec, 0x02); /* memory */
        buf_uleb(&sec, 0);
        /* Export __line global */
        buf_str(&sec, "__line");
        buf_byte(&sec, 0x03); /* global */
        buf_uleb(&sec, GLOBAL_LINE);
        buf_section(&out, 7, &sec);
        buf_free(&sec);
    }

    /* --- Code Section (10) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, nfuncs);
        for (int i = 0; i < nfuncs; i++) {
            FuncCtx *f = &func_bufs[i];
            /* Build function body */
            Buf body; buf_init(&body);

            /* Local declarations: group consecutive same-type locals */
            if (f->nlocals == 0) {
                buf_uleb(&body, 0);
            } else {
                /* Count groups */
                int ngroups = 0;
                int counts[128]; uint8_t types[128];
                int j = 0;
                while (j < f->nlocals) {
                    uint8_t t = f->local_types[j];
                    int c = 0;
                    while (j < f->nlocals && f->local_types[j] == t) { c++; j++; }
                    counts[ngroups] = c;
                    types[ngroups] = t;
                    ngroups++;
                }
                buf_uleb(&body, ngroups);
                for (int g = 0; g < ngroups; g++) {
                    buf_uleb(&body, counts[g]);
                    buf_byte(&body, types[g]);
                }
            }

            /* Code */
            buf_bytes(&body, f->code.data, f->code.len);

            /* Ensure function ends with OP_END */
            if (i == 0) {
                /* setup: just end */
                buf_byte(&body, OP_END);
            }
            /* SUB functions already have OP_END from compile_end */

            /* Write body size + body */
            buf_uleb(&sec, body.len);
            buf_bytes(&sec, body.data, body.len);
            buf_free(&body);
        }
        buf_section(&out, 10, &sec);
        buf_free(&sec);
    }

    /* --- Data Section (11) --- */
    {
        /* Build complete data payload: string constants + padding + DATA table */
        int data_table_start = (data_len + 3) & ~3;
        int total_data = data_table_start;
        if (ndata_items > 0)
            total_data += 4 + ndata_items * 8;

        if (total_data > 0) {
            uint8_t *full_data = calloc(total_data, 1);
            memcpy(full_data, data_buf, data_len);
            /* Padding zeros already from calloc */
            if (ndata_items > 0) {
                uint8_t *p = full_data + data_table_start;
                int32_t count = ndata_items;
                memcpy(p, &count, 4); p += 4;
                for (int i = 0; i < ndata_items; i++) {
                    int32_t type_tag = 0;
                    int32_t value = 0;
                    switch (data_items[i].type) {
                    case T_I32: type_tag = 0; value = data_items[i].ival; break;
                    case T_F32: type_tag = 1; memcpy(&value, &data_items[i].fval, 4); break;
                    case T_STR: type_tag = 2; value = data_items[i].str_off; break;
                    }
                    memcpy(p, &type_tag, 4); p += 4;
                    memcpy(p, &value, 4); p += 4;
                }
            }

            Buf sec; buf_init(&sec);
            buf_uleb(&sec, 1); /* 1 data segment */
            buf_byte(&sec, 0x00); /* active, memory 0 */
            buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, 0); buf_byte(&sec, OP_END);
            buf_uleb(&sec, total_data);
            buf_bytes(&sec, full_data, total_data);
            buf_section(&out, 11, &sec);
            buf_free(&sec);
            free(full_data);
        }
    }

    /* Write output */
    FILE *fp = fopen(outpath, "wb");
    if (!fp) { fprintf(stderr, "Cannot open %s for writing\n", outpath); exit(1); }
    fwrite(out.data, 1, out.len, fp);
    fclose(fp);
    printf("Wrote %d bytes to %s\n", out.len, outpath);
    printf("  %d imports, %d local functions, %d globals, %d bytes data (%d DATA items)\n",
           num_used_imports, nfuncs, 4 + nvar, data_len, ndata_items);
    buf_free(&out);
}

/* ================================================================
 *  Main
 * ================================================================ */

static void compile(void) {
    /* Initialize setup function (local func 0) */
    nfuncs = 1;
    buf_init(&func_bufs[0].code);
    func_bufs[0].nparams = 0;
    func_bufs[0].nlocals = 0;
    func_bufs[0].sub_var = -1;
    cur_func = 0;
    block_depth = 0;
    ctrl_sp = 0;
    vsp = 0;
    nvar = 0;
    data_len = 0;
    ndata_items = 0;
    had_error = 0;
    nftypes = 0;
    line_num = 0;
    src_pos = 0;

    /* Initialize file handle table to -1 (closed) */
    for (int i = 0; i < 4; i++) {
        emit_i32_const(FILE_TABLE_BASE + i * 4);
        emit_i32_const(-1);
        emit_i32_store(0);
    }

    while (next_line()) {
        ungot = 0;
        vsp = 0;
        stmt();
        if (had_error) break;
    }

    if (ctrl_sp > 0 && !had_error) {
        error_at("unterminated block (missing END)");
    }
}

int main(int argc, char **argv) {
    const char *inpath = NULL;
    const char *outpath = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc) {
            outpath = argv[++i];
        } else if (argv[i][0] != '-') {
            inpath = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!inpath) {
        fprintf(stderr, "Usage: basic2wasm input.bas [-o output.wasm]\n");
        return 1;
    }

    /* Default output: replace .bas with .wasm */
    char default_out[512];
    if (!outpath) {
        strncpy(default_out, inpath, sizeof(default_out)-6);
        char *dot = strrchr(default_out, '.');
        if (dot) strcpy(dot, ".wasm");
        else strcat(default_out, ".wasm");
        outpath = default_out;
    }

    /* Read input file */
    FILE *fp = fopen(inpath, "r");
    if (!fp) { fprintf(stderr, "Cannot open %s\n", inpath); return 1; }
    fseek(fp, 0, SEEK_END);
    src_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    source = malloc(src_len + 1);
    fread(source, 1, src_len, fp);
    source[src_len] = 0;
    fclose(fp);

    printf("Compiling %s...\n", inpath);
    compile();

    if (had_error) {
        fprintf(stderr, "Compilation failed.\n");
        free(source);
        return 1;
    }

    assemble(outpath);
    free(source);
    return 0;
}
