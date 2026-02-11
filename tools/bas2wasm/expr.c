/*
 * expr.c — expression parser, builtin function tables, constant folding
 */
#include "bas2wasm.h"

/* ================================================================
 *  Binary op helpers
 * ================================================================ */

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

/* ================================================================
 *  Built-in Function Tables
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
    {"VAL#",     1, IMP_STR_TO_FLOAT, T_F32, {SA, 0, 0}},
    {NULL, 0, 0, 0, {0, 0, 0}}
};
#undef SA
#undef SI

/* ================================================================
 *  compile_builtin_expr
 * ================================================================ */

int compile_builtin_expr(const char *name) {
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
            ImportDef *id = &imp_defs[b->imp];
            vpush(id->nr > 0 && id->r[0] == WASM_F32 ? T_F32 : T_I32);
        }
        return 1;
    }

    /* Float math builtins */
    for (const FloatMathBI *b = float_math_bi; b->name; b++) {
        if (strcmp(name, b->name) != 0) continue;
        for (int i = 0; i < b->nargs; i++) {
            if (i > 0) need(TOK_COMMA);
            expr(); coerce_f32();
        }
        need(TOK_RP);
        if (b->imp >= 0)
            emit_call(b->imp);
        else
            emit_op(b->opcode);
        vpush(T_F32);
        return 1;
    }

    /* String builtins */
    for (const StringBI *b = string_bi; b->name; b++) {
        if (strcmp(name, b->name) != 0) continue;
        for (int i = 0; i < b->nargs; i++) {
            if (i > 0) need(TOK_COMMA);
            expr();
            if (b->arg_types[i] == 1) coerce_i32();
        }
        need(TOK_RP);
        emit_call(b->imp);
        vpush(b->result);
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
        emit_local_get(val);
        emit_local_get(lo);
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
        emit_local_get(val);
        emit_i32_const(0);
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
        int ev = alloc_local(); emit_local_set(ev);
        expr(); coerce_i32(); need(TOK_COMMA);
        int src = alloc_local(); emit_local_set(src);
        expr(); coerce_i32(); need(TOK_COMMA);
        int cond = alloc_local(); emit_local_set(cond);
        expr(); coerce_i32(); need(TOK_COMMA);
        int trig = alloc_local(); emit_local_set(trig);
        expr(); coerce_i32(); need(TOK_RP);
        int tout = alloc_local(); emit_local_set(tout);
        emit_local_get(ev);
        emit_i32_const(4);
        emit_op(OP_I32_EQ);
        emit_if_void();
            emit_local_get(trig);
            emit_local_get(cond);
            emit_i32_const(6);
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_i32_const(3600000); emit_op(OP_I32_MUL);
            emit_else();
                emit_local_get(cond);
                emit_i32_const(7);
                emit_op(OP_I32_EQ);
                emit_if_void();
                    emit_i32_const(60000); emit_op(OP_I32_MUL);
                emit_else();
                    emit_local_get(cond);
                    emit_i32_const(8);
                    emit_op(OP_I32_EQ);
                    emit_if_void();
                        emit_i32_const(1000); emit_op(OP_I32_MUL);
                    emit_end();
                emit_end();
            emit_end();
            emit_call(IMP_DELAY_MS);
            emit_i32_const(1);
        emit_else();
            emit_local_get(ev);
            emit_i32_const(5);
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_local_get(tout);
                emit_call(IMP_WAIT_PPS);
            emit_else();
                emit_local_get(ev);
                emit_i32_const(6);
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
    /* SETLEDRGB(aR, aG, aB) */
    if (strcmp(name, "SETLEDRGB") == 0) {
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_COMMA);
        expr(); coerce_i32(); need(TOK_RP);
        int ab = alloc_local(), ag = alloc_local(), ar = alloc_local();
        emit_local_set(ab); emit_local_set(ag); emit_local_set(ar);
        int i = alloc_local();
        emit_i32_const(0); emit_local_set(i);
        emit_block(); emit_loop();
            emit_local_get(i);
            emit_i32_const(1); emit_call(IMP_LED_COUNT);
            emit_op(OP_I32_GE_S);
            emit_br_if(1);
            emit_i32_const(1);
            emit_local_get(i);
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
    /* SETARRAY */
    if (strcmp(name, "SETARRAY") == 0) {
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
        }
        need(TOK_RP);
        emit_i32_const(0); vpush(T_I32);
        return 1;
    }
    /* STR$ — special: dispatches based on type */
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
    /* INSTR — optional 3rd arg */
    if (strcmp(name, "INSTR") == 0) {
        expr(); need(TOK_COMMA);
        expr();
        if (want(TOK_COMMA)) {
            expr(); coerce_i32();
        } else {
            emit_i32_const(1);
        }
        need(TOK_RP);
        emit_call(IMP_STR_INSTR);
        vpush(T_I32);
        return 1;
    }
    if (strcmp(name, "SGN") == 0) {
        expr(); need(TOK_RP);
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
        emit_i32_const(1);
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

void base_expr(void) {
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
        emit_i32_const(tokv);
        vpush(T_STR);
    } else if (want(TOK_NAME)) {
        int var = tokv;
        if (want(TOK_LP)) {
            if (vars[var].mode == VAR_DIM) {
                expr(); coerce_i32(); vpop();
                need(TOK_RP);
                int idx_local = alloc_local();
                emit_local_set(idx_local);
                emit_global_get(vars[var].global_idx);
                emit_local_get(idx_local);
                emit_i32_const(4); emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
                emit_i32_load(0);
                vpush(T_I32);
            } else if (!compile_builtin_expr(vars[var].name)) {
                int nargs = 0;
                if (!want(TOK_RP)) {
                    do {
                        expr();
                        if (nargs < vars[var].param_count &&
                            vars[vars[var].param_vars[nargs]].type_set &&
                            vars[vars[var].param_vars[nargs]].type == T_F32)
                            coerce_f32();
                        else if (nargs >= vars[var].param_count ||
                                 !vars[vars[var].param_vars[nargs]].type_set ||
                                 vars[vars[var].param_vars[nargs]].type != T_STR)
                            coerce_i32();
                        nargs++;
                    } while (want(TOK_COMMA));
                    need(TOK_RP);
                }
                if (vars[var].mode != VAR_SUB) {
                    error_at("not a function");
                } else {
                    emit_call(IMP_COUNT + vars[var].func_local_idx);
                }
                vpush(vars[var].type_set ? vars[var].type : T_I32);
            }
        } else {
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

static void power(void) {
    base_expr();
    if (want(TOK_POW)) {
        int pos1 = CODE->len;
        FoldSlot save1 = fold_b;
        coerce_f32();
        power();  /* right-associative */
        int pos2 = CODE->len;
        FoldSlot save2 = fold_b;
        coerce_f32();
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

void expr(void) {
    relation();
    while (want(0), tok == TOK_AND || tok == TOK_OR || tok == TOK_XOR) {
        int op = tok;
        read_tok();
        relation();
        VType b = vpop(), a = vpop();
        if (a == T_STR || b == T_STR) {
            error_at("cannot use AND/OR/XOR on strings");
        }
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
