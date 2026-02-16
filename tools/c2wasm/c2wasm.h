/*
 * c2wasm.h — shared types, constants, and inline helpers for C-to-WASM compiler
 */
#ifndef C2WASM_H
#define C2WASM_H

#define C2WASM_VERSION_MAJOR 0
#define C2WASM_VERSION_MINOR 1
#define CONEZ_API_VERSION    0

#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>

#include "c2wasm_platform.h"

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
void buf_sleb64(Buf *b, int64_t v);
void buf_f32(Buf *b, float v);
void buf_f64(Buf *b, double v);
void buf_str(Buf *b, const char *s);
void buf_section(Buf *out, int id, Buf *content);

/* ================================================================
 *  WASM Opcodes & Types
 * ================================================================ */

#define OP_UNREACHABLE   0x00
#define OP_NOP           0x01
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
#define OP_I64_LOAD      0x29
#define OP_F32_LOAD      0x2A
#define OP_F64_LOAD      0x2B
#define OP_I32_LOAD8_S   0x2C
#define OP_I32_LOAD8_U   0x2D
#define OP_I32_LOAD16_S  0x2E
#define OP_I32_LOAD16_U  0x2F
#define OP_I32_STORE     0x36
#define OP_I64_STORE     0x37
#define OP_F32_STORE     0x38
#define OP_F64_STORE     0x39
#define OP_I32_STORE8    0x3A
#define OP_I32_STORE16   0x3B

#define OP_I32_CONST     0x41
#define OP_I64_CONST     0x42
#define OP_F32_CONST     0x43
#define OP_F64_CONST     0x44

#define OP_I32_EQZ       0x45
#define OP_I32_EQ        0x46
#define OP_I32_NE        0x47
#define OP_I32_LT_S      0x48
#define OP_I32_LT_U      0x49
#define OP_I32_GT_S      0x4A
#define OP_I32_GT_U      0x4B
#define OP_I32_LE_S      0x4C
#define OP_I32_LE_U      0x4D
#define OP_I32_GE_S      0x4E
#define OP_I32_GE_U      0x4F

#define OP_I64_EQZ       0x50
#define OP_I64_EQ        0x51
#define OP_I64_NE        0x52
#define OP_I64_LT_S      0x53
#define OP_I64_LT_U      0x54
#define OP_I64_GT_S      0x55
#define OP_I64_GT_U      0x56
#define OP_I64_LE_S      0x57
#define OP_I64_LE_U      0x58
#define OP_I64_GE_S      0x59
#define OP_I64_GE_U      0x5A

#define OP_F32_EQ        0x5B
#define OP_F32_NE        0x5C
#define OP_F32_LT        0x5D
#define OP_F32_GT        0x5E
#define OP_F32_LE        0x5F
#define OP_F32_GE        0x60

#define OP_F64_EQ        0x61
#define OP_F64_NE        0x62
#define OP_F64_LT        0x63
#define OP_F64_GT        0x64
#define OP_F64_LE        0x65
#define OP_F64_GE        0x66

#define OP_I32_ADD       0x6A
#define OP_I32_SUB       0x6B
#define OP_I32_MUL       0x6C
#define OP_I32_DIV_S     0x6D
#define OP_I32_DIV_U     0x6E
#define OP_I32_REM_S     0x6F
#define OP_I32_REM_U     0x70
#define OP_I32_AND       0x71
#define OP_I32_OR        0x72
#define OP_I32_XOR       0x73
#define OP_I32_SHL       0x74
#define OP_I32_SHR_S     0x75
#define OP_I32_SHR_U     0x76

#define OP_I64_ADD       0x7C
#define OP_I64_SUB       0x7D
#define OP_I64_MUL       0x7E
#define OP_I64_DIV_S     0x7F
#define OP_I64_DIV_U     0x80
#define OP_I64_REM_S     0x81
#define OP_I64_REM_U     0x82
#define OP_I64_AND       0x83
#define OP_I64_OR        0x84
#define OP_I64_XOR       0x85
#define OP_I64_SHL       0x86
#define OP_I64_SHR_S     0x87
#define OP_I64_SHR_U     0x88

