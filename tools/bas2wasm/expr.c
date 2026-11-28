/*
 * expr.c — expression parser, builtin function tables, constant folding
 */
#include "bas2wasm.h"

/* ================================================================
 *  String temporaries / variable typing helpers
 * ================================================================ */

int bw_str_hold(void) {
    if (vsp > 0 && vstack[vsp-1] == T_STR && vown[vsp-1] == STR_TEMP) {
        int l = alloc_local();
        buf_byte(CODE, OP_LOCAL_TEE); buf_uleb(CODE, l);
        return l;
    }
    return -1;
}

void bw_str_release(int local) {
    if (local < 0) return;
    emit_local_get(local);
    emit_call(IMP_STR_FREE);
}

void bw_str_own(void) {
    if (vsp > 0 && vstack[vsp-1] == T_STR && vown[vsp-1] == STR_VAR) {
        emit_call(IMP_STR_COPY);
        vown[vsp-1] = STR_TEMP;
    }
}

/* Array element access is bounds-checked at run time: a bad index stops the
 * script with a trap (reported with the BASIC line number) instead of reading
 * or writing other memory. Unchecked, `A(4)` on `DIM A(3)` wrote the next
 * allocation's header, index 0 under the default OPTION BASE 1 overwrote the
 * array's own stored bound, and an array that was never DIMmed (or was ERASEd)
 * was accessed through address 0. */
void bw_emit_array_null_check(int var) {
    emit_global_get(vars[var].global_idx);
    emit_op(OP_I32_EQZ);
    emit_if_void();
    emit_op(OP_UNREACHABLE);
    emit_end();
}

/* idx_local holds the BASIC index, extent_local the dimension's extent
 * (upper - option_base + 1). One unsigned compare covers both ends:
 * (idx - base) as unsigned >= extent  <=>  idx < base or idx > upper. */
void bw_emit_index_check(int idx_local, int extent_local) {
    emit_local_get(idx_local);
    emit_i32_const(option_base);
    emit_op(OP_I32_SUB);
    emit_local_get(extent_local);
    emit_op(OP_I32_GE_U);
    emit_if_void();
    emit_op(OP_UNREACHABLE);
    emit_end();
}

/* Reading an untyped variable fixes it as an integer. Without this, code that
 * had already used the variable as i32 was left behind when a LATER assignment
 * made the global f32 or i64 -- the module was invalid and the compiler still
 * exited 0 (`X# = 1.5 : SUM = SUM + X#`). Now such a variable stays an integer
 * and the later assignment is coerced, as for any typed variable. */
void bw_var_lock_type(int var) {
    if (!vars[var].type_set) {
        vars[var].type = T_I32;
        vars[var].type_set = 1;
    }
}

/* ================================================================
 *  Binary op helpers
 * ================================================================ */

static void emit_binop(int i32_op, int f32_op, int i64_op) {
    VType b = vpop(), a = vpop();
    /* Constant folding: check if both operands are adjacent constants in the
     * current function's code buffer. */
    if (fold_a.valid && fold_b.valid &&
        fold_a.buf == CODE && fold_b.buf == CODE &&
        fold_a.buf_end == fold_b.buf_start &&
        fold_b.buf_end == CODE->len) {
        int folded = 0;
        FoldSlot saved_p = fold_p;  /* preserve predecessor across the fold */
        if (fold_a.valid == 1 && fold_b.valid == 1) {
            /* Both i32 */
            int32_t va = fold_a.ival, vb = fold_b.ival, r = 0;
            /* Fold with wrapping (unsigned) arithmetic, like the WASM ops. A
             * division is NOT folded when it would trap at run time: by zero,
             * or INT_MIN / -1 -- evaluating that one here raised SIGFPE and
             * killed the host tool and the simulator. */
            if (i32_op == OP_I32_ADD) { r = (int32_t)((uint32_t)va + (uint32_t)vb); folded = 1; }
            else if (i32_op == OP_I32_SUB) { r = (int32_t)((uint32_t)va - (uint32_t)vb); folded = 1; }
            else if (i32_op == OP_I32_MUL) { r = (int32_t)((uint32_t)va * (uint32_t)vb); folded = 1; }
            else if (i32_op == OP_I32_DIV_S && vb != 0 && !(va == INT32_MIN && vb == -1)) { r = va / vb; folded = 1; }
            if (folded) {
                CODE->len = fold_a.buf_start;
                fold_p.valid = fold_a.valid = fold_b.valid = 0;
                emit_i32_const(r);
                fold_a = saved_p; fold_p.valid = 0;
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
                fold_p.valid = fold_a.valid = fold_b.valid = 0;
                emit_f32_const(r);
                fold_a = saved_p; fold_p.valid = 0;
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
                fold_p.valid = fold_a.valid = fold_b.valid = 0;
                emit_f32_const(r);
                fold_a = saved_p; fold_p.valid = 0;
                vpush(T_F32);
                return;
            }
        }
    }
    /* Normal (non-folded) path */
    if (a == T_F32 || b == T_F32) {
        if (a != T_F32 && b == T_F32) {
            /* a(i32) is under b(f32) — save b, convert a, reload b */
            int scratch = alloc_local_f32();
            emit_local_set(scratch);
            if (a == T_I64) emit_op(OP_F32_CONVERT_I64_S);
            else emit_op(OP_F32_CONVERT_I32_S);
            emit_local_get(scratch);
        } else if (a == T_F32 && b != T_F32) {
            if (b == T_I64) emit_op(OP_F32_CONVERT_I64_S);
            else emit_op(OP_F32_CONVERT_I32_S);
        }
        emit_op(f32_op);
        vpush(T_F32);
    } else if (a == T_I64 || b == T_I64) {
        if (a == T_I32 && b == T_I64) {
            int scratch = alloc_local_i64();
            emit_local_set(scratch);
            emit_op(OP_I64_EXTEND_I32_S);
            emit_local_get(scratch);
        } else if (a == T_I64 && b == T_I32) {
            emit_op(OP_I64_EXTEND_I32_S);
        }
        emit_op(i64_op);
        vpush(T_I64);
    } else {
        emit_op(i32_op);
        vpush(T_I32);
    }
}

