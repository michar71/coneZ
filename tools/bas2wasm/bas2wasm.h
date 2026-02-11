/*
 * bas2wasm.h — shared types, constants, and inline helpers
 */
#ifndef BAS2WASM_H
#define BAS2WASM_H

#define BAS2WASM_VERSION_MAJOR 0
#define BAS2WASM_VERSION_MINOR 1

#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

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

void buf_init(Buf *b);
void buf_grow(Buf *b, int need);
void buf_byte(Buf *b, uint8_t v);
void buf_bytes(Buf *b, const void *p, int n);
void buf_free(Buf *b);
void buf_uleb(Buf *b, uint32_t v);
void buf_sleb(Buf *b, int32_t v);
void buf_f32(Buf *b, float v);
void buf_str(Buf *b, const char *s);
void buf_section(Buf *out, int id, Buf *content);

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
 *  Import Table
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
    IMP_LERP, IMP_LARP, IMP_LARPF,
    IMP_COUNT
};

extern ImportDef imp_defs[IMP_COUNT];
extern uint8_t imp_used[IMP_COUNT];

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
    uint8_t param_types[8]; /* WASM_I32 or WASM_F32 per param */
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

/* DATA items (compile-time collection, assembled into data section) */
#define MAX_DATA_ITEMS 1024
typedef struct { VType type; int32_t ival; float fval; int str_off; } DataItem;

/* Constant folding */
typedef struct {
    int valid;       /* 0=none, 1=i32, 2=f32 */
    int buf_start;   /* CODE->len before emit */
    int buf_end;     /* CODE->len after emit */
    int32_t ival;
    float fval;
} FoldSlot;

/* Global compiler state */
extern Var vars[MAX_VARS];
extern int nvar;
extern FuncCtx func_bufs[MAX_FUNCS];
extern int nfuncs;
extern int cur_func;
extern CtrlEntry ctrl_stk[MAX_CTRL];
extern int ctrl_sp;
extern int block_depth;

extern char data_buf[MAX_STRINGS];
extern int data_len;

extern DataItem data_items[MAX_DATA_ITEMS];
extern int ndata_items;

extern char *source;
extern int src_len;
extern int src_pos;
extern char line_buf[512];
extern char *lp;
extern int line_num;

extern int tok, tokv, ungot;
extern float tokf;
extern char tokn[16];

extern VType vstack[64];
extern int vsp;

extern int had_error;

extern FoldSlot fold_a, fold_b;

/* ================================================================
 *  Lexer tokens
 * ================================================================ */

enum {
    TOK_EOF=0, TOK_NAME=1, TOK_NUMBER=2, TOK_STRING=3, TOK_FLOAT=4,
    TOK_LP=5, TOK_RP=6, TOK_COMMA=7,
    TOK_ADD=8, TOK_SUB=9, TOK_MUL=10, TOK_DIV=11, TOK_IDIV=12,
    TOK_EQ=13, TOK_LT=14, TOK_GT=15,
    TOK_NE=16, TOK_LE=17, TOK_GE=18,
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
    TOK_HASH=64,
    TOK_POW=65
};

/* ================================================================
 *  Helpers (always-inline to avoid multi-definition)
 * ================================================================ */

#define CODE (&func_bufs[cur_func].code)

static inline void error_at(const char *msg) {
    fprintf(stderr, "ERROR line %d: %s\n", line_num, msg);
    had_error = 1;
}

static inline int find_var(const char *name) {
    for (int i = 0; i < nvar; i++)
        if (strcmp(vars[i].name, name) == 0) return i;
    return -1;
}

static inline int add_var(const char *name) {
    int i = find_var(name);
    if (i >= 0) return i;
    if (nvar >= MAX_VARS) { error_at("too many variables"); return 0; }
    memset(&vars[nvar], 0, sizeof(Var));
    snprintf(vars[nvar].name, sizeof(vars[nvar].name), "%s", name);
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
    vars[nvar].global_idx = nvar + 4;
    return nvar++;
}

static inline int alloc_local(void) {
    FuncCtx *f = &func_bufs[cur_func];
    if (f->nlocals >= 128) { error_at("too many locals in function"); return 0; }
    int idx = f->nparams + f->nlocals;
    f->local_types[f->nlocals++] = WASM_I32;
    return idx;
}

static inline int alloc_local_f32(void) {
    FuncCtx *f = &func_bufs[cur_func];
    if (f->nlocals >= 128) { error_at("too many locals in function"); return 0; }
    int idx = f->nparams + f->nlocals;
    f->local_types[f->nlocals++] = WASM_F32;
    return idx;
}