#define OP_F32_ABS       0x8B
#define OP_F32_NEG       0x8C
#define OP_F32_CEIL      0x8D
#define OP_F32_FLOOR     0x8E
#define OP_F32_TRUNC     0x8F
#define OP_F32_SQRT      0x91
#define OP_F32_ADD       0x92
#define OP_F32_SUB       0x93
#define OP_F32_MUL       0x94
#define OP_F32_DIV       0x95
#define OP_F32_MIN       0x96
#define OP_F32_MAX       0x97

#define OP_F64_ABS       0x99
#define OP_F64_NEG       0x9A
#define OP_F64_CEIL      0x9B
#define OP_F64_FLOOR     0x9C
#define OP_F64_TRUNC     0x9D
#define OP_F64_SQRT      0x9F
#define OP_F64_ADD       0xA0
#define OP_F64_SUB       0xA1
#define OP_F64_MUL       0xA2
#define OP_F64_DIV       0xA3
#define OP_F64_MIN       0xA4
#define OP_F64_MAX       0xA5

#define OP_I32_WRAP_I64      0xA7
#define OP_I32_TRUNC_F32_S   0xA8
#define OP_I32_TRUNC_F32_U   0xA9
#define OP_I32_TRUNC_F64_S   0xAA
#define OP_I32_TRUNC_F64_U   0xAB
#define OP_I64_EXTEND_I32_S  0xAC
#define OP_I64_EXTEND_I32_U  0xAD
#define OP_I64_TRUNC_F32_S   0xAE
#define OP_I64_TRUNC_F32_U   0xAF
#define OP_I64_TRUNC_F64_S   0xB0
#define OP_I64_TRUNC_F64_U   0xB1
#define OP_F32_CONVERT_I32_S 0xB2
#define OP_F32_CONVERT_I32_U 0xB3
#define OP_F32_CONVERT_I64_S 0xB4
#define OP_F32_CONVERT_I64_U 0xB5
#define OP_F32_DEMOTE_F64    0xB6
#define OP_F64_CONVERT_I32_S 0xB7
#define OP_F64_CONVERT_I32_U 0xB8
#define OP_F64_CONVERT_I64_S 0xB9
#define OP_F64_CONVERT_I64_U 0xBA
#define OP_F64_PROMOTE_F32   0xBB

#define WASM_I32  0x7F
#define WASM_I64  0x7E
#define WASM_F32  0x7D
#define WASM_F64  0x7C
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
    IMP_FMODF, IMP_TANF, IMP_EXPF, IMP_LOGF, IMP_LOG2F,
    IMP_ASINF, IMP_ACOSF, IMP_ATANF,
    IMP_GET_ORIGIN_LAT, IMP_GET_ORIGIN_LON,
    IMP_FILE_OPEN, IMP_FILE_CLOSE, IMP_FILE_READ, IMP_FILE_WRITE,
    IMP_FILE_SIZE, IMP_FILE_SEEK, IMP_FILE_TELL,
    IMP_FILE_EXISTS, IMP_FILE_DELETE, IMP_FILE_RENAME,
    IMP_FILE_MKDIR, IMP_FILE_RMDIR,
    IMP_GET_EPOCH_MS, IMP_GET_UPTIME_MS, IMP_GET_LAST_COMM_MS,
    IMP_PRINT_I64, IMP_PRINT_F64,
    IMP_HOST_SNPRINTF,
    IMP_SIN, IMP_COS, IMP_TAN, IMP_ASIN, IMP_ACOS, IMP_ATAN,
    IMP_ATAN2, IMP_POW, IMP_EXP, IMP_LOG, IMP_LOG2, IMP_FMOD,
    IMP_LERP, IMP_LARP, IMP_LARPF,
    IMP_MALLOC, IMP_FREE, IMP_CALLOC, IMP_REALLOC,
    IMP_INFLATE_FILE, IMP_INFLATE_FILE_TO_MEM, IMP_INFLATE_MEM,
    IMP_COUNT
};

extern const ImportDef imp_defs[IMP_COUNT];
extern uint8_t imp_used[IMP_COUNT];

/* ================================================================
 *  C Type System
 * ================================================================ */