/* Comparison: always produces i32 (-1 or 0) */
static void emit_compare(int i32_op, int f32_op, int i64_op) {
    VType b = vpop(), a = vpop();
    if (a == T_F32 || b == T_F32) {
        if (a != T_F32 && b == T_F32) {
            int scratch = alloc_local_f32();
            emit_local_set(scratch);
            if (a == T_I64) emit_op(OP_F32_CONVERT_I64_S);
            else emit_op(OP_F32_CONVERT_I32_S);
            emit_local_get(scratch);
        } else if (a == T_F32 && b != T_F32) {
            if (b == T_I64) emit_op(OP_F32_CONVERT_I64_S);
            else emit_op(OP_F32_CONVERT_I32_S);
        }
        emit_op(f32_op);
    } else if (a == T_I64 || b == T_I64) {
        if (a == T_I32 && b == T_I64) {
            int scratch = alloc_local_i64();
            emit_local_set(scratch);
            emit_op(OP_I64_EXTEND_I32_S);
            emit_local_get(scratch);
        } else if (a == T_I64 && b == T_I32) {
            emit_op(OP_I64_EXTEND_I32_S);
        }
        emit_op(i64_op);
    } else {
        emit_op(i32_op);
    }
    /* Convert WASM 0/1 to BASIC -1/0 */
    emit_i32_const(-1);
    emit_op(OP_I32_MUL);
    vpush(T_I32);
}