static inline int add_string(const char *s, int len) {
    if (data_len + len + 1 > MAX_STRINGS) { error_at("string table full"); return 0; }
    int off = data_len;
    memcpy(data_buf + data_len, s, len);
    data_buf[data_len + len] = 0;
    data_len += len + 1;
    return off;
}

static inline void vpush(VType t) {
    if (vsp >= 64) { error_at("expression too complex"); return; }
    vstack[vsp++] = t;
}
static inline VType vpop(void) {
    if (vsp <= 0) { error_at("internal: value stack underflow"); return T_I32; }
    return vstack[--vsp];
}

/* ================================================================
 *  Emit Helpers
 * ================================================================ */

static inline void emit_op(int op) { buf_byte(CODE, op); }

static inline void emit_i32_const(int32_t v) {
    fold_a = fold_b;
    fold_b.valid = 1;
    fold_b.buf_start = CODE->len;
    buf_byte(CODE, OP_I32_CONST); buf_sleb(CODE, v);
    fold_b.buf_end = CODE->len;
    fold_b.ival = v;
}
static inline void emit_f32_const(float v) {
    fold_a = fold_b;
    fold_b.valid = 2;
    fold_b.buf_start = CODE->len;
    buf_byte(CODE, OP_F32_CONST); buf_f32(CODE, v);
    fold_b.buf_end = CODE->len;
    fold_b.fval = v;
}
static inline void emit_call(int func_idx) {
    buf_byte(CODE, OP_CALL);
    FuncCtx *f = &func_bufs[cur_func];
    if (f->ncall_fixups >= 512) { error_at("too many call sites in function"); }
    else f->call_fixups[f->ncall_fixups++] = f->code.len;
    buf_uleb(CODE, func_idx);
    if (func_idx < IMP_COUNT) imp_used[func_idx] = 1;
}
static inline void emit_global_get(int idx) {
    buf_byte(CODE, OP_GLOBAL_GET); buf_uleb(CODE, idx);
}
static inline void emit_global_set(int idx) {
    buf_byte(CODE, OP_GLOBAL_SET); buf_uleb(CODE, idx);
}
static inline void emit_local_get(int idx) {
    buf_byte(CODE, OP_LOCAL_GET); buf_uleb(CODE, idx);
}
static inline void emit_local_set(int idx) {
    buf_byte(CODE, OP_LOCAL_SET); buf_uleb(CODE, idx);
}
static inline void emit_i32_load(int offset) {
    buf_byte(CODE, OP_I32_LOAD); buf_uleb(CODE, 2); buf_uleb(CODE, offset);
}
static inline void emit_i32_store(int offset) {
    buf_byte(CODE, OP_I32_STORE); buf_uleb(CODE, 2); buf_uleb(CODE, offset);
}
static inline void emit_f32_load(int offset) {
    buf_byte(CODE, OP_F32_LOAD); buf_uleb(CODE, 2); buf_uleb(CODE, offset);
}
static inline void emit_block(void)  { buf_byte(CODE, OP_BLOCK); buf_byte(CODE, WASM_VOID); block_depth++; }
static inline void emit_loop(void)   { buf_byte(CODE, OP_LOOP);  buf_byte(CODE, WASM_VOID); block_depth++; }
static inline void emit_if_void(void){ buf_byte(CODE, OP_IF);    buf_byte(CODE, WASM_VOID); block_depth++; }
static inline void emit_else(void)   { buf_byte(CODE, OP_ELSE); }
static inline void emit_end(void)    { buf_byte(CODE, OP_END); block_depth--; }
static inline void emit_br(int d)    { buf_byte(CODE, OP_BR); buf_uleb(CODE, d); }
static inline void emit_br_if(int d) { buf_byte(CODE, OP_BR_IF); buf_uleb(CODE, d); }
static inline void emit_drop(void)   { buf_byte(CODE, OP_DROP); }
static inline void emit_return(void) { buf_byte(CODE, OP_RETURN); }

static inline void coerce_i32(void) {
    if (vsp > 0 && vstack[vsp-1] == T_STR) {
        error_at("cannot use string in numeric context");
        return;
    }
    if (vsp > 0 && vstack[vsp-1] == T_F32) {
        emit_op(OP_I32_TRUNC_F32_S);
        vstack[vsp-1] = T_I32;
    }
}

static inline void coerce_f32(void) {
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
 *  Cross-file function declarations
 * ================================================================ */

/* lexer.c */
int next_line(void);
int read_tok(void);
int want(int t);
void need(int t);

/* expr.c */
void expr(void);
void base_expr(void);
int compile_builtin_expr(const char *name);

/* stmt.c */
void stmt(void);

/* assemble.c */
void assemble(const char *outpath);

/* main.c */
void compile(void);

#endif /* BAS2WASM_H */