typedef enum {
    CT_VOID = 0,
    CT_CHAR,       /* i32 */
    CT_INT,        /* i32 */
    CT_LONG_LONG,  /* i64 */
    CT_FLOAT,      /* f32 */
    CT_DOUBLE,     /* f64 */
    CT_CONST_STR,  /* pointer to string literal (i32) */
    CT_UINT,       /* unsigned i32 */
    CT_ULONG_LONG, /* unsigned i64 */
} CType;

/* Extended type information for pointers and arrays */
#define MAX_TYPE_DEPTH 4  /* Max pointer/array nesting depth */

typedef enum {
    TYPE_BASE,
    TYPE_POINTER,
    TYPE_ARRAY
} TypeKind;

typedef struct {
    TypeKind kinds[MAX_TYPE_DEPTH];
    CType base;
    int sizes[MAX_TYPE_DEPTH];  /* For arrays: element count, -1 for pointers */
    int depth;  /* Total depth of pointer/array nesting */
} TypeInfo;

/* Type constructors */
TypeInfo type_base(CType ct);
TypeInfo type_pointer(TypeInfo base);
TypeInfo type_array(TypeInfo base, int size);
TypeInfo type_decay(TypeInfo t);  /* array -> pointer */

/* Type predicates */
int type_is_pointer(TypeInfo t);
int type_is_array(TypeInfo t);
int type_is_scalar(TypeInfo t);
CType type_base_ctype(TypeInfo t);
int type_element_size(TypeInfo t);  /* Size of element when dereferenced */
int type_sizeof(TypeInfo t);        /* Total size of type */

/* ================================================================
 *  Symbol Table
 * ================================================================ */

#ifdef C2WASM_EMBEDDED
#define MAX_SYMS     256
#define MAX_FUNCS    16
#define MAX_CTRL     32
#define MAX_STRINGS  4096
#define CW_MAX_LOCALS  64
#define CW_MAX_FIXUPS  128
#else
#define MAX_SYMS     512
#define MAX_FUNCS    64
#define MAX_CTRL     64
#define MAX_STRINGS  16384
#define CW_MAX_LOCALS  256
#define CW_MAX_FIXUPS  1024
#endif
#define FMT_BUF_ADDR 0xF000

typedef enum {
    SYM_GLOBAL,    /* global variable */
    SYM_LOCAL,     /* function-local variable */
    SYM_FUNC,      /* user-defined function */
    SYM_IMPORT,    /* ConeZ API import */
    SYM_DEFINE,    /* #define macro */
} SymKind;

typedef struct {
#ifdef C2WASM_EMBEDDED
    char name[32];
    /* macro value — heap-allocated in embedded mode */
    char *macro_val;
#else
    char name[64];
    /* macro value */
    char macro_val[128];
#endif
    SymKind kind;
    CType ctype;        /* return type for functions, var type for vars */
    TypeInfo type_info; /* extended type info for pointers/arrays */
    int idx;            /* WASM global/local/func index */
    int imp_id;         /* IMP_xxx for imports, -1 otherwise */
    int scope;          /* scope depth (0=global) */
    /* function info */
    int param_count;
    CType param_types[8];
    int is_static;
    int is_const;       /* 1 if variable is const-qualified */
    int is_defined;     /* 1 if function body has been compiled */
    int is_float_macro; /* 1 if macro value is a float */
    /* global init value (for SYM_GLOBAL) */
    int32_t init_ival;
    float init_fval;
    double init_dval;
    int64_t init_llval;
    /* local variable info */
    int stack_offset;   /* Stack frame offset for locals (for & operator) */
    int is_lvalue;      /* 1 if this symbol can be assigned to */
    int is_mem_backed;  /* local scalar spilled to linear memory */
    int mem_off;        /* linear memory offset for spilled local */
} Symbol;

/* Null-safe macro_val check (pointer in embedded, char[] in standalone) */
#ifdef C2WASM_EMBEDDED
#define HAS_MACRO_VAL(s) ((s)->macro_val && (s)->macro_val[0])
#else
#define HAS_MACRO_VAL(s) ((s)->macro_val[0])
#endif