/* Integer binary op: coerce both operands to i32 */
static void emit_int_binop(int i32_op) {
    VType b = vpop(), a = vpop();
    /* Constant folding: both i32, same code buffer, adjacent. */
    if (fold_a.valid == 1 && fold_b.valid == 1 &&
        fold_a.buf == CODE && fold_b.buf == CODE &&
        fold_a.buf_end == fold_b.buf_start &&
        fold_b.buf_end == CODE->len) {
        int32_t va = fold_a.ival, vb = fold_b.ival;
        int folded = 0; int32_t r = 0;
        int would_trap = (vb == 0) || (va == INT32_MIN && vb == -1);   /* see emit_binop */
        if (i32_op == OP_I32_DIV_S && !would_trap) { r = va / vb; folded = 1; }
        else if (i32_op == OP_I32_REM_S && !would_trap) { r = va % vb; folded = 1; }
        if (folded) {
            FoldSlot saved_p = fold_p;
            CODE->len = fold_a.buf_start;
            fold_p.valid = fold_a.valid = fold_b.valid = 0;
            emit_i32_const(r);
            fold_a = saved_p; fold_p.valid = 0;
            vpush(T_I32);
            return;
        }
    }
    if (a == T_I64 || b == T_I64) {
        int i64_op = OP_I64_DIV_S;
        if (i32_op == OP_I32_REM_S) i64_op = OP_I64_REM_S;
        if (a == T_I32 && b == T_I64) {
            int sc = alloc_local_i64();
            emit_local_set(sc);
            emit_op(OP_I64_EXTEND_I32_S);
            emit_local_get(sc);
        } else if (a == T_I64 && b == T_I32) {
            emit_op(OP_I64_EXTEND_I32_S);
        } else if (a == T_F32 && b == T_I64) {
            int sc = alloc_local_i64();
            emit_local_set(sc);
            emit_op(OP_I64_TRUNC_F32_S);
            emit_local_get(sc);
        } else if (a == T_I64 && b == T_F32) {
            emit_op(OP_I64_TRUNC_F32_S);
        } else if (a == T_F32 && b == T_F32) {
            int sc = alloc_local_f32();
            emit_local_set(sc);
            emit_op(OP_I64_TRUNC_F32_S);
            emit_local_get(sc);
            emit_op(OP_I64_TRUNC_F32_S);
        }
        emit_op(i64_op);
        vpush(T_I64);
    } else if (a == T_I32 && b == T_I32) {
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
    if (!(a == T_I64 || b == T_I64)) vpush(T_I32);
}

/* ================================================================
 *  Built-in Function Tables
 * ================================================================ */

/* Simple call-through builtins: name, nargs, import, result conversion:
 *   0 = as returned by the host
 *   1 = truncate the host's f32 to i32
 *   2 = predicate: the host returns 1/0; convert to BASIC truth (-1 / 0).
 *       NOT is bitwise, so with a 1 for "true", `IF NOT HASGPS()` was always
 *       taken (NOT 1 = -2). EOF() already did this conversion.
 *   3 = inverted predicate: -1 when the host returns 0, else 0 (PIN_LOW). */
typedef struct { const char *name; int nargs; int imp; int trunc; } SimpleBI;

/* Code position just after the most recent PIN_READ() call, or -1. PIN_READ
 * returns the pin LEVEL (1/0), not a truth value, so `NOT PIN_READ(n)` is -2
 * or -1 -- always true. NOT compares this with the end of its operand to
 * reject exactly that form and point at PIN_LOW()/PIN_HIGH(). */
static int pin_read_call_end = -1;
static const SimpleBI simple_bi[] = {
    {"EPOCHMS&",    0, IMP_GET_EPOCH_MS, 0},
    {"GETPARAM",    1, IMP_GET_PARAM,    0},
    {"HASGPS",      0, IMP_GPS_VALID,    2},
    {"HASORIGIN",   0, IMP_HAS_ORIGIN,   2},
    {"SECOND",      0, IMP_GET_SECOND,   0},
    {"MINUTE",      0, IMP_GET_MINUTE,   0},
    {"HOUR",        0, IMP_GET_HOUR,     0},
    {"DAY",         0, IMP_GET_DAY,      0},
    {"MONTH",       0, IMP_GET_MONTH,    0},
    {"YEAR",        0, IMP_GET_YEAR,     0},
    {"DAYOFWEEK",   0, IMP_GET_DAY_OF_WEEK,  0},
    {"DAYOFYEAR",   0, IMP_GET_DAY_OF_YEAR,  0},
    {"ISLEAPYEAR",  0, IMP_GET_IS_LEAP_YEAR, 2},
    {"HASDATE",     0, IMP_TIME_VALID,   2},
    {"HASTIME",     0, IMP_TIME_VALID,   2},
    {"HASGYRO",     0, IMP_IMU_VALID,    2},
    {"HASACC",      0, IMP_IMU_VALID,    2},
    {"HASMAG",      0, IMP_IMU_VALID,    2},
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
    {"PIN_HIGH",    1, IMP_PIN_READ,     2},
    {"PIN_LOW",     1, IMP_PIN_READ,     3},
    {"ANALOG_READ", 1, IMP_ANALOG_READ,  0},
    {"GPSPRESENT",  0, IMP_GPS_PRESENT,  2},
    {"IMUPRESENT",  0, IMP_IMU_PRESENT,  2},
    {"UPTIME",      0, IMP_MILLIS,       0},
    {"UPTIME&",     0, IMP_MILLIS64,     0},
    {"CUEELAPSED&", 0, IMP_CUE_ELAPSED,  0},
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

/* Float math builtins: name, nargs, import (-1 = use opcode), opcode */
typedef struct {
    const char *name;
    int nargs;
    int imp;
    int opcode;
} FloatMathBI;

static const FloatMathBI float_math_bi[] = {
    {"SIN",   1, IMP_SINF,   0},
    {"COS",   1, IMP_COSF,   0},
    {"TAN",   1, IMP_TANF,   0},
    {"EXP",   1, IMP_EXPF,   0},
    {"LOG",   1, IMP_LOGF,   0},
    {"LOG2",  1, IMP_LOG2F,  0},
    {"ATAN2", 2, IMP_ATAN2F, 0},
    {"POW",   2, IMP_POWF,   0},
    {"FMOD",  2, IMP_FMODF,  0},
    {"SQRT",  1, -1,         OP_F32_SQRT},
    {"FLOOR", 1, -1,         OP_F32_FLOOR},
    {"CEIL",  1, -1,         OP_F32_CEIL},
    {NULL, 0, 0, 0}
};

/* String builtins: name, nargs, import, result type, arg types */
#define SA 0  /* any (no coercion) */
#define SI 1  /* coerce to i32 */
typedef struct {
    const char *name;
    int nargs;
    int imp;
    VType result;
    uint8_t arg_types[3];
} StringBI;

static const StringBI string_bi[] = {
    {"MID$",     3, IMP_STR_MID,    T_STR, {SA, SI, SI}},
    {"LEFT$",    2, IMP_STR_LEFT,   T_STR, {SA, SI, 0}},
    {"RIGHT$",   2, IMP_STR_RIGHT,  T_STR, {SA, SI, 0}},
    {"CHR$",     1, IMP_STR_CHR,    T_STR, {SI, 0, 0}},
    {"UPPER$",   1, IMP_STR_UPPER,  T_STR, {SA, 0, 0}},
    {"UCASE$",   1, IMP_STR_UPPER,  T_STR, {SA, 0, 0}},
    {"LOWER$",   1, IMP_STR_LOWER,  T_STR, {SA, 0, 0}},
    {"LCASE$",   1, IMP_STR_LOWER,  T_STR, {SA, 0, 0}},
    {"TRIM$",    1, IMP_STR_TRIM,   T_STR, {SA, 0, 0}},
    {"LTRIM$",   1, IMP_STR_LTRIM,  T_STR, {SA, 0, 0}},
    {"RTRIM$",   1, IMP_STR_RTRIM,  T_STR, {SA, 0, 0}},
    {"SPACE$",   1, IMP_STR_SPACE,  T_STR, {SI, 0, 0}},
    {"HEX$",     1, IMP_STR_HEX,   T_STR, {SI, 0, 0}},
    {"OCT$",     1, IMP_STR_OCT,   T_STR, {SI, 0, 0}},
    {"STRING$",  2, IMP_STR_REPEAT, T_STR, {SI, SI, 0}},
    {"LEN",      1, IMP_STR_LEN,   T_I32, {SA, 0, 0}},
    {"ASC",      1, IMP_STR_ASC,   T_I32, {SA, 0, 0}},
    {"VAL",      1, IMP_STR_TO_INT,   T_I32, {SA, 0, 0}},
    {"VAL&",     1, IMP_STR_TO_I64,   T_I64, {SA, 0, 0}},
    {"VAL#",     1, IMP_STR_TO_FLOAT, T_F32, {SA, 0, 0}},
    {NULL, 0, 0, 0, {0, 0, 0}}
};
#undef SA
#undef SI

/* ================================================================
 *  compile_builtin_expr
 * ================================================================ */

static int compile_builtin_expr_inner(const char *name);

/* A builtin call leaves exactly ONE value. Several of the hand-written ones
 * (RANDOM, LIMIT, LIMIT256, SCALE, SIN256, DIST, ANGLE, INSTR) pushed their
 * result without popping their argument types, so the next operator read an
 * argument's type as its left operand: `1.5 * RANDOM(0,1)` produced an invalid
 * module and `"n=" + STR$(LIMIT(5,0,3))` was rejected. Enforced here for all of
 * them rather than at each site. */
int compile_builtin_expr(const char *name) {
    int base = vsp;
    int r = compile_builtin_expr_inner(name);
    if (r && vsp > base + 1) {
        VType rt = vstack[vsp-1];
        uint8_t ro = vown[vsp-1];
        vsp = base;
        vpush(rt);
        vown[vsp-1] = ro;
    }
    return r;
}

static int compile_builtin_expr_inner(const char *name) {
    /* Check simple builtins first */
    for (const SimpleBI *b = simple_bi; b->name; b++) {
        if (strcmp(name, b->name) != 0) continue;
        int base_vsp = vsp;
        for (int i = 0; i < b->nargs; i++) {
            if (i > 0) need(TOK_COMMA);
            expr(); coerce_i32();
        }
        need(TOK_RP);
        emit_call(b->imp);
        vsp = base_vsp;
        if (b->imp == IMP_PIN_READ && b->trunc == 0)
            pin_read_call_end = CODE->len;
        if (b->trunc == 2 || b->trunc == 3) {
            emit_op(OP_I32_EQZ);                        /* 1 when the host said 0 */
            if (b->trunc == 2) emit_op(OP_I32_EQZ);     /* 1 when it said non-zero */
            emit_i32_const(-1); emit_op(OP_I32_MUL);    /* 0/-1 */
            vpush(T_I32);
        } else if (b->trunc) {
            emit_op(OP_I32_TRUNC_F32_S);
            vpush(T_I32);
        } else {
            const ImportDef *id = &imp_defs[b->imp];
            if (id->nr > 0 && id->r[0] == WASM_F32) vpush(T_F32);
            else if (id->nr > 0 && id->r[0] == WASM_I64) vpush(T_I64);
            else vpush(T_I32);
        }
        return 1;
    }

    /* Float math builtins */
    for (const FloatMathBI *b = float_math_bi; b->name; b++) {
        if (strcmp(name, b->name) != 0) continue;
        int base_vsp = vsp;
        for (int i = 0; i < b->nargs; i++) {
            if (i > 0) need(TOK_COMMA);
            expr(); coerce_f32();
        }
        need(TOK_RP);
        if (b->imp >= 0)
            emit_call(b->imp);
        else
            emit_op(b->opcode);
        vsp = base_vsp;
        vpush(T_F32);
        return 1;
    }

    /* String builtins */
    for (const StringBI *b = string_bi; b->name; b++) {
        if (strcmp(name, b->name) != 0) continue;
        int base_vsp = vsp;
        int holds[3] = { -1, -1, -1 };
        for (int i = 0; i < b->nargs; i++) {
            if (i > 0) need(TOK_COMMA);
            expr();
            if (b->arg_types[i] == 1) coerce_i32();
            else {
                /* A string is required. A number here was passed straight to
                 * the host as a pointer (`LEN(123)` read memory at address
                 * 123; `LEN(1.5)` produced an invalid module). */
                if (vsp > 0 && vstack[vsp-1] != T_STR) {
                    char msg[48];
                    snprintf(msg, sizeof(msg), "%s expects a string argument", b->name);
                    error_at(msg);
                }
                holds[i] = bw_str_hold();   /* free it after the call if temporary */
            }
        }
        need(TOK_RP);
        emit_call(b->imp);
        for (int i = 0; i < 3; i++) bw_str_release(holds[i]);
        vsp = base_vsp;
        if (b->result == T_STR) vpush_temp_str();
        else vpush(b->result);
        return 1;
    }

    /* Custom builtins with special logic */

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
        vpop(); vpop(); vpop();   /* consume R, G, B arg types */
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "WAIT") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        emit_call(IMP_DELAY_MS);
        vpop();                   /* consume duration arg type */
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
        vpop();                   /* consume gamma arg type */
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "TIMESTAMP") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        int scratch = alloc_local();
        emit_local_set(scratch);
        emit_call(IMP_MILLIS);
        emit_local_get(scratch);
        emit_op(OP_I32_DIV_S);
        vpop(); vpush(T_I32);   /* consume divisor type; push result */
        return 1;
    }
    if (strcmp(name, "TIMESTAMP&") == 0) {
        expr(); coerce_i64(); need(TOK_RP);
        int scratch = alloc_local_i64();
        emit_local_set(scratch);
        emit_call(IMP_MILLIS64);
        emit_local_get(scratch);
        emit_op(OP_I64_DIV_S);
        vpop(); vpush(T_I64);   /* consume divisor type; push result */
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
        if (vsp > 0 && vstack[vsp-1] == T_STR) { error_at("ABS expects a number"); return 1; }
        VType t = vpop();
        if (t == T_F32) {
            emit_op(OP_F32_ABS);
            vpush(T_F32);
        } else if (t == T_I64) {
            int scratch = alloc_local_i64();
            emit_local_set(scratch);
            emit_i64_const(0);
            emit_local_get(scratch);
            emit_op(OP_I64_SUB);
            emit_local_get(scratch);
            emit_local_get(scratch);
            emit_i64_const(0);
            emit_op(OP_I64_LT_S);
            emit_op(OP_SELECT);
            vpush(T_I64);
        } else {
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
        int hi = alloc_local(), lo = alloc_local(), val = alloc_local();
        emit_local_set(hi);
        emit_local_set(lo);
        emit_local_set(val);
        /* max(val, lo): select(a, b, c) yields a when c != 0. The operands
         * were the other way round, which gave min(val, lo) -- LIMIT returned
         * the lower bound (or less) for every in-range value. */
        emit_local_get(lo);
        emit_local_get(val);
        emit_local_get(val);
        emit_local_get(lo);
        emit_op(OP_I32_LT_S);
        emit_op(OP_SELECT);
        int tmp = alloc_local();
        emit_local_set(tmp);
        emit_local_get(hi);
        emit_local_get(tmp);
        emit_local_get(tmp);
        emit_local_get(hi);
        emit_op(OP_I32_GT_S);
        emit_op(OP_SELECT);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "LIMIT256") == 0) {
        expr(); coerce_i32(); need(TOK_RP);
        int val = alloc_local();
        emit_local_set(val);
        emit_i32_const(0);          /* max(val, 0) -- see LIMIT */
        emit_local_get(val);
        emit_local_get(val);
        emit_i32_const(0);
        emit_op(OP_I32_LT_S);
        emit_op(OP_SELECT);
        int tmp = alloc_local();
        emit_local_set(tmp);
        emit_i32_const(255);
        emit_local_get(tmp);
        emit_local_get(tmp);
        emit_i32_const(255);
        emit_op(OP_I32_GT_S);
        emit_op(OP_SELECT);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "SCALE") == 0) {
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
        /* Equal input bounds: nothing to interpolate, the result is rmin
         * (as the interpreter documents). It used to divide by zero and trap. */
        int res = alloc_local();
        emit_local_get(vmax); emit_local_get(vmin); emit_op(OP_I32_EQ);
        emit_if_void();
            emit_local_get(rmin); emit_local_set(res);
        emit_else();
            emit_local_get(val); emit_local_get(vmin); emit_op(OP_I32_SUB);
            emit_local_get(rmax); emit_local_get(rmin); emit_op(OP_I32_SUB);
            emit_op(OP_I32_MUL);
            emit_local_get(vmax); emit_local_get(vmin); emit_op(OP_I32_SUB);
            emit_op(OP_I32_DIV_S);
            emit_local_get(rmin); emit_op(OP_I32_ADD);
            emit_local_set(res);
        emit_end();
        emit_local_get(res);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "SIN256") == 0) {
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
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        int y2 = alloc_local(), x2 = alloc_local();
        int y1 = alloc_local(), x1 = alloc_local();
        emit_local_set(y2); emit_local_set(x2);
        emit_local_set(y1); emit_local_set(x1);
        emit_local_get(x2); emit_local_get(x1); emit_op(OP_I32_SUB);
        emit_op(OP_F32_CONVERT_I32_S);
        int fdx = alloc_local_f32();
        emit_local_set(fdx);
        emit_local_get(y2); emit_local_get(y1); emit_op(OP_I32_SUB);
        emit_op(OP_F32_CONVERT_I32_S);
        int fdy = alloc_local_f32();
        emit_local_set(fdy);
        emit_local_get(fdx); emit_local_get(fdx); emit_op(OP_F32_MUL);
        emit_local_get(fdy); emit_local_get(fdy); emit_op(OP_F32_MUL);
        emit_op(OP_F32_ADD);
        emit_op(OP_F32_SQRT);
        emit_op(OP_I32_TRUNC_F32_S);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "ANGLE") == 0) {
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
        emit_f32_const(57.29578f); emit_op(OP_F32_MUL);
        emit_op(OP_I32_TRUNC_F32_S);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "WAITFOR") == 0) {
        expr(); coerce_i32(); need(TOK_COMMA);
        int ev = alloc_local(); emit_local_set(ev); vpop();
        expr(); coerce_i32(); need(TOK_COMMA);
        int src = alloc_local(); emit_local_set(src); vpop();
        expr(); coerce_i32(); need(TOK_COMMA);
        int cond = alloc_local(); emit_local_set(cond); vpop();
        expr(); coerce_i32(); need(TOK_COMMA);
        int trig = alloc_local(); emit_local_set(trig); vpop();
        expr(); coerce_i32(); need(TOK_RP);
        int tout = alloc_local(); emit_local_set(tout); vpop();

        /* All if-blocks below MUST have stack signature [] -> [] (void).
         * Each branch writes the result/scale to a local; we load `result`
         * at the very end. This avoids needing typed if-blocks. */
        int result = alloc_local();
        int scale = alloc_local();
        emit_i32_const(0); emit_local_set(result);

        emit_local_get(ev);
        emit_i32_const(4);
        emit_op(OP_I32_EQ);
        emit_if_void();
            emit_i32_const(1); emit_local_set(scale);   /* default scale */
            emit_local_get(cond);
            emit_i32_const(6);
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_i32_const(3600000); emit_local_set(scale);
            emit_else();
                emit_local_get(cond);
                emit_i32_const(7);
                emit_op(OP_I32_EQ);
                emit_if_void();
                    emit_i32_const(60000); emit_local_set(scale);
                emit_else();
                    emit_local_get(cond);
                    emit_i32_const(8);
                    emit_op(OP_I32_EQ);
                    emit_if_void();
                        emit_i32_const(1000); emit_local_set(scale);
                    emit_end();
                emit_end();
            emit_end();
            emit_local_get(trig);
            emit_local_get(scale);
            emit_op(OP_I32_MUL);
            emit_call(IMP_DELAY_MS);   /* takes 1 i32, returns nothing */
            emit_i32_const(1); emit_local_set(result);
        emit_else();
            emit_local_get(ev);
            emit_i32_const(5);
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_local_get(tout);
                emit_call(IMP_WAIT_PPS);   /* takes 1 i32, returns 1 i32 */
                emit_local_set(result);
            emit_else();
                emit_local_get(ev);
                emit_i32_const(6);
                emit_op(OP_I32_EQ);
                emit_if_void();
                    emit_local_get(src);
                    emit_local_get(cond);
                    emit_local_get(trig);
                    emit_local_get(tout);
                    emit_call(IMP_WAIT_PARAM);   /* 4 args, returns 1 i32 */
                    emit_local_set(result);
                emit_end();
                /* (else: result keeps its default 0) */
            emit_end();
        emit_end();

        emit_local_get(result);
        vpush(T_I32);
        return 1;
    }
    /* INT(x): to integer, truncating toward zero. Convert by the argument's
     * type: an integer used to be sent through f32 and back (INT(16777217) gave
     * 16777216, INT(2147483647) trapped). A float saturates at the i32 range
     * (NaN -> 0) instead of trapping. */
    if (strcmp(name, "INT") == 0) {
        expr(); need(TOK_RP);
        if (vsp > 0 && vstack[vsp-1] == T_STR) { error_at("INT expects a number"); return 1; }
        VType t = vpop();
        if (t == T_F32) { buf_byte(CODE, 0xFC); buf_byte(CODE, 0x00); }    /* i32.trunc_sat_f32_s */
        else if (t == T_I64) emit_op(OP_I32_WRAP_I64);
        vpush(T_I32);
        return 1;
    }
    /* FLOAT(x): to float. A float argument is left alone -- it used to be
     * truncated to an integer first (FLOAT(2.7) gave 2). */
    if (strcmp(name, "FLOAT") == 0) {
        expr(); need(TOK_RP);
        if (vsp > 0 && vstack[vsp-1] == T_STR) { error_at("FLOAT expects a number"); return 1; }
        VType t = vpop();
        if (t == T_I32) emit_op(OP_F32_CONVERT_I32_S);
        else if (t == T_I64) emit_op(OP_F32_CONVERT_I64_S);
        vpush(T_F32);
        return 1;
    }
    /* SETLEDRGB(aR, aG, aB)
     * A 1-D array is [ndims][upper bound][element option_base]...: the data
     * starts at +8. This used to read from +4 + i*4, so pixel 0 got the
     * array's UPPER BOUND as its colour and every LED was one element late. It
     * also ran to the LED count regardless of the arrays' size; now it stops
     * at the shortest array. */
    if (strcmp(name, "SETLEDRGB") == 0) {
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        int ab = alloc_local(), ag = alloc_local(), ar = alloc_local();
        emit_local_set(ab); emit_local_set(ag); emit_local_set(ar);
        vpop(); vpop(); vpop();   /* consume aR, aG, aB arg types */
        int i = alloc_local(), n = alloc_local(), ext = alloc_local();
        emit_i32_const(1); emit_call(IMP_LED_COUNT); emit_local_set(n);
        int arrs[3] = { ar, ag, ab };
        for (int k = 0; k < 3; k++) {
            /* ext = upper bound - option_base + 1;  n = min(n, ext) */
            emit_local_get(arrs[k]); emit_i32_load(4);
            emit_i32_const(option_base - 1); emit_op(OP_I32_SUB);
            emit_local_set(ext);
            emit_local_get(ext); emit_local_get(n);
            emit_local_get(ext); emit_local_get(n); emit_op(OP_I32_LT_S);
            emit_op(OP_SELECT);
            emit_local_set(n);
        }
        emit_i32_const(0); emit_local_set(i);
        emit_block(); emit_loop();
            emit_local_get(i); emit_local_get(n);
            emit_op(OP_I32_GE_S);
            emit_br_if(1);
            emit_i32_const(1);
            emit_local_get(i);
            for (int k = 0; k < 3; k++) {
                emit_local_get(arrs[k]);
                emit_local_get(i); emit_i32_const(4); emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
                emit_i32_load(8);
            }
            emit_call(IMP_LED_SET_PIXEL);
            emit_local_get(i); emit_i32_const(1); emit_op(OP_I32_ADD); emit_local_set(i);
            emit_br(0);
        emit_end(); emit_end();
        emit_call(IMP_LED_SHOW);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    /* SETARRAY(arr, start, end, value) -- set elements start..end (BASIC
     * indices) of a 1-D integer array. Same header mistake as SETLEDRGB: it
     * wrote to arr + i*4, missing the last element and overwriting the stored
     * upper bound (index 0 overwrote ndims). The range is now clamped to the
     * array's bounds. */
    if (strcmp(name, "SETARRAY") == 0) {
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        int val = alloc_local(), end = alloc_local();
        int start = alloc_local(), arr = alloc_local();
        emit_local_set(val); emit_local_set(end);
        emit_local_set(start); emit_local_set(arr);
        vpop(); vpop(); vpop(); vpop();   /* consume arr, start, end, val arg types */
        int i = alloc_local(), ub = alloc_local();
        /* start = max(start, option_base) */
        emit_i32_const(option_base); emit_local_get(start);
        emit_local_get(start); emit_i32_const(option_base); emit_op(OP_I32_LT_S);
        emit_op(OP_SELECT);
        emit_local_set(i);
        /* end = min(end, upper bound) */
        emit_local_get(arr); emit_i32_load(4); emit_local_set(ub);
        emit_local_get(ub); emit_local_get(end);
        emit_local_get(end); emit_local_get(ub); emit_op(OP_I32_GT_S);
        emit_op(OP_SELECT);
        emit_local_set(end);
        emit_block(); emit_loop();
            emit_local_get(i); emit_local_get(end); emit_op(OP_I32_GT_S);
            emit_br_if(1);
            emit_local_get(arr);
            emit_local_get(i); emit_i32_const(option_base); emit_op(OP_I32_SUB);
            emit_i32_const(4); emit_op(OP_I32_MUL); emit_op(OP_I32_ADD);
            emit_local_get(val);
            emit_i32_store(8);
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
        int nargs = 2;
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
            vpop();               /* WASM-stack was dropped; mirror on vstack */
        }
        need(TOK_RP);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    /* STR$ — special: dispatches based on type */
    if (strcmp(name, "STR$") == 0) {
        expr(); need(TOK_RP);
        if (vsp > 0 && vstack[vsp-1] == T_STR) { error_at("STR$ expects a number"); return 1; }
        VType t = vpop();
        if (t == T_F32) {
            emit_call(IMP_STR_FROM_FLOAT);
        } else if (t == T_I64) {
            emit_call(IMP_STR_FROM_I64);
        } else {
            emit_call(IMP_STR_FROM_INT);
        }
        vpush_temp_str();
        return 1;
    }
    /* INSTR — optional 3rd arg */
    if (strcmp(name, "INSTR") == 0) {
        expr();
        if (vsp > 0 && vstack[vsp-1] != T_STR) error_at("INSTR expects string arguments");
        int h1 = bw_str_hold(); need(TOK_COMMA);
        expr();
        if (vsp > 0 && vstack[vsp-1] != T_STR) error_at("INSTR expects string arguments");
        int h2 = bw_str_hold();
        if (want(TOK_COMMA)) {
            expr(); coerce_i32();
        } else {
            emit_i32_const(1);
        }
        need(TOK_RP);
        emit_call(IMP_STR_INSTR);
        bw_str_release(h1); bw_str_release(h2);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "SGN") == 0) {
        expr(); need(TOK_RP);
        if (vsp > 0 && vstack[vsp-1] == T_STR) { error_at("SGN expects a number"); return 1; }
        VType t = vpop();
        if (t == T_F32) {
            int scratch = alloc_local_f32();
            emit_local_set(scratch);
            emit_local_get(scratch);
            emit_f32_const(0.0f);
            emit_op(OP_F32_GT);
            emit_local_get(scratch);
            emit_f32_const(0.0f);
            emit_op(OP_F32_LT);
            emit_op(OP_I32_SUB);
        } else if (t == T_I64) {
            int scratch = alloc_local_i64();
            emit_local_set(scratch);
            emit_local_get(scratch);
            emit_i64_const(0);
            emit_op(OP_I64_GT_S);
            emit_local_get(scratch);
            emit_i64_const(0);
            emit_op(OP_I64_LT_S);
            emit_op(OP_I32_SUB);
        } else {
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

    /* LBOUND */
    if (strcmp(name, "LBOUND") == 0) {
        need(TOK_NAME); need(TOK_RP);
        emit_i32_const(option_base);
        vpush(T_I32);
        return 1;
    }
    /* EOF */
    if (strcmp(name, "EOF") == 0) {
        need(TOK_NUMBER);
        int ch = tokv;
        if (ch < 1 || ch > 4) error_at("channel must be 1-4");
        need(TOK_RP);
        emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
        emit_i32_load(0);
        emit_call(IMP_FILE_EOF);
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

static void relation(void);

/* Expression nesting limit. Every level of parentheses, call arguments, array
 * indices, NOT or unary minus recurses through about 300 bytes of stack
 * (measured for the ESP32 build), and on the device the compiler runs on a task
 * with an 8 KB stack -- unlimited nesting overflowed it. */
#ifdef BAS2WASM_EMBEDDED
  #define BW_MAX_EXPR_DEPTH 12
#else
  #define BW_MAX_EXPR_DEPTH 100
#endif
static int bw_expr_depth;

static void base_expr_inner(void);

void base_expr(void) {
    if (bw_expr_depth >= BW_MAX_EXPR_DEPTH) {
        if (!had_error) error_at("expression nested too deeply");
        emit_i32_const(0);
        vpush(T_I32);
        return;
    }
    bw_expr_depth++;
    base_expr_inner();
    bw_expr_depth--;
}

static void base_expr_inner(void) {
    if (want(TOK_NOT)) {
        /* NOT binds LOOSER than arithmetic and comparison (QuickBASIC, and
         * this project's own precedence test header): its operand is a whole
         * relational expression. It used to take a single base expression, so
         * `IF NOT X = 5` meant `(NOT X) = 5` and `NOT A$ = B$` was rejected. */
        pin_read_call_end = -1;
        relation();
        if (vsp > 0 && vstack[vsp-1] == T_STR) { error_at("cannot use NOT on strings"); return; }
        if (pin_read_call_end >= 0 && pin_read_call_end == CODE->len) {
            /* the operand was exactly PIN_READ(...): nothing emitted after it */
            error_at("NOT PIN_READ() is always true (NOT is bitwise and the level is 1 or 0); "
                     "use PIN_LOW(pin) or PIN_READ(pin) = 0");
            return;
        }
        coerce_i32();
        emit_i32_const(-1);
        emit_op(OP_I32_XOR);
    } else if (want(TOK_NUMBER)) {
        if (tok_num_is_i64) {
            emit_i64_const(tokq);
            vpush(T_I64);
        } else {
            emit_i32_const(tokv);
            vpush(T_I32);
        }
    } else if (want(TOK_FLOAT)) {
        emit_f32_const(tokf);
        vpush(T_F32);
    } else if (want(TOK_STRING)) {
        emit_i32_const(tokv);
        vpush(T_STR);
    } else if (want(TOK_NAME)) {
        int var = tokv;
        if (want(TOK_LP)) {
            if (vars[var].mode == VAR_DIM) {
                int ndims = 0;
                int idx_locals[8];
                int dim_local = alloc_local();
                int flat_local = alloc_local();

                emit_i32_const(0);
                emit_local_set(flat_local);
                bw_emit_array_null_check(var);

                do {
                    if (ndims >= 8) { error_at("too many dimensions (max 8)"); break; }
                    expr(); coerce_i32(); vpop();
                    idx_locals[ndims] = alloc_local();
                    emit_local_set(idx_locals[ndims]);

                    emit_global_get(vars[var].global_idx);
                    emit_i32_const((ndims + 1) * 4);
                    emit_op(OP_I32_ADD);
                    emit_i32_load(0); /* upper bound */
                    emit_i32_const(option_base);
                    emit_op(OP_I32_SUB);
                    emit_i32_const(1);
                    emit_op(OP_I32_ADD); /* extent */
                    emit_local_set(dim_local);
                    bw_emit_index_check(idx_locals[ndims], dim_local);

                    emit_local_get(flat_local);
                    emit_local_get(dim_local);
                    emit_op(OP_I32_MUL);
                    emit_local_get(idx_locals[ndims]);
                    emit_i32_const(option_base);
                    emit_op(OP_I32_SUB);
                    emit_op(OP_I32_ADD);
                    emit_local_set(flat_local);

                    ndims++;
                } while (want(TOK_COMMA));
                need(TOK_RP);

                if (vars[var].dim_count > 0 && vars[var].dim_count != ndims)
                    error_at("array index dimension mismatch");

                emit_global_get(vars[var].global_idx);
                emit_i32_const((ndims + 1) * 4);
                emit_op(OP_I32_ADD);
                emit_local_get(flat_local);
                emit_i32_const(vars[var].type == T_I64 ? 8 : 4); emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
                if (vars[var].type == T_I64) {
                    emit_i64_load(0);
                    vpush(T_I64);
                } else if (vars[var].type == T_F32) {
                    emit_f32_load(0);
                    vpush(T_F32);
                } else if (vars[var].type == T_STR) {
                    /* Borrowed pointer to the element's string (no copy): whoever
                     * stores it elsewhere copies it then. Returning a fresh
                     * duplicate here leaked one pool block per element read. */
                    emit_i32_load(0);
                    vpush(T_STR);
                    vown[vsp-1] = STR_VAR;
                } else {
                    emit_i32_load(0);
                    vpush(T_I32);
                }
            } else if (!compile_builtin_expr(vars[var].name)) {
                /* Call a user SUB/FUNCTION. The argument types are popped
                 * again before the result is pushed (they used to be left on
                 * the type stack), and temporary string arguments are freed
                 * after the call -- the callee copies its string parameters. */
                int base_vsp = vsp;
                int nargs = 0;
                int holds[8];
                if (!want(TOK_RP)) {
                    do {
                        expr();
                        if (nargs < vars[var].param_count &&
                            vars[vars[var].param_vars[nargs]].type_set &&
                            vars[vars[var].param_vars[nargs]].type == T_F32)
                            coerce_f32();
                        else if (nargs < vars[var].param_count &&
                                 vars[vars[var].param_vars[nargs]].type_set &&
                                 vars[vars[var].param_vars[nargs]].type == T_I64)
                            coerce_i64();
                        else if (nargs >= vars[var].param_count ||
                                 !vars[vars[var].param_vars[nargs]].type_set ||
                                 vars[vars[var].param_vars[nargs]].type != T_STR)
                            coerce_i32();
                        else if (vsp > 0 && vstack[vsp-1] != T_STR)
                            error_at("string parameter needs a string argument");
                        if (nargs < 8) holds[nargs] = bw_str_hold();
                        nargs++;
                    } while (want(TOK_COMMA));
                    need(TOK_RP);
                }
                if (vars[var].mode != VAR_SUB) {
                    error_at("not a function");
                } else {
                    if (nargs != vars[var].param_count)
                        error_at("wrong number of arguments");
                    emit_call(IMP_COUNT + vars[var].func_local_idx);
                    for (int i = 0; i < nargs && i < 8; i++) bw_str_release(holds[i]);
                }
                vsp = base_vsp;
                if (vars[var].type_set && vars[var].type == T_STR)
                    vpush_temp_str();          /* a FUNCTION hands back an owned string */
                else
                    vpush(vars[var].type_set ? vars[var].type : T_I32);
            }
        } else {
            emit_global_get(vars[var].global_idx);
            if (vars[var].mode == VAR_DIM) {
                /* A bare array name is the array's pointer (an i32), whatever
                 * the element type -- e.g. as a SETLEDRGB / SETARRAY argument. */
                vpush(T_I32);
            } else {
                bw_var_lock_type(var);
                vpush(vars[var].type);
                if (vars[var].type == T_STR) vown[vsp-1] = STR_VAR;
            }
        }
    } else if (want(TOK_LP)) {
        expr();
        need(TOK_RP);
    } else if (want(TOK_UBOUND)) {
        need(TOK_LP); need(TOK_NAME);
        int var = tokv;
        /* UBOUND of a scalar read "the bound" through the scalar's VALUE. */
        if (!had_error && vars[var].mode != VAR_DIM) { error_at("UBOUND expects a DIM array"); }
        int dim_local = alloc_local();
        emit_i32_const(1);
        emit_local_set(dim_local);
        if (want(TOK_COMMA)) {
            expr(); coerce_i32(); vpop();
            emit_local_set(dim_local);
        }
        need(TOK_RP);
        emit_global_get(vars[var].global_idx);
        emit_local_get(dim_local);
        emit_i32_const(4);
        emit_op(OP_I32_MUL);
        emit_op(OP_I32_ADD);
        emit_i32_load(0);
        vpush(T_I32);
    } else {
        error_at("bad expression");
        emit_i32_const(0);
        vpush(T_I32);
    }

}

static void power(void);

/* Unary minus / plus. They bind LOOSER than ^ and tighter than * and /, as in
 * QuickBASIC: -2 ^ 2 is -(2 ^ 2) = -4. (The minus used to be taken inside the
 * base expression, giving (-2) ^ 2 = 4.) Chains like `- -5` and a leading `+`
 * are accepted. */
static void unary(void) {
    if (want(TOK_ADD)) { unary(); return; }
    if (!want(TOK_SUB)) { power(); return; }

    if (bw_expr_depth >= BW_MAX_EXPR_DEPTH) {
        if (!had_error) error_at("expression nested too deeply");
        emit_i32_const(0);
        vpush(T_I32);
        return;
    }
    bw_expr_depth++;
    unary();
    bw_expr_depth--;

    if (vsp > 0 && vstack[vsp-1] == T_STR) { error_at("cannot negate a string"); return; }
    VType t = vpop();
    if (fold_b.valid && fold_b.buf == CODE && fold_b.buf_end == CODE->len) {
        /* Save fold_p/fold_a — emit_*_const shifts them; we want them preserved
         * so an enclosing op can still fold against the predecessor literal. */
        FoldSlot saved_p = fold_p, saved_a = fold_a;
        if (fold_b.valid == 1) {
            int32_t v = fold_b.ival;
            CODE->len = fold_b.buf_start;
            fold_b.valid = 0;
            emit_i32_const((int32_t)(0u - (uint32_t)v));   /* wraps for INT_MIN, like i32.sub */
            fold_p = saved_p; fold_a = saved_a;
            vpush(T_I32);
        } else {
            float v = fold_b.fval;
            CODE->len = fold_b.buf_start;
            fold_b.valid = 0;
            emit_f32_const(-v);
            fold_p = saved_p; fold_a = saved_a;
            vpush(T_F32);
        }
    } else if (t == T_F32) {
        int scratch = alloc_local_f32();
        emit_local_set(scratch);
        emit_f32_const(0.0f);
        emit_local_get(scratch);
        emit_op(OP_F32_SUB);
        vpush(T_F32);
    } else if (t == T_I64) {
        int sc = alloc_local_i64();
        emit_local_set(sc);
        emit_i64_const(0);
        emit_local_get(sc);
        emit_op(OP_I64_SUB);
        vpush(T_I64);
    } else {
        int sc = alloc_local();
        emit_local_set(sc);
        emit_i32_const(0);
        emit_local_get(sc);
        emit_op(OP_I32_SUB);
        vpush(T_I32);
    }
}

static void power(void) {
    base_expr();
    if (want(TOK_POW)) {
        int pos1 = CODE->len;
        FoldSlot save1 = fold_b;
        coerce_f32();
        unary();  /* right-associative; the exponent may carry its own sign (2 ^ -3) */
        int pos2 = CODE->len;
        FoldSlot save2 = fold_b;
        coerce_f32();
        if (save1.valid && save1.buf == CODE && save1.buf_end == pos1 &&
            save2.valid && save2.buf == CODE && save2.buf_end == pos2) {
            float va = (save1.valid == 1) ? (float)save1.ival : save1.fval;
            float vb = (save2.valid == 1) ? (float)save2.ival : save2.fval;
            CODE->len = save1.buf_start;
            fold_a.valid = fold_b.valid = 0;
            emit_f32_const(powf(va, vb));
            vpop(); vpop(); vpush(T_F32);  /* consume LHS+RHS, push result */
            return;
        }
        emit_call(IMP_POWF);
        vpop(); vpop(); vpush(T_F32);  /* consume LHS+RHS, push result */
    }
}

static void factor(void) {
    unary();
    while (want(0), (tok >= TOK_MUL && tok <= TOK_IDIV) || tok == TOK_MOD) {
        int op = tok;
        read_tok();
        unary();
        if (vsp >= 2 && (vstack[vsp-1] == T_STR || vstack[vsp-2] == T_STR)) {
            error_at("cannot use *, /, \\ or MOD on strings"); return;
        }
        switch (op) {
        case TOK_MUL:  emit_binop(OP_I32_MUL, OP_F32_MUL, OP_I64_MUL); break;
        case TOK_DIV:  emit_binop(OP_I32_DIV_S, OP_F32_DIV, OP_I64_DIV_S); break;
        case TOK_IDIV: emit_int_binop(OP_I32_DIV_S); break;
        case TOK_MOD:  emit_int_binop(OP_I32_REM_S); break;
        }
    }
}

static void addition(void) {
    factor();
    while (want(0), tok >= TOK_ADD && tok <= TOK_SUB) {
        int op = tok;
        /* The left operand is on top of the stack right now. If it is a
         * temporary string, keep a copy of the pointer so it can be freed once
         * the concatenation has consumed it (likewise the right operand). */
        int lh = (op == TOK_ADD) ? bw_str_hold() : -1;
        read_tok();
        factor();
        if (op == TOK_ADD && vsp >= 2 && vstack[vsp-1] == T_STR && vstack[vsp-2] == T_STR) {
            int rh = bw_str_hold();
            vpop(); vpop();
            emit_call(IMP_STR_CONCAT);
            bw_str_release(lh);
            bw_str_release(rh);
            vpush_temp_str();
        } else if (vsp >= 2 && (vstack[vsp-1] == T_STR || vstack[vsp-2] == T_STR)) {
            error_at("cannot mix strings and numbers with + or -");
        } else if (op == TOK_ADD) {
            emit_binop(OP_I32_ADD, OP_F32_ADD, OP_I64_ADD);
        } else {
            emit_binop(OP_I32_SUB, OP_F32_SUB, OP_I64_SUB);
        }
    }
}

static void relation(void) {
    addition();
    while (want(0), tok >= TOK_EQ && tok <= TOK_GE) {
        int op = tok;
        int lh = bw_str_hold();     /* left operand, if a temporary string */
        read_tok();
        addition();
        if (vsp >= 2 && vstack[vsp-1] == T_STR && vstack[vsp-2] == T_STR) {
            int rh = bw_str_hold();
            vpop(); vpop();
            emit_call(IMP_STR_CMP);
            bw_str_release(lh);
            bw_str_release(rh);
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
            case TOK_EQ: emit_compare(OP_I32_EQ, OP_F32_EQ, OP_I64_EQ); break;
            case TOK_LT: emit_compare(OP_I32_LT_S, OP_F32_LT, OP_I64_LT_S); break;
            case TOK_GT: emit_compare(OP_I32_GT_S, OP_F32_GT, OP_I64_GT_S); break;
            case TOK_NE: emit_compare(OP_I32_NE, OP_F32_NE, OP_I64_NE); break;
            case TOK_LE: emit_compare(OP_I32_LE_S, OP_F32_LE, OP_I64_LE_S); break;
            case TOK_GE: emit_compare(OP_I32_GE_S, OP_F32_GE, OP_I64_GE_S); break;
            }
        }
    }
}

/* Combine the two values on top of the stack with AND / OR / XOR. */
static void emit_logic(int op) {
    VType b = vpop(), a = vpop();
    if (a == T_STR || b == T_STR) {
        error_at("cannot use AND/OR/XOR on strings");
    }
    if (a == T_I64 || b == T_I64) {
        if (b == T_F32) emit_op(OP_I64_TRUNC_F32_S);
        else if (b == T_I32) emit_op(OP_I64_EXTEND_I32_S);
        if (a == T_F32) {
            int sc = alloc_local_i64();
            emit_local_set(sc);
            emit_op(OP_I64_TRUNC_F32_S);
            emit_local_get(sc);
        } else if (a == T_I32) {
            int sc = alloc_local_i64();
            emit_local_set(sc);
            emit_op(OP_I64_EXTEND_I32_S);
            emit_local_get(sc);
        }
        emit_op(op == TOK_AND ? OP_I64_AND : op == TOK_OR ? OP_I64_OR : OP_I64_XOR);
        vpush(T_I64);
    } else {
        if (b == T_F32) emit_op(OP_I32_TRUNC_F32_S);
        if (a == T_F32) {
            int sc = alloc_local();
            emit_local_set(sc);
            emit_op(OP_I32_TRUNC_F32_S);
            emit_local_get(sc);
        }
        emit_op(op == TOK_AND ? OP_I32_AND : op == TOK_OR ? OP_I32_OR : OP_I32_XOR);
        vpush(T_I32);
    }
}

/* Logical operators, loosest last: AND binds tighter than OR, OR tighter than
 * XOR (QuickBASIC order). They used to share one left-to-right level, so
 * `A OR B AND C` evaluated as `(A OR B) AND C`. NOT sits between comparison
 * and AND -- see base_expr. */
static void and_expr(void) {
    relation();
    while (want(0), tok == TOK_AND) {
        read_tok();
        relation();
        emit_logic(TOK_AND);
    }
}

static void or_expr(void) {
    and_expr();
    while (want(0), tok == TOK_OR) {
        read_tok();
        and_expr();
        emit_logic(TOK_OR);
    }
}

void expr(void) {
    or_expr();
    while (want(0), tok == TOK_XOR) {
        read_tok();
        or_expr();
        emit_logic(TOK_XOR);
    }
}