static inline void set_macro_val(Symbol *s, const char *value) {
#ifdef C2WASM_EMBEDDED
    cw_free(s->macro_val);
    int len = (int)strlen(value);
    s->macro_val = (char *)cw_malloc(len + 1);
    if (s->macro_val) memcpy(s->macro_val, value, len + 1);
#else
    snprintf(s->macro_val, sizeof(s->macro_val), "%s", value);
#endif
}

/* ================================================================
 *  Function Context (code generation)
 * ================================================================ */

typedef struct {
    Buf code;
    int nparams;
    uint8_t param_wasm_types[8];
    CType param_ctypes[8];
    int nlocals;
    uint8_t local_types[CW_MAX_LOCALS];
    char *name;          /* function name for export */
    CType return_type;
    int call_fixups[CW_MAX_FIXUPS];
    int ncall_fixups;
} FuncCtx;

/* ================================================================
 *  Control Flow Stack
 * ================================================================ */

enum { CTRL_IF, CTRL_FOR, CTRL_WHILE, CTRL_DO, CTRL_SWITCH, CTRL_BLOCK };

typedef struct {
    int kind;
    int break_depth;
    int cont_depth;
    /* for-loop increment buffer */
    Buf *incr_buf;
} CtrlEntry;

/* ================================================================
 *  Tokens
 * ================================================================ */

enum {
    TOK_EOF = 0, TOK_NAME, TOK_INT_LIT, TOK_FLOAT_LIT, TOK_DOUBLE_LIT, TOK_STR_LIT, TOK_CHAR_LIT,
    /* punctuation */
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMI, TOK_COMMA, TOK_DOT, TOK_ARROW,
    /* operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_BANG,
    TOK_LT, TOK_GT, TOK_LSHIFT, TOK_RSHIFT,
    TOK_EQ, TOK_NE, TOK_LE, TOK_GE,
    TOK_AND_AND, TOK_OR_OR,
    TOK_ASSIGN, TOK_PLUS_EQ, TOK_MINUS_EQ, TOK_STAR_EQ, TOK_SLASH_EQ,
    TOK_PERCENT_EQ, TOK_AMP_EQ, TOK_PIPE_EQ, TOK_CARET_EQ,
    TOK_LSHIFT_EQ, TOK_RSHIFT_EQ,
    TOK_INC, TOK_DEC,
    TOK_QUESTION, TOK_COLON,
    /* keywords */
    TOK_IF, TOK_ELSE, TOK_FOR, TOK_WHILE, TOK_DO, TOK_SWITCH,
    TOK_CASE, TOK_DEFAULT, TOK_BREAK, TOK_CONTINUE, TOK_RETURN,
    TOK_INT, TOK_FLOAT, TOK_DOUBLE, TOK_VOID, TOK_CHAR,
    TOK_STATIC, TOK_CONST, TOK_UNSIGNED, TOK_LONG,
    TOK_SHORT, TOK_SIGNED, TOK_BOOL,
    TOK_INT8, TOK_INT16, TOK_INT32, TOK_INT64, TOK_SIZE_T,
    TOK_UINT8, TOK_UINT16, TOK_UINT32, TOK_UINT64,
    TOK_SIZEOF,
    /* preprocessor (returned by preproc layer) */
    TOK_PP_DONE,  /* preprocessor line consumed, get next token */
};

/* ================================================================
 *  Global Compiler State
 * ================================================================ */

#ifdef C2WASM_EMBEDDED
extern Symbol *syms;
extern FuncCtx *func_bufs;
extern CtrlEntry *ctrl_stk;
extern char *data_buf;
#else
extern Symbol syms[MAX_SYMS];
extern FuncCtx func_bufs[MAX_FUNCS];
extern CtrlEntry ctrl_stk[MAX_CTRL];
extern char data_buf[MAX_STRINGS];
#endif
extern int nsym;
extern int cur_scope;
extern int nfuncs;
extern int cur_func;
extern int ctrl_sp;
extern int block_depth;
extern int data_len;

extern char *source;
extern int src_len;
extern int src_pos;
extern int line_num;
extern char *src_file;

extern int tok;
extern int tok_ival;
extern int64_t tok_i64;
extern int tok_int_is_64;
extern int tok_int_unsigned;
extern float tok_fval;
extern double tok_dval;      /* double-precision float literal value */
extern char tok_sval[1024];  /* string/name value */
extern int tok_slen;         /* string literal length */

extern int had_error;
extern int nglobals;    /* number of WASM globals (0=_heap_ptr, 1+=user) */

#define GLOBAL_HEAP_PTR 0
#define GLOBAL_LINE     1

extern int has_setup;
extern int has_loop;
extern int type_had_pointer;
extern int type_had_const;
extern int type_had_unsigned;

/* ================================================================
 *  Helpers
 * ================================================================ */

#define CODE (&func_bufs[cur_func].code)

static inline void error_at(const char *msg) {
    cw_error("%s:%d: error: %s\n", src_file ? src_file : "<input>", line_num, msg);
    had_error = 1;
}

static inline void error_fmt(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    error_at(buf);
}

static inline Symbol *find_sym(const char *name) {
    /* Search from end to find innermost scope first */
    for (int i = nsym - 1; i >= 0; i--)
        if (strcmp(syms[i].name, name) == 0 && syms[i].scope <= cur_scope)
            return &syms[i];
    return NULL;
}

static inline Symbol *find_sym_kind(const char *name, SymKind kind) {
    for (int i = nsym - 1; i >= 0; i--)
        if (syms[i].kind == kind && strcmp(syms[i].name, name) == 0)
            return &syms[i];
    return NULL;
}

static inline Symbol *add_sym(const char *name, SymKind kind, CType ct) {
    if (nsym >= MAX_SYMS) { cw_fatal("%s:%d: error: too many symbols\n", src_file ? src_file : "<input>", line_num); }
    Symbol *s = &syms[nsym++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->kind = kind;
    s->ctype = ct;
    s->imp_id = -1;
    s->scope = cur_scope;
    return s;
}

static inline int add_string(const char *s, int len) {
    if (data_len + len + 1 > MAX_STRINGS) { error_at("string table full"); return 0; }
    int off = data_len;
    memcpy(data_buf + data_len, s, len);
    data_buf[data_len + len] = 0;
    data_len += len + 1;
    return off;
}

static inline int add_data_zeros(int size, int align) {
    if (align < 1) align = 1;
    int off = (data_len + (align - 1)) & ~(align - 1);
    if (off + size > MAX_STRINGS) { error_at("data section full"); return 0; }
    if (off > data_len) memset(data_buf + data_len, 0, off - data_len);
    memset(data_buf + off, 0, size);
    data_len = off + size;
    return off;
}

static inline int ctype_is_unsigned(CType ct) {
    return ct == CT_UINT || ct == CT_ULONG_LONG;
}

static inline uint8_t ctype_to_wasm(CType ct) {
    switch (ct) {
    case CT_LONG_LONG:
    case CT_ULONG_LONG: return WASM_I64;
    case CT_FLOAT:      return WASM_F32;
    case CT_DOUBLE:     return WASM_F64;
    default:            return WASM_I32;
    }
}

static inline int alloc_local(uint8_t wtype) {
    FuncCtx *f = &func_bufs[cur_func];
    if (f->nlocals >= CW_MAX_LOCALS) { cw_fatal("%s:%d: error: too many locals\n", src_file ? src_file : "<input>", line_num); }
    int idx = f->nparams + f->nlocals;
    f->local_types[f->nlocals++] = wtype;
    return idx;
}

/* ================================================================
 *  Emit Helpers
 * ================================================================ */

static inline void emit_op(int op) { buf_byte(CODE, op); }

static inline void emit_i32_const(int32_t v) {
    buf_byte(CODE, OP_I32_CONST); buf_sleb(CODE, v);
}
static inline void emit_f32_const(float v) {
    buf_byte(CODE, OP_F32_CONST); buf_f32(CODE, v);
}
static inline void emit_f64_const(double v) {
    buf_byte(CODE, OP_F64_CONST); buf_f64(CODE, v);
}
static inline void emit_i64_const(int64_t v) {
    buf_byte(CODE, OP_I64_CONST); buf_sleb64(CODE, v);
}
static inline void emit_call(int func_idx) {
    buf_byte(CODE, OP_CALL);
    FuncCtx *f = &func_bufs[cur_func];
    if (f->ncall_fixups >= CW_MAX_FIXUPS) { error_at("too many call sites"); return; }
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
static inline void emit_local_tee(int idx) {
    buf_byte(CODE, OP_LOCAL_TEE); buf_uleb(CODE, idx);
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
static inline void emit_f32_store(int offset) {
    buf_byte(CODE, OP_F32_STORE); buf_uleb(CODE, 2); buf_uleb(CODE, offset);
}
static inline void emit_block(void)  { buf_byte(CODE, OP_BLOCK); buf_byte(CODE, WASM_VOID); block_depth++; }
static inline void emit_loop(void)   { buf_byte(CODE, OP_LOOP);  buf_byte(CODE, WASM_VOID); block_depth++; }
static inline void emit_if_void(void){ buf_byte(CODE, OP_IF);    buf_byte(CODE, WASM_VOID); block_depth++; }
static inline void emit_if_i32(void) { buf_byte(CODE, OP_IF);    buf_byte(CODE, WASM_I32);  block_depth++; }
static inline void emit_if_f32(void) { buf_byte(CODE, OP_IF);    buf_byte(CODE, WASM_F32);  block_depth++; }
static inline void emit_if_f64(void) { buf_byte(CODE, OP_IF);    buf_byte(CODE, WASM_F64);  block_depth++; }
static inline void emit_if_i64(void) { buf_byte(CODE, OP_IF);    buf_byte(CODE, WASM_I64);  block_depth++; }
static inline void emit_else(void)   { buf_byte(CODE, OP_ELSE); }
static inline void emit_end(void)    { buf_byte(CODE, OP_END); block_depth--; }
static inline void emit_br(int d)    { buf_byte(CODE, OP_BR); buf_uleb(CODE, d); }
static inline void emit_br_if(int d) { buf_byte(CODE, OP_BR_IF); buf_uleb(CODE, d); }
static inline void emit_drop(void)   { buf_byte(CODE, OP_DROP); }
static inline void emit_return(void) { buf_byte(CODE, OP_RETURN); }

/* Coerce top of wasm stack to i32 (signed target) */
static inline void emit_coerce_i32(CType from) {
    if (from == CT_FLOAT) emit_op(OP_I32_TRUNC_F32_S);
    else if (from == CT_DOUBLE) emit_op(OP_I32_TRUNC_F64_S);
    else if (from == CT_LONG_LONG || from == CT_ULONG_LONG) emit_op(OP_I32_WRAP_I64);
    /* CT_UINT → already i32, no-op */
}
/* Coerce top of wasm stack to i64 (sign/zero-extend based on source) */
static inline void emit_coerce_i64(CType from) {
    if (from == CT_INT || from == CT_CHAR || from == CT_CONST_STR)
        emit_op(OP_I64_EXTEND_I32_S);
    else if (from == CT_UINT)
        emit_op(OP_I64_EXTEND_I32_U);
    else if (from == CT_FLOAT) emit_op(OP_I64_TRUNC_F32_S);
    else if (from == CT_DOUBLE) emit_op(OP_I64_TRUNC_F64_S);
    /* CT_ULONG_LONG → already i64, no-op */
}
/* Coerce top of wasm stack to f32 */
static inline void emit_coerce_f32(CType from) {
    if (from == CT_INT || from == CT_CHAR || from == CT_CONST_STR)
        emit_op(OP_F32_CONVERT_I32_S);
    else if (from == CT_UINT)
        emit_op(OP_F32_CONVERT_I32_U);
    else if (from == CT_LONG_LONG)
        emit_op(OP_F32_CONVERT_I64_S);
    else if (from == CT_ULONG_LONG)
        emit_op(OP_F32_CONVERT_I64_U);
    else if (from == CT_DOUBLE)
        emit_op(OP_F32_DEMOTE_F64);
    /* CT_FLOAT -> no-op, already f32 */
}
/* Coerce top of wasm stack to f64 */
static inline void emit_promote_f64(CType from) {
    if (from == CT_FLOAT) emit_op(OP_F64_PROMOTE_F32);
    else if (from == CT_INT || from == CT_CHAR) emit_op(OP_F64_CONVERT_I32_S);
    else if (from == CT_UINT) emit_op(OP_F64_CONVERT_I32_U);
    else if (from == CT_LONG_LONG) emit_op(OP_F64_CONVERT_I64_S);
    else if (from == CT_ULONG_LONG) emit_op(OP_F64_CONVERT_I64_U);
}
/* General coerce between any two types */
static inline void emit_coerce(CType from, CType to) {
    if (from == to) return;
    if (to == CT_UINT) {
        /* float→uint: unsigned trunc */
        if (from == CT_FLOAT) emit_op(OP_I32_TRUNC_F32_U);
        else if (from == CT_DOUBLE) emit_op(OP_I32_TRUNC_F64_U);
        else if (from == CT_LONG_LONG || from == CT_ULONG_LONG) emit_op(OP_I32_WRAP_I64);
        /* CT_INT/CT_CHAR/CT_CONST_STR → already i32 */
    } else if (to == CT_ULONG_LONG) {
        if (from == CT_UINT) emit_op(OP_I64_EXTEND_I32_U);
        else if (from == CT_INT || from == CT_CHAR || from == CT_CONST_STR)
            emit_op(OP_I64_EXTEND_I32_S);
        else if (from == CT_FLOAT) emit_op(OP_I64_TRUNC_F32_U);
        else if (from == CT_DOUBLE) emit_op(OP_I64_TRUNC_F64_U);
        /* CT_LONG_LONG → already i64, same bit pattern */
    } else if (to == CT_INT || to == CT_CHAR) {
        emit_coerce_i32(from);
    } else if (to == CT_LONG_LONG) {
        emit_coerce_i64(from);
    } else if (to == CT_FLOAT) {
        emit_coerce_f32(from);
    } else if (to == CT_DOUBLE) {
        emit_promote_f64(from);
    }
}

/* Type operations (type_ops.c) */
TypeInfo type_base(CType ct);
TypeInfo type_pointer(TypeInfo base);
TypeInfo type_array(TypeInfo base, int size);
TypeInfo type_decay(TypeInfo t);
int type_is_pointer(TypeInfo t);
int type_is_array(TypeInfo t);
int type_is_scalar(TypeInfo t);
CType type_base_ctype(TypeInfo t);
int type_element_size(TypeInfo t);
int type_sizeof(TypeInfo t);
TypeInfo type_deref(TypeInfo t);
int type_compatible(TypeInfo a, TypeInfo b);

/* ================================================================
 *  Cross-file function declarations
 * ================================================================ */

/* lexer.c */
void lex_init(void);
int next_token(void);
int peek_token(void);
void expect(int t);
int accept(int t);
const char *tok_name(int t);
void synchronize(int stop_at_semi, int stop_at_brace, int stop_at_rparen);

typedef struct {
    char *saved_source;
    int saved_src_pos, saved_src_len, saved_line_num;
    int saved_tok, saved_tok_ival;
    int64_t saved_tok_i64;
    int saved_tok_int_is_64;
    int saved_tok_int_unsigned;
    float saved_tok_fval;
    double saved_tok_dval;
    char saved_tok_sval[1024];
    int saved_tok_slen;
    int saved_peek_valid, saved_peek_tok, saved_peek_ival;
    int64_t saved_peek_i64;
    int saved_peek_int_is_64;
    int saved_peek_int_unsigned;
    float saved_peek_fval;
    double saved_peek_dval;
    char saved_peek_sval[1024];
    int saved_peek_slen;
    int saved_macro_depth;
} LexerSave;

void lexer_save(LexerSave *s);
void lexer_restore(LexerSave *s);

/* preproc.c */
void preproc_init(void);
int preproc_line(void);  /* handle # directives, returns 1 if consumed */
int preproc_skipping(void);  /* returns 1 if inside #if 0 / #ifdef skip block */
void register_api_imports(void);

/* type.c */
CType parse_type_spec(void);
int is_type_keyword(int t);

/* expr.c */
CType expr(void);
CType assignment_expr(void);

/* stmt.c */
void parse_top_level(void);
void parse_block(void);
void parse_stmt(void);

/* assemble.c */
Buf assemble_to_buf(void);
void assemble(const char *outpath);

/* main.c */
void cw_compile(void);
Buf c2wasm_compile_buffer(const char *src, int len, const char *filename);
void c2wasm_reset(void);

#endif /* C2WASM_H */
