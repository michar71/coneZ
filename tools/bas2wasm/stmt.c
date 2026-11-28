/*
 * stmt.c — statement parser
 */
#include "bas2wasm.h"

static int prints_fmt_off = -1;

/* > 0 while compiling the statement(s) of a single-line IF: lets stmt() stop
 * at an ELSE instead of reporting it as trailing junk. */
static int single_line_if;

void stmt_reset(void) {
    prints_fmt_off = -1;
    single_line_if = 0;
}

static void compile_format(void) {
    need(TOK_STRING);
    int raw_off = tokv;
    char raw[512];
    int raw_len = data_len - raw_off;
    if (raw_len < 0) raw_len = 0;
    if (raw_len >= (int)sizeof(raw)) raw_len = (int)sizeof(raw) - 1;
#ifdef BAS2WASM_USE_PSRAM
    bw_psram_read(data_buf + raw_off, raw, raw_len);
#else
    memcpy(raw, data_buf + raw_off, raw_len);
#endif
    raw[raw_len] = 0;

    /* Build the C-style format string. `%`/`&`/`$` translate to a printf
     * conversion, but the FINAL conversion char is decided per-argument
     * (once we know its actual type) — see the patching below. We emit a
     * placeholder 'd' here and remember each spec's local offset + which
     * BASIC spec char it was.
     *
     * The format buffer is laid out exactly as host_printf reads it -- the
     * wasm32 C varargs layout that c2wasm also emits: an int or string
     * pointer is a 4-byte cell; a float is passed as a DOUBLE, 8 bytes at an
     * 8-byte-aligned offset (%f always takes a double). It used to store the
     * raw f32 in a 4-byte cell, which the host then read as half of a double:
     * FORMAT "&" printed a wrong value and shifted every later argument on
     * real hosts, while the Node test harness (which read a Float32) passed.
     * An i64 is rendered to a string and printed with %s, and type/spec
     * mismatches (i32→&, f32→%) are coerced rather than reinterpreted. */
    char cfmt[512];
    int ci = 0;
    int spec_local[FMT_BUF_SIZE / 4];   /* cfmt index of each conversion char */
    char spec_raw[FMT_BUF_SIZE / 4];    /* '%' / '&' / '$' per spec */
    int nspec = 0;
    for (const char *p = raw; *p; p++) {
        if (ci >= (int)sizeof(cfmt) - 3) { error_at("FORMAT string too long"); return; }
        if (*p == '%' || *p == '$' || *p == '&') {
            if (nspec >= FMT_BUF_SIZE / 4) { error_at("too many FORMAT specifiers"); return; }
            cfmt[ci++] = '%';
            spec_raw[nspec] = *p;
            spec_local[nspec] = ci;     /* conversion char goes here */
            cfmt[ci++] = 'd';           /* placeholder, patched per arg */
            nspec++;
        } else {
            cfmt[ci++] = *p;
        }
    }
    cfmt[ci++] = '\n'; cfmt[ci] = 0;
    data_len = raw_off;
    int fmt_off = add_string(cfmt, ci);

    /* Arguments are evaluated left to right onto the WASM operand stack and
     * only THEN moved into the 0xF000 buffer, last first. Storing each one as
     * it was evaluated let a later argument's code overwrite the earlier cells
     * -- a function call that itself prints (FORMAT, `>`, PRINTS all use the
     * same buffer) made `FORMAT "% $", 42, SHOUT$(1)` print 0 for the 42. */
    int nargs = 0;
    int arg_off = 0;            /* byte offset of the next argument in the buffer */
    uint8_t arg_cell[FMT_BUF_SIZE / 4];     /* each argument's offset / 4 */
    uint8_t arg_kind[FMT_BUF_SIZE / 4];     /* 0 = i32 cell, 1 = f64, 2 = i32 cell holding a temporary string */
    while (want(TOK_COMMA)) {
        if (nargs >= FMT_BUF_SIZE / 4) { error_at("too many FORMAT arguments"); return; }
        char spec = (nargs < nspec) ? spec_raw[nargs] : '%';
        char conv;
        int kind;
        expr();
        int arg_is_temp = (vsp > 0 && vstack[vsp-1] == T_STR && vown[vsp-1] == STR_TEMP);
        VType t = vpop();
        /* A number prints as a float under `&`, and also when a float is given
         * to `$` (which used to truncate it to an integer: 1.5 printed as 1). */
        int is_float = (t != T_I64 && t != T_STR && (spec == '&' || (spec == '$' && t == T_F32)));
        /* Capacity is checked once the argument's size is known: a float needs
         * padding to 8 plus 8 bytes, everything else a 4-byte cell. */
        {
            int end = is_float ? ((arg_off + 7) & ~7) + 8 : arg_off + 4;
            if (end > FMT_BUF_SIZE) { error_at("too many FORMAT arguments"); return; }
        }
        if (t == T_I64) {
            /* i64 — render to a pool string and print it with %s (exact,
             * same as `>`/STR$). */
            emit_call(IMP_STR_FROM_I64);
            kind = 2;
            conv = 's';
        } else if (t == T_STR) {
            kind = arg_is_temp ? 2 : 0;
            conv = 's';
        } else if (is_float) {
            /* %f takes a double at an 8-byte-aligned offset. An integer is
             * converted straight to f64 -- going through f32 first lost
             * precision above 2^24 (16777217 printed as 16777216). */
            if (t == T_F32) emit_op(OP_F64_PROMOTE_F32);
            else emit_op(OP_F64_CONVERT_I32_S);
            arg_off = (arg_off + 7) & ~7;
            kind = 1;
            conv = 'f';
        } else {
            /* int intent (`%`, or `$` given an integer). A float is truncated
             * with the SATURATING conversion: the plain one traps on NaN,
             * infinity or anything outside the i32 range, so printing such a
             * value (a bad sensor reading) aborted the whole script. */
            if (t == T_F32) { buf_byte(CODE, 0xFC); buf_byte(CODE, 0x00); }   /* i32.trunc_sat_f32_s */
            kind = 0;
            conv = 'd';
        }
        arg_cell[nargs] = (uint8_t)(arg_off / 4);
        arg_kind[nargs] = (uint8_t)kind;
        arg_off += (kind == 1) ? 8 : 4;
        if (nargs < nspec)
            dbuf_set(fmt_off + spec_local[nargs], conv);
        nargs++;
    }

    /* One argument per placeholder. With too few, the leftover placeholders
     * printed whatever an earlier FORMAT had left in the buffer; with too many,
     * the extras were evaluated and silently dropped. */
    if (nargs != nspec) {
        char msg[72];
        snprintf(msg, sizeof(msg), "FORMAT has %d placeholder%s but %d argument%s",
                 nspec, nspec == 1 ? "" : "s", nargs, nargs == 1 ? "" : "s");
        error_at(msg);
        return;
    }

    /* Move the values from the operand stack into the buffer, last first. */
    {
        int ti = -1, tf = -1;
        for (int i = nargs - 1; i >= 0; i--) {
            if (arg_kind[i] == 1) {
                if (tf < 0) tf = bw_alloc_local(WASM_F64, 0);
                emit_local_set(tf);
                emit_i32_const(0xF000);
                emit_local_get(tf);
                buf_byte(CODE, OP_F64_STORE); buf_uleb(CODE, 3); buf_uleb(CODE, arg_cell[i] * 4);
            } else {
                if (ti < 0) ti = alloc_local();
                emit_local_set(ti);
                emit_i32_const(0xF000);
                emit_local_get(ti);
                emit_i32_store(arg_cell[i] * 4);
            }
        }
    }

    emit_i32_const(fmt_off);
    emit_i32_const(0xF000);
    emit_call(IMP_HOST_PRINTF);
    emit_drop();
    /* Free temporary strings (a string expression's result, or the text an
     * i64 was rendered to) now that they have been printed. */
    for (int i = 0; i < nargs; i++) {
        if (arg_kind[i] != 2) continue;
        emit_i32_const(0xF000);
        emit_i32_load(arg_cell[i] * 4);
        emit_call(IMP_STR_FREE);
    }
}

static void compile_prints(void) {
    expr();
    int is_temp = (vsp > 0 && vstack[vsp-1] == T_STR && vown[vsp-1] == STR_TEMP);
    VType t = vpop();
    if (t != T_STR) { error_at("PRINTS requires a string"); return; }
    int fmt_buf = 0xF000;
    emit_i32_const(fmt_buf);
    int tmp = alloc_local();
    emit_local_set(tmp);
    int tmp2 = alloc_local();
    emit_local_set(tmp2);
    emit_local_get(tmp);
    emit_local_get(tmp2);
    emit_i32_store(0);

    if (prints_fmt_off < 0) prints_fmt_off = add_string("%s\n", 3);
    emit_i32_const(prints_fmt_off);
    emit_i32_const(fmt_buf);
    emit_call(IMP_HOST_PRINTF);
    emit_drop();
    if (is_temp) bw_str_release(tmp2);      /* tmp2 holds the string pointer */
}

/* DECLARE FUNCTION name [params...]  /  DECLARE SUB name [params...]
 * Reserves a func_local_idx and records the param list so later code can
 * call the function before its body is compiled. The matching definition
 * must follow somewhere in the source. */
static void compile_declare(void) {
    if (cur_func != 0) {
        error_at("DECLARE must be at top level");
        return;
    }
    if (!want(TOK_FUNCTION) && !want(TOK_KW_SUB)) {
        error_at("DECLARE must be followed by FUNCTION or SUB");
        return;
    }
    need(TOK_NAME);
    int var = tokv;
    if (vars[var].mode == VAR_SUB) {
        error_at("function already declared or defined");
        return;
    }
    vars[var].mode = VAR_SUB;
    vars[var].is_declared = 1;

    if (nfuncs >= MAX_FUNCS) { error_at("too many SUB/FUNCTION definitions"); return; }
    int fi = nfuncs++;
    vars[var].func_local_idx = fi;
    FuncCtx *f = &func_bufs[fi];
    buf_init(&f->code);
    f->nparams = 0;
    f->nlocals = 0;
    f->ncall_fixups = 0;
    f->sub_var = var;

    int params[8], np = 0;
    if (!want(TOK_EOF)) {
        ungot = 1;
        do {
            need(TOK_NAME);
            if (np >= 8) { error_at("too many SUB parameters (max 8)"); return; }
            params[np++] = tokv;
        } while (want(TOK_COMMA));
    }
    vars[var].param_count = np;
    for (int i = 0; i < np; i++) vars[var].param_vars[i] = params[i];
    f->nparams = np;
    for (int i = 0; i < np; i++) {
        bw_var_lock_type(params[i]);    /* an untyped parameter is an integer */
        VType pt = vars[params[i]].type;
        f->param_types[i] = wasm_type_for_vtype(pt);
    }
}

static void compile_sub(void) {
    if (cur_func != 0) {
        error_at("nested SUB/FUNCTION definitions are not supported");
        return;
    }
    need(TOK_NAME);
    int var = tokv;
    int was_declared = (vars[var].mode == VAR_SUB && vars[var].is_declared);
    int fi;
    if (was_declared) {
        /* Fill in the body of a previously DECLAREd function. */
        fi = vars[var].func_local_idx;
        vars[var].is_declared = 0;
    } else {
        if (vars[var].mode == VAR_SUB) {
            error_at("function already defined");
            return;
        }
        vars[var].mode = VAR_SUB;
        if (nfuncs >= MAX_FUNCS) { error_at("too many SUB/FUNCTION definitions"); return; }
        fi = nfuncs++;
        vars[var].func_local_idx = fi;
        FuncCtx *fc = &func_bufs[fi];
        buf_init(&fc->code);
        fc->nparams = 0;
        fc->nlocals = 0;
        fc->ncall_fixups = 0;
        fc->sub_var = var;
    }
    FuncCtx *f = &func_bufs[fi];

    int params[8], np = 0;
    if (!want(TOK_EOF)) {
        ungot = 1;
        do {
            need(TOK_NAME);
            if (np >= 8) { error_at("too many SUB parameters (max 8)"); return; }
            params[np++] = tokv;
        } while (want(TOK_COMMA));
    }
    if (was_declared) {
        if (np != vars[var].param_count) {
            error_at("definition parameter count differs from DECLARE");
            return;
        }
        /* The body uses the DEFINITION's parameter names, so those are the
         * ones to save/restore; keeping the DECLARE's names made the exit code
         * restore the wrong globals. The types must agree, because calls were
         * already compiled against the DECLARE (a mismatch gave an invalid
         * module). */
        for (int i = 0; i < np; i++) {
            bw_var_lock_type(params[i]);
            if (vars[params[i]].type != vars[vars[var].param_vars[i]].type) {
                error_at("definition parameter type differs from DECLARE");
                return;
            }
            vars[var].param_vars[i] = params[i];
        }
    } else {
        vars[var].param_count = np;
        for (int i = 0; i < np; i++) vars[var].param_vars[i] = params[i];
    }
    f->nparams = np;
    for (int i = 0; i < np; i++) {
        bw_var_lock_type(params[i]);    /* an untyped parameter is an integer */
        VType pt = vars[params[i]].type;
        f->param_types[i] = wasm_type_for_vtype(pt);
    }

    int prev_func = cur_func;
    int prev_depth = block_depth;
    cur_func = fi;
    block_depth = 0;

    int saved[8];
    for (int i = 0; i < np; i++) {
        VType pt = vars[params[i]].type_set ? vars[params[i]].type : T_I32;
        /* Permanent for the function, and necessarily the first np locals
         * (indices np..2np-1): the epilogue restores from np + i. */
        saved[i] = alloc_local_keep(pt);
    }

    for (int i = 0; i < np; i++) {
        emit_global_get(vars[params[i]].global_idx);
        emit_local_set(saved[i]);
    }
    for (int i = 0; i < np; i++) {
        emit_local_get(i);
        if (vars[params[i]].type == T_STR)
            emit_call(IMP_STR_COPY);
        emit_global_set(vars[params[i]].global_idx);
    }

    if (ctrl_sp >= MAX_CTRL) { error_at("too many nested blocks"); return; }
    ctrl_stk[ctrl_sp].kind = -1;
    ctrl_stk[ctrl_sp].for_var = var;
    ctrl_stk[ctrl_sp].for_limit_local = prev_func;
    ctrl_stk[ctrl_sp].break_depth = prev_depth;
    ctrl_stk[ctrl_sp].if_extra_ends = np;
    ctrl_sp++;
}

/* Block closers must look before they pop. Popping first read ctrl_stk[-1] for
 * a stray WEND / NEXT / END (on the host that slot happened to look like a
 * WHILE, so a stray WEND produced an invalid module with no error), and
 * END SUB never checked that the top entry was the SUB at all -- with a NEXT
 * missing it took the FOR entry for the SUB frame and set cur_func from the
 * FOR's limit local, after which code was emitted through func_bufs[<garbage>]
 * (heap corruption in the firmware build, from a common typo). */
static int ctrl_top_is(int kind) {
    return ctrl_sp > 0 && ctrl_stk[ctrl_sp-1].kind == kind;
}

static void close_sub(void) {
    if (!ctrl_top_is(-1)) {
        error_at(ctrl_sp > 0 ? "unterminated block before END SUB/FUNCTION"
                             : "END SUB/FUNCTION without SUB/FUNCTION");
        return;
    }
    ctrl_sp--;
    int var = ctrl_stk[ctrl_sp].for_var;
    int prev_func = ctrl_stk[ctrl_sp].for_limit_local;
    int prev_depth = ctrl_stk[ctrl_sp].break_depth;
    int np = ctrl_stk[ctrl_sp].if_extra_ends;

    for (int i = 0; i < np; i++) {
        int pvar = vars[var].param_vars[i];
        if (vars[pvar].type == T_STR) {
            emit_global_get(vars[pvar].global_idx);
            emit_call(IMP_STR_FREE);
        }
        emit_local_get(np + i);
        emit_global_set(vars[pvar].global_idx);
    }

    for (int i = 0; i < vars[var].local_count; i++) {
        int lvar = vars[var].local_vars[i];
        if (vars[lvar].type == T_STR) {
            emit_global_get(vars[lvar].global_idx);
            emit_call(IMP_STR_FREE);
        }
        emit_local_get(vars[var].local_save_idx[i]);
        emit_global_set(vars[lvar].global_idx);
    }

    if (vars[var].type_set && vars[var].type == T_F32)
        emit_f32_const(0.0f);
    else if (vars[var].type_set && vars[var].type == T_I64)
        emit_i64_const(0);
    else
        emit_i32_const(0);
    emit_end();

    cur_func = prev_func;
    block_depth = prev_depth;
}

static void close_while(void) {
    if (!ctrl_top_is(CTRL_WHILE)) { error_at("WEND without WHILE"); return; }
    ctrl_sp--;
    emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
    emit_end();
    emit_end();
}

static void close_for(void) {
    if (!ctrl_top_is(CTRL_FOR)) { error_at("NEXT without FOR"); return; }
    ctrl_sp--;
    int var = ctrl_stk[ctrl_sp].for_var;
    /* Optional loop variable: NEXT I */
    if (want(TOK_NAME) && tokv != var) { error_at("NEXT variable does not match its FOR"); return; }
    int is_float = (vars[var].type_set && vars[var].type == T_F32);
    int is_i64 = (vars[var].type_set && vars[var].type == T_I64);
    emit_global_get(vars[var].global_idx);
    if (ctrl_stk[ctrl_sp].for_has_step) {
        emit_local_get(ctrl_stk[ctrl_sp].for_step_local);
    } else {
        if (is_float) emit_f32_const(1.0f);
        else if (is_i64) emit_i64_const(1);
        else emit_i32_const(1);
    }
    emit_op(is_float ? OP_F32_ADD : (is_i64 ? OP_I64_ADD : OP_I32_ADD));
    emit_global_set(vars[var].global_idx);
    emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
    emit_end();
    emit_end();
    free_local(ctrl_stk[ctrl_sp].for_limit_local);
    if (ctrl_stk[ctrl_sp].for_has_step) free_local(ctrl_stk[ctrl_sp].for_step_local);
}

static void compile_end(void) {
    int kw = read_tok();
    if (kw == TOK_KW_SUB || kw == TOK_FUNCTION) {
        close_sub();
    } else if (kw == TOK_IF) {
        if (!ctrl_top_is(CTRL_IF)) { error_at("END IF without IF"); return; }
        ctrl_sp--;
        int extras = ctrl_stk[ctrl_sp].if_extra_ends;
        emit_end();
        for (int i = 0; i < extras; i++) emit_end();
    } else if (kw == TOK_SELECT) {
        if (!ctrl_top_is(CTRL_SELECT)) { error_at("END SELECT without SELECT"); return; }
        ctrl_sp--;
        int extras = ctrl_stk[ctrl_sp].if_extra_ends;
        for (int i = 0; i < extras; i++) emit_end();
        emit_end();
        if (ctrl_stk[ctrl_sp].for_has_step)         /* temporary string selector */
            bw_str_release(ctrl_stk[ctrl_sp].for_var);
        free_local(ctrl_stk[ctrl_sp].for_var);     /* the SELECT test value */
    } else {
        error_at("unexpected END");
    }
}

static void compile_while(void) {
    emit_block();
    emit_loop();
    expr(); coerce_i32();
    emit_op(OP_I32_EQZ);
    emit_br_if(1);
    vpop();
    if (ctrl_sp >= MAX_CTRL) { error_at("too many nested blocks"); return; }
    ctrl_stk[ctrl_sp].kind = CTRL_WHILE;
    ctrl_stk[ctrl_sp].break_depth = block_depth - 1;
    ctrl_stk[ctrl_sp].cont_depth = block_depth;
    ctrl_stk[ctrl_sp].if_extra_ends = 0;
    ctrl_sp++;
}

static void compile_for(void) {
    need(TOK_NAME);
    int var = tokv;
    if (vars[var].type == T_STR) { error_at("FOR loop variable cannot be a string"); return; }
    bw_var_lock_type(var);      /* an untyped loop variable is an integer from here on */
    int is_float = (vars[var].type_set && vars[var].type == T_F32);
    int is_i64 = (vars[var].type_set && vars[var].type == T_I64);
    need(TOK_EQ);
    expr();
    if (is_float) coerce_f32(); else if (is_i64) coerce_i64(); else coerce_i32();
    vpop();
    emit_global_set(vars[var].global_idx);
    need(TOK_TO);
    expr();
    if (is_float) coerce_f32(); else if (is_i64) coerce_i64(); else coerce_i32();
    vpop();
    VType loop_t = is_float ? T_F32 : (is_i64 ? T_I64 : T_I32);
    int limit_local = alloc_local_keep(loop_t);    /* live until NEXT */
    emit_local_set(limit_local);

    int step_local = -1;
    int has_step = 0;
    if (want(TOK_STEP)) {
        expr();
        if (is_float) coerce_f32(); else if (is_i64) coerce_i64(); else coerce_i32();
        vpop();
        step_local = alloc_local_keep(loop_t);     /* live until NEXT */
        emit_local_set(step_local);
        has_step = 1;
    }

    emit_block();
    emit_loop();

    if (has_step) {
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(is_float ? OP_F32_GT : (is_i64 ? OP_I64_GT_S : OP_I32_GT_S));
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(is_float ? OP_F32_LT : (is_i64 ? OP_I64_LT_S : OP_I32_LT_S));
        emit_local_get(step_local);
        if (is_float) emit_f32_const(0.0f);
        else if (is_i64) emit_i64_const(0);
        else emit_i32_const(0);
        emit_op(is_float ? OP_F32_GT : (is_i64 ? OP_I64_GT_S : OP_I32_GT_S));
        emit_op(OP_SELECT);
        emit_br_if(1);
    } else {
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(is_float ? OP_F32_GT : (is_i64 ? OP_I64_GT_S : OP_I32_GT_S));
        emit_br_if(1);
    }

    if (ctrl_sp >= MAX_CTRL) { error_at("too many nested blocks"); return; }
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
    int has_then = want(TOK_THEN);
    if (has_then && !want(TOK_EOF)) {
        /* Single-line:  IF cond THEN statement [ELSE statement] */
        emit_if_void();
        int sp0 = ctrl_sp;
        single_line_if++;
        stmt();
        if (!had_error && want(TOK_ELSE)) {
            emit_else();
            stmt();
        }
        single_line_if--;
        /* The statement must be self-contained. `IF X THEN FOR I = 1 TO 3`
         * left a block open inside this `if`, and the module that came out was
         * invalid with no error reported. */
        if (!had_error && ctrl_sp != sp0) {
            error_at("a block cannot be opened or closed by a single-line IF");
            return;
        }
        emit_end();
    } else {
        /* Block IF: "IF cond" or "IF cond THEN" alone on the line. */
        emit_if_void();
        if (ctrl_sp >= MAX_CTRL) { error_at("too many nested blocks"); return; }
        ctrl_stk[ctrl_sp].kind = CTRL_IF;
        ctrl_stk[ctrl_sp].if_extra_ends = 0;
        ctrl_stk[ctrl_sp].for_has_step = 0;     /* reused: 1 once a plain ELSE has been seen */
        ctrl_sp++;
    }
}

/* ELSE / ELSE IF cond [THEN] / ELSEIF cond [THEN] */
static void compile_else_branch(int is_elseif) {
    if (!ctrl_top_is(CTRL_IF)) {
        error_at(is_elseif ? "ELSEIF without IF" : "ELSE without IF"); return;
    }
    /* Only one plain ELSE, and it comes last. A second one produced an invalid
     * module ("else without matching if") with no compile error. */
    if (ctrl_stk[ctrl_sp-1].for_has_step) {
        error_at(is_elseif ? "ELSEIF after ELSE" : "ELSE after ELSE"); return;
    }
    emit_else();
    if (is_elseif || want(TOK_IF)) {
        expr(); coerce_i32(); vpop();
        want(TOK_THEN);                 /* optional */
        emit_if_void();
        ctrl_stk[ctrl_sp-1].if_extra_ends++;
    } else {
        ctrl_stk[ctrl_sp-1].for_has_step = 1;
    }
}

static void compile_else(void) { compile_else_branch(0); }

static void compile_const(void) {
    need(TOK_NAME);
    int var = tokv;
    need(TOK_EQ);
    expr();
    VType et = (vsp > 0) ? vstack[vsp-1] : T_I32;
    if (vars[var].type == T_STR) {
        if (et != T_STR) { error_at("cannot assign a number to a string CONST"); return; }
        bw_str_own();
        vpop();
        emit_global_set(vars[var].global_idx);
    } else {
        if (et == T_STR) { error_at("cannot assign a string to a numeric CONST"); return; }
        if (!vars[var].type_set) {
            vars[var].type = et;
            vars[var].type_set = 1;
        } else {
            coerce_to(vars[var].type);      /* before the vpop -- see assignment */
        }
        vpop();
        emit_global_set(vars[var].global_idx);
    }
    vars[var].is_const = 1;
}

static int parse_dim_indices_to_addr(int var, int *out_addr_local, int *out_dims) {
    int ndims = 0;
    int idx_locals[8];
    int dim_local = alloc_local();
    int flat_local = alloc_local();

    emit_i32_const(0);
    emit_local_set(flat_local);
    bw_emit_array_null_check(var);      /* see bw_emit_index_check */

    do {
        if (ndims >= 8) {
            error_at("too many dimensions (max 8)");
            return 0;
        }
        expr(); coerce_i32(); vpop();
        idx_locals[ndims] = alloc_local();
        emit_local_set(idx_locals[ndims]);

        emit_global_get(vars[var].global_idx);
        emit_i32_const((ndims + 1) * 4);
        emit_op(OP_I32_ADD);
        emit_i32_load(0); /* upper bound for this dimension */
        emit_i32_const(option_base);
        emit_op(OP_I32_SUB);
        emit_i32_const(1);
        emit_op(OP_I32_ADD); /* extent = upper - lower + 1 */
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

    int addr_local = alloc_local();
    emit_global_get(vars[var].global_idx);
    emit_i32_const((ndims + 1) * 4);
    emit_op(OP_I32_ADD);
    emit_local_get(flat_local);
    emit_i32_const((vars[var].type_set && vars[var].type == T_I64) ? 8 : 4);
    emit_op(OP_I32_MUL);
    emit_op(OP_I32_ADD);
    emit_local_set(addr_local);

    *out_addr_local = addr_local;
    *out_dims = ndims;
    return 1;
}

static int parse_dim_decl_sizes(int dim_locals[8], int *out_ndims) {
    int ndims = 0;
    do {
        if (ndims >= 8) {
            error_at("too many dimensions (max 8)");
            return 0;
        }
        expr(); coerce_i32(); vpop();
        dim_locals[ndims] = alloc_local();
        emit_local_set(dim_locals[ndims]);
        ndims++;
    } while (want(TOK_COMMA));
    need(TOK_RP);
    *out_ndims = ndims;
    return 1;
}

static void compile_dim_core(int preserve) {
    need(TOK_NAME);
    int var = tokv;

    need(TOK_LP);
    int dim_locals[8];
    int ndims = 0;
    if (!parse_dim_decl_sizes(dim_locals, &ndims)) return;

    if (preserve && vars[var].mode == VAR_DIM && vars[var].dim_count > 0 && vars[var].dim_count != ndims) {
        error_at("REDIM PRESERVE requires same number of dimensions");
        return;
    }
    if (preserve && vars[var].mode != VAR_DIM) {
        error_at("REDIM PRESERVE requires an existing DIM array");
        return;
    }

    int old_ptr_local = alloc_local();
    int old_count_local = alloc_local();
    int new_count_local = alloc_local();
    int total_words_local = alloc_local();
    int total_bytes_local = alloc_local();
    int new_ptr_local = alloc_local();
    int elem_size = (vars[var].type_set && vars[var].type == T_I64) ? 8 : 4;

    emit_global_get(vars[var].global_idx);
    emit_local_set(old_ptr_local);

    emit_i32_const(1);
    emit_local_set(new_count_local);
    for (int i = 0; i < ndims; i++) {
        emit_local_get(new_count_local);
        emit_local_get(dim_locals[i]);
        emit_i32_const(option_base);
        emit_op(OP_I32_SUB);
        emit_i32_const(1);
        emit_op(OP_I32_ADD);
        emit_op(OP_I32_MUL);
        emit_local_set(new_count_local);
    }

    emit_i32_const(ndims + 1);
    emit_local_get(new_count_local);
    emit_op(OP_I32_ADD);
    emit_local_set(total_words_local);

    emit_i32_const((ndims + 1) * 4);
    emit_local_get(new_count_local);
    emit_i32_const(elem_size);
    emit_op(OP_I32_MUL);
    emit_op(OP_I32_ADD);
    emit_local_set(total_bytes_local);

    emit_i32_const(0);
    emit_local_set(old_count_local);
    /* (old_count_local is left at 0 for non-preserve; preserve doesn't need
     *  it anymore — the element-remap loop below iterates per-dim extents.) */

    if (preserve) {
        /* PRESERVE: allocate a fresh zero-filled buffer (calloc) and copy
         * intersection elements from the old buffer below, taking stride
         * differences into account. Old approach used realloc, which only
         * works correctly for 1D arrays — for multi-dim, the (i,j,...) →
         * flat-offset mapping changes when any non-final dim resizes. */
        emit_i32_const(1);
        emit_local_get(total_bytes_local);
        emit_call(IMP_CALLOC);
        emit_local_set(new_ptr_local);
    } else {
        /* Free whatever array the variable holds NOW, decided at run time. The
         * test used to be "was this name already DIMmed when this statement
         * was compiled", so a DIM that runs more than once -- in a SUB, or in a
         * loop -- never freed the previous array and leaked it every time. The
         * run-time null test also covers DIM after ERASE, which used to read
         * the element count through the null pointer ERASE left. */
        emit_local_get(old_ptr_local);
        emit_if_void();
        {
            /* String array: free every element's pool string before
             * freeing the array memory itself. */
            if (vars[var].type_set && vars[var].type == T_STR) {
                int idx_local = alloc_local();
                int data_base_local = alloc_local();
                int old_n_local = alloc_local();
                int old_ndims = (vars[var].mode == VAR_DIM && vars[var].dim_count > 0)
                                ? vars[var].dim_count : ndims;

                /* Recompute old element count from the header in old_ptr. */
                emit_i32_const(1);
                emit_local_set(old_n_local);
                for (int d = 0; d < old_ndims; d++) {
                    emit_local_get(old_n_local);
                    emit_local_get(old_ptr_local);
                    emit_i32_const((d + 1) * 4);
                    emit_op(OP_I32_ADD);
                    emit_i32_load(0);
                    emit_i32_const(option_base);
                    emit_op(OP_I32_SUB);
                    emit_i32_const(1);
                    emit_op(OP_I32_ADD);
                    emit_op(OP_I32_MUL);
                    emit_local_set(old_n_local);
                }

                emit_local_get(old_ptr_local);
                emit_i32_const((old_ndims + 1) * 4);
                emit_op(OP_I32_ADD);
                emit_local_set(data_base_local);

                emit_i32_const(0);
                emit_local_set(idx_local);
                emit_block();
                emit_loop();
                emit_local_get(idx_local);
                emit_local_get(old_n_local);
                emit_op(OP_I32_GE_S);
                emit_br_if(1);
                emit_local_get(data_base_local);
                emit_local_get(idx_local);
                emit_i32_const(4);
                emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
                emit_i32_load(0);
                emit_call(IMP_STR_FREE);
                emit_local_get(idx_local);
                emit_i32_const(1);
                emit_op(OP_I32_ADD);
                emit_local_set(idx_local);
                emit_br(0);
                emit_end();
                emit_end();
            }
            emit_local_get(old_ptr_local);
            emit_call(IMP_FREE);
        }
        emit_end();     /* end "old array present" */
        emit_i32_const(1);
        emit_local_get(total_bytes_local);
        emit_call(IMP_CALLOC);
        emit_local_set(new_ptr_local);
    }

    /* Out of memory: stop the script with a trap. The result used to go
     * unchecked, so the header and every element were then written through
     * address 0 -- over the program's string constants. */
    emit_local_get(new_ptr_local);
    emit_op(OP_I32_EQZ);
    emit_if_void();
    emit_op(OP_UNREACHABLE);
    emit_end();

    emit_local_get(new_ptr_local);
    emit_global_set(vars[var].global_idx);

    emit_global_get(vars[var].global_idx);
    emit_i32_const(ndims);
    emit_i32_store(0);

    for (int i = 0; i < ndims; i++) {
        emit_global_get(vars[var].global_idx);
        emit_i32_const((i + 1) * 4);
        emit_op(OP_I32_ADD);
        emit_local_get(dim_locals[i]);
        emit_i32_store(0);
    }

    if (preserve) {
        /* PRESERVE element remap: iterate every old element by per-dimension
         * indices (so the (i₁,…,iₙ) → flat-offset mapping respects the
         * actual old strides), and for each element check whether its index
         * is within the new extents. If yes, copy it to the new buffer at
         * the new strides' offset. If no, the element is dropped; for
         * string arrays we must free its pool string before the old buffer
         * goes away. Buffer is calloc-zeroed so cells outside the
         * intersection are already empty. */
        int new_ext[8], old_ext[8], new_str[8], old_str[8], idx[8];
        for (int d = 0; d < ndims; d++) {
            new_ext[d] = alloc_local();
            old_ext[d] = alloc_local();
            new_str[d] = alloc_local();
            old_str[d] = alloc_local();
            idx[d] = alloc_local();
        }
        int in_range = alloc_local();
        int old_off  = alloc_local();
        int new_off  = alloc_local();

        /* new_ext[d] = new_dim_size - option_base + 1 */
        for (int d = 0; d < ndims; d++) {
            emit_local_get(dim_locals[d]);
            emit_i32_const(option_base);
            emit_op(OP_I32_SUB);
            emit_i32_const(1);
            emit_op(OP_I32_ADD);
            emit_local_set(new_ext[d]);
        }
        /* new_str[ndims-1] = 1; new_str[d] = new_str[d+1] * new_ext[d+1] */
        emit_i32_const(1);
        emit_local_set(new_str[ndims - 1]);
        for (int d = ndims - 2; d >= 0; d--) {
            emit_local_get(new_str[d + 1]);
            emit_local_get(new_ext[d + 1]);
            emit_op(OP_I32_MUL);
            emit_local_set(new_str[d]);
        }

        /* If old_ptr is null, nothing to copy. */
        emit_local_get(old_ptr_local);
        emit_if_void();

        /* old_ext[d] from old buffer's header */
        for (int d = 0; d < ndims; d++) {
            emit_local_get(old_ptr_local);
            emit_i32_const((d + 1) * 4);
            emit_op(OP_I32_ADD);
            emit_i32_load(0);
            emit_i32_const(option_base);
            emit_op(OP_I32_SUB);
            emit_i32_const(1);
            emit_op(OP_I32_ADD);
            emit_local_set(old_ext[d]);
        }
        /* old strides */
        emit_i32_const(1);
        emit_local_set(old_str[ndims - 1]);
        for (int d = ndims - 2; d >= 0; d--) {
            emit_local_get(old_str[d + 1]);
            emit_local_get(old_ext[d + 1]);
            emit_op(OP_I32_MUL);
            emit_local_set(old_str[d]);
        }

        /* Nested loops over old extents. Each level's counter is reset to
         * 0 just before its loop opens (so the inner counter restarts on
         * every outer iteration — the bug if you only reset once upfront). */
        for (int d = 0; d < ndims; d++) {
            emit_i32_const(0);
            emit_local_set(idx[d]);
            emit_block();
            emit_loop();
            emit_local_get(idx[d]);
            emit_local_get(old_ext[d]);
            emit_op(OP_I32_GE_S);
            emit_br_if(1);
        }

        /* in_range = AND_d (idx[d] < new_ext[d]) */
        emit_i32_const(1);
        for (int d = 0; d < ndims; d++) {
            emit_local_get(idx[d]);
            emit_local_get(new_ext[d]);
            emit_op(OP_I32_LT_S);
            emit_op(OP_I32_AND);
        }
        emit_local_set(in_range);

        /* old_off = sum(idx[d] * old_str[d]) */
        emit_i32_const(0);
        for (int d = 0; d < ndims; d++) {
            emit_local_get(idx[d]);
            emit_local_get(old_str[d]);
            emit_op(OP_I32_MUL);
            emit_op(OP_I32_ADD);
        }
        emit_local_set(old_off);

        emit_local_get(in_range);
        emit_if_void();
            /* In intersection: copy element. new_off = sum(idx[d] * new_str[d]) */
            emit_i32_const(0);
            for (int d = 0; d < ndims; d++) {
                emit_local_get(idx[d]);
                emit_local_get(new_str[d]);
                emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
            }
            emit_local_set(new_off);

            /* dest = new_data + new_off * elem_size */
            emit_global_get(vars[var].global_idx);
            emit_i32_const((ndims + 1) * 4);
            emit_op(OP_I32_ADD);
            emit_local_get(new_off);
            emit_i32_const(elem_size);
            emit_op(OP_I32_MUL);
            emit_op(OP_I32_ADD);

            /* src = old_data + old_off * elem_size; load value */
            emit_local_get(old_ptr_local);
            emit_i32_const((ndims + 1) * 4);
            emit_op(OP_I32_ADD);
            emit_local_get(old_off);
            emit_i32_const(elem_size);
            emit_op(OP_I32_MUL);
            emit_op(OP_I32_ADD);
            if (elem_size == 8) {
                emit_i64_load(0);
                emit_i64_store(0);
            } else if (vars[var].type_set && vars[var].type == T_F32) {
                emit_f32_load(0);
                emit_f32_store(0);
            } else {
                /* Includes T_STR — moves the i32 pool pointer as-is. */
                emit_i32_load(0);
                emit_i32_store(0);
            }
        emit_else();
            /* Dropped element. For string arrays, free its pool string. */
            if (vars[var].type_set && vars[var].type == T_STR) {
                emit_local_get(old_ptr_local);
                emit_i32_const((ndims + 1) * 4);
                emit_op(OP_I32_ADD);
                emit_local_get(old_off);
                emit_i32_const(elem_size);
                emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
                emit_i32_load(0);
                emit_call(IMP_STR_FREE);
            }
        emit_end();

        /* Close loops in reverse: increment idx[d], br back to loop start,
         * end loop, end block. */
        for (int d = ndims - 1; d >= 0; d--) {
            emit_local_get(idx[d]);
            emit_i32_const(1);
            emit_op(OP_I32_ADD);
            emit_local_set(idx[d]);
            emit_br(0);
            emit_end();
            emit_end();
        }

        /* Free old buffer now that all kept strings have been moved and
         * dropped strings have been freed. */
        emit_local_get(old_ptr_local);
        emit_call(IMP_FREE);

        emit_end();   /* end "if old_ptr non-null" */
    }

    vars[var].mode = VAR_DIM;
    vars[var].dim_count = ndims;
}

static void compile_option(void) {
    need(TOK_BASE);
    need(TOK_NUMBER);
    if (tokv != 0 && tokv != 1) {
        error_at("OPTION BASE must be 0 or 1");
        return;
    }
    option_base = tokv;
}

/* DIM A(3), B(5, 5): a comma-separated list. */
static void compile_dim(void) {
    do { compile_dim_core(0); } while (!had_error && want(TOK_COMMA));
}

static void compile_redim(void) {
    int preserve = want(TOK_PRESERVE) ? 1 : 0;
    do { compile_dim_core(preserve); } while (!had_error && want(TOK_COMMA));
}

static void compile_erase(void) {
    do {
        need(TOK_NAME);
        int var = tokv;
        if (vars[var].mode != VAR_DIM) {
            error_at("ERASE expects DIM array variable");
            return;
        }
        /* For string arrays, free every element's pool string before freeing
         * the array memory. Computes total element count from the stored
         * dimension sizes. */
        if (vars[var].type_set && vars[var].type == T_STR) {
            int count_local = alloc_local();
            int idx_local = alloc_local();
            int data_base_local = alloc_local();
            int ndims = vars[var].dim_count;

            emit_global_get(vars[var].global_idx);
            emit_if_void();   /* skip if pointer is null */
                /* count = product of dim extents */
                emit_i32_const(1);
                emit_local_set(count_local);
                for (int d = 0; d < ndims; d++) {
                    emit_local_get(count_local);
                    emit_global_get(vars[var].global_idx);
                    emit_i32_const((d + 1) * 4);
                    emit_op(OP_I32_ADD);
                    emit_i32_load(0);
                    emit_i32_const(option_base);
                    emit_op(OP_I32_SUB);
                    emit_i32_const(1);
                    emit_op(OP_I32_ADD);
                    emit_op(OP_I32_MUL);
                    emit_local_set(count_local);
                }

                emit_global_get(vars[var].global_idx);
                emit_i32_const((ndims + 1) * 4);
                emit_op(OP_I32_ADD);
                emit_local_set(data_base_local);

                emit_i32_const(0);
                emit_local_set(idx_local);

                emit_block();
                emit_loop();
                emit_local_get(idx_local);
                emit_local_get(count_local);
                emit_op(OP_I32_GE_S);
                emit_br_if(1);

                emit_local_get(data_base_local);
                emit_local_get(idx_local);
                emit_i32_const(4);
                emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
                emit_i32_load(0);
                emit_call(IMP_STR_FREE);

                emit_local_get(idx_local);
                emit_i32_const(1);
                emit_op(OP_I32_ADD);
                emit_local_set(idx_local);
                emit_br(0);
                emit_end();
                emit_end();
            emit_end();
        }
        emit_global_get(vars[var].global_idx);
        emit_call(IMP_FREE);
        emit_i32_const(0);
        emit_global_set(vars[var].global_idx);
    } while (want(TOK_COMMA));
}

static void compile_local(void) {
    if (cur_func == 0) { error_at("LOCAL outside SUB"); return; }
    /* The SUB's exit code restores every LOCAL unconditionally, so the save
     * must run unconditionally too. Inside an IF or a loop that was not
     * entered, the global was "restored" from a save that never happened
     * (i.e. zeroed). */
    if (!ctrl_top_is(-1)) { error_at("LOCAL must be at the top level of a SUB, not inside a block"); return; }
    int sub_var = func_bufs[cur_func].sub_var;
    do {
        need(TOK_NAME);
        int var = tokv;
        if (vars[sub_var].local_count >= 8) { error_at("too many LOCAL variables (max 8)"); return; }
        bw_var_lock_type(var);          /* an untyped LOCAL is an integer */
        VType vt = vars[var].type;
        /* The saved outer value lives in whatever local we get here, and the
         * epilogue must restore from THAT one. It used to restore from the
         * fixed index np + np + i, which is only right when every LOCAL comes
         * before anything else in the SUB that allocates a local (a FOR, an
         * array access, a string assignment...) -- otherwise the global was
         * "restored" from an unrelated local. */
        int saved = alloc_local_keep(vt);
        vars[sub_var].local_save_idx[vars[sub_var].local_count] = saved;
        vars[sub_var].local_vars[vars[sub_var].local_count++] = var;
        emit_global_get(vars[var].global_idx);
        emit_local_set(saved);
        if (vars[var].type == T_STR) {
            emit_i32_const(0);
            emit_global_set(vars[var].global_idx);
        }
    } while (want(TOK_COMMA));
}

static void compile_return(void) {
    if (cur_func == 0) {
        emit_return();
        return;
    }
    int sub_var = func_bufs[cur_func].sub_var;
    int np = vars[sub_var].param_count;

    int sub_is_float = (vars[sub_var].type_set && vars[sub_var].type == T_F32);
    int sub_is_i64 = (vars[sub_var].type_set && vars[sub_var].type == T_I64);
    int sub_is_str = (vars[sub_var].type_set && vars[sub_var].type == T_STR);

    if (!want(TOK_EOF)) {
        ungot = 1;
        expr();
        if (sub_is_str) {
            /* The caller receives an OWNED string. A value borrowed from a
             * variable is copied first: returning a LOCAL's or parameter's
             * pointer handed back memory the epilogue below frees. */
            if (vsp > 0 && vstack[vsp-1] != T_STR) { error_at("string FUNCTION must return a string"); return; }
            bw_str_own();
        }
        else if (sub_is_float) coerce_f32();
        else if (sub_is_i64) coerce_i64();
        else coerce_i32();
        vpop();
        int ret_local = sub_is_float ? alloc_local_f32() : (sub_is_i64 ? alloc_local_i64() : alloc_local());
        emit_local_set(ret_local);

        for (int i = 0; i < np; i++) {
            int pvar = vars[sub_var].param_vars[i];
            if (vars[pvar].type == T_STR) {
                emit_global_get(vars[pvar].global_idx);
                emit_call(IMP_STR_FREE);
            }
            emit_local_get(np + i);
            emit_global_set(vars[pvar].global_idx);
        }
        for (int i = 0; i < vars[sub_var].local_count; i++) {
            int lvar = vars[sub_var].local_vars[i];
            if (vars[lvar].type == T_STR) {
                emit_global_get(vars[lvar].global_idx);
                emit_call(IMP_STR_FREE);
            }
            emit_local_get(vars[sub_var].local_save_idx[i]);
            emit_global_set(vars[lvar].global_idx);
        }

        emit_local_get(ret_local);
        emit_return();
    } else {
        for (int i = 0; i < np; i++) {
            int pvar = vars[sub_var].param_vars[i];
            if (vars[pvar].type == T_STR) {
                emit_global_get(vars[pvar].global_idx);
                emit_call(IMP_STR_FREE);
            }
            emit_local_get(np + i);
            emit_global_set(vars[pvar].global_idx);
        }
        for (int i = 0; i < vars[sub_var].local_count; i++) {
            int lvar = vars[sub_var].local_vars[i];
            if (vars[lvar].type == T_STR) {
                emit_global_get(vars[lvar].global_idx);
                emit_call(IMP_STR_FREE);
            }
            emit_local_get(vars[sub_var].local_save_idx[i]);
            emit_global_set(vars[lvar].global_idx);
        }
        if (sub_is_float) emit_f32_const(0.0f);
        else if (sub_is_i64) emit_i64_const(0);
        else emit_i32_const(0);
        emit_return();
    }
}

static void compile_select(void) {
    need(TOK_CASE);
    expr();
    /* A string selector the expression created is kept until END SELECT and
     * freed there; one borrowed from a variable is left alone. */
    int test_is_temp = (vsp > 0 && vstack[vsp-1] == T_STR && vown[vsp-1] == STR_TEMP);
    VType test_type = vpop();
    int test_local = alloc_local_keep(test_type == T_STR ? T_I32 : test_type);  /* live until END SELECT */
    emit_local_set(test_local);

    emit_block();

    if (ctrl_sp >= MAX_CTRL) { error_at("too many nested blocks"); return; }
    ctrl_stk[ctrl_sp].kind = CTRL_SELECT;
    ctrl_stk[ctrl_sp].for_var = test_local;
    ctrl_stk[ctrl_sp].for_limit_local = (int)test_type;
    ctrl_stk[ctrl_sp].break_depth = block_depth;
    ctrl_stk[ctrl_sp].if_extra_ends = 0;
    ctrl_stk[ctrl_sp].for_has_step = test_is_temp;  /* reused: free selector at END SELECT */
    ctrl_stk[ctrl_sp].for_step_local = 0;           /* reused: 1 once CASE ELSE has been seen */
    ctrl_sp++;
}

static void compile_case(void) {
    int si = -1;
    for (int i = ctrl_sp - 1; i >= 0; i--) {
        if (ctrl_stk[i].kind == CTRL_SELECT) { si = i; break; }
    }
    if (si < 0) { error_at("CASE without SELECT"); return; }
    /* CASE ELSE must be the last branch: a CASE after it used to fall through,
     * running both bodies. */
    if (ctrl_stk[si].for_step_local == 1) { error_at("CASE after CASE ELSE"); return; }

    int test_local = ctrl_stk[si].for_var;
    VType test_type = (VType)ctrl_stk[si].for_limit_local;

    if (ctrl_stk[si].if_extra_ends > 0) {
        emit_br(block_depth - ctrl_stk[si].break_depth);
        emit_end();
        ctrl_stk[si].if_extra_ends--;
    }

    if (want(TOK_ELSE)) {
        ctrl_stk[si].for_step_local = 1;
        return;
    }

    int nmatches = 0;
    do {
        if (nmatches > 0) {
        }

        if (want(TOK_IS)) {
            int op = read_tok();
            if (op < TOK_EQ || op > TOK_GE) {
                error_at("expected comparison operator after IS");
                return;
            }
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
                expr();
                int ch = bw_str_hold();
                vpop();
                emit_call(IMP_STR_CMP);
                bw_str_release(ch);
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
                if (test_type == T_I64) {
                    expr(); coerce_i64(); vpop();
                    switch (op) {
                    case TOK_EQ: emit_op(OP_I64_EQ); break;
                    case TOK_NE: emit_op(OP_I64_NE); break;
                    case TOK_LT: emit_op(OP_I64_LT_S); break;
                    case TOK_GT: emit_op(OP_I64_GT_S); break;
                    case TOK_LE: emit_op(OP_I64_LE_S); break;
                    case TOK_GE: emit_op(OP_I64_GE_S); break;
                    }
                } else {
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
            }
        } else {
            if (test_type == T_F32) {
                emit_local_get(test_local);
                expr(); coerce_f32(); vpop();
                emit_op(OP_F32_EQ);
            } else if (test_type == T_I64) {
                emit_local_get(test_local);
                expr(); coerce_i64(); vpop();
                emit_op(OP_I64_EQ);
            } else if (test_type == T_STR) {
                emit_local_get(test_local);
                expr();
                int ch = bw_str_hold();
                vpop();
                emit_call(IMP_STR_CMP);
                bw_str_release(ch);
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

    int do_variant = 0;

    if (want(TOK_WHILE)) {
        expr(); coerce_i32(); vpop();
        emit_op(OP_I32_EQZ);
        emit_br_if(1);
        do_variant = 1;
    } else if (want(TOK_UNTIL)) {
        expr(); coerce_i32(); vpop();
        emit_br_if(1);
        do_variant = 2;
    }

    if (ctrl_sp >= MAX_CTRL) { error_at("too many nested blocks"); return; }
    ctrl_stk[ctrl_sp].kind = CTRL_DO;
    ctrl_stk[ctrl_sp].break_depth = block_depth - 1;
    ctrl_stk[ctrl_sp].cont_depth = block_depth;
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
        emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
    } else {
        if (want(TOK_WHILE)) {
            expr(); coerce_i32(); vpop();
            emit_op(OP_I32_EQZ);
            emit_br_if(block_depth - ctrl_stk[ctrl_sp].break_depth);
            emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
        } else if (want(TOK_UNTIL)) {
            expr(); coerce_i32(); vpop();
            emit_br_if(block_depth - ctrl_stk[ctrl_sp].break_depth);
            emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
        } else {
            emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
        }
    }

    emit_end();
    emit_end();
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

    /* Two arrays swap their pointers. An array's global is an i32 pointer
     * whatever its element type; using the element type for the temporary gave
     * an invalid module for `SWAP A#, B#`. */
    if (vars[var_a].mode == VAR_DIM || vars[var_b].mode == VAR_DIM) {
        if (vars[var_a].mode != VAR_DIM || vars[var_b].mode != VAR_DIM ||
            vars[var_a].type != vars[var_b].type ||
            vars[var_a].dim_count != vars[var_b].dim_count) {
            error_at("SWAP of arrays needs two arrays of the same type and number of dimensions");
            return;
        }
        int tmp = alloc_local();
        emit_global_get(vars[var_a].global_idx);
        emit_local_set(tmp);
        emit_global_get(vars[var_b].global_idx);
        emit_global_set(vars[var_a].global_idx);
        emit_local_get(tmp);
        emit_global_set(vars[var_b].global_idx);
        return;
    }
    bw_var_lock_type(var_a);
    bw_var_lock_type(var_b);
    VType ta = vars[var_a].type;
    VType tb = vars[var_b].type;
    if (ta != tb) { error_at("SWAP requires both variables to be the same type"); return; }

    if (ta == T_F32) {
        int tmp = alloc_local_f32();
        emit_global_get(vars[var_a].global_idx);
        emit_local_set(tmp);
        emit_global_get(vars[var_b].global_idx);
        emit_global_set(vars[var_a].global_idx);
        emit_local_get(tmp);
        emit_global_set(vars[var_b].global_idx);
    } else if (ta == T_I64) {
        int tmp = alloc_local_i64();
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
    do {
        if (ndata_items >= MAX_DATA_ITEMS) { error_at("too many DATA items"); return; }
        DataItem item;
        memset(&item, 0, sizeof(item));
        int neg = 0;
        if (want(TOK_SUB)) neg = 1;
        if (want(TOK_NUMBER)) {
            item.type = T_I32;
            if (tok_num_is_i64) {
                int64_t v = neg ? -tokq : tokq;
                item.ival = (int32_t)v;
            } else {
                item.ival = neg ? -tokv : tokv;
            }
        } else if (want(TOK_FLOAT)) {
            item.type = T_F32;
            item.fval = neg ? -tokf : tokf;
        } else if (!neg && want(TOK_STRING)) {
            item.type = T_STR;
            item.str_off = tokv;
        } else {
            error_at("expected number or string in DATA");
            return;
        }
#ifdef BAS2WASM_USE_PSRAM
        bw_psram_write(data_items + ndata_items * sizeof(DataItem),
                        &item, sizeof(DataItem));
#else
        data_items[ndata_items] = item;
#endif
        ndata_items++;
    } while (want(TOK_COMMA));
}

static void compile_read(void) {
    do {
        need(TOK_NAME);
        int var = tokv;

        /* Out of DATA: stop the script with a trap. Unchecked, READ went on
         * reading whatever followed the table in memory. */
        emit_global_get(GLOBAL_DATA_IDX);
        emit_global_get(GLOBAL_DATA_BASE);
        emit_i32_load(0);                   /* item count */
        emit_op(OP_I32_GE_U);
        emit_if_void();
        emit_op(OP_UNREACHABLE);
        emit_end();

        emit_global_get(GLOBAL_DATA_BASE);
        emit_i32_const(4);
        emit_op(OP_I32_ADD);
        emit_global_get(GLOBAL_DATA_IDX);
        emit_i32_const(8);
        emit_op(OP_I32_MUL);
        emit_op(OP_I32_ADD);
        int addr = alloc_local();
        emit_local_set(addr);

        /* Type tag 2 = string item. A string variable must get a string item
         * and a numeric variable a numeric one; the mismatch used to treat a
         * number as a string pointer (or a pointer as a number). */
        emit_local_get(addr);
        emit_i32_load(0);
        emit_i32_const(2);
        emit_op(vars[var].type == T_STR ? OP_I32_NE : OP_I32_EQ);
        emit_if_void();
        emit_op(OP_UNREACHABLE);
        emit_end();

        if (vars[var].type == T_STR) {
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
            int tag = alloc_local();
            emit_local_get(addr);
            emit_i32_load(0);
            emit_local_set(tag);
            emit_local_get(tag);
            emit_i32_const(1);
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
        } else if (vars[var].type_set && vars[var].type == T_I64) {
            int tag = alloc_local();
            emit_local_get(addr);
            emit_i32_load(0);
            emit_local_set(tag);
            emit_local_get(tag);
            emit_i32_const(1);
            emit_op(OP_I32_EQ);
            emit_if_void();
                emit_local_get(addr);
                emit_f32_load(4);
                emit_op(OP_I64_TRUNC_F32_S);
                emit_global_set(vars[var].global_idx);
            emit_else();
                emit_local_get(addr);
                emit_i32_load(4);
                emit_op(OP_I64_EXTEND_I32_S);
                emit_global_set(vars[var].global_idx);
            emit_end();
        } else {
            if (!vars[var].type_set) {
                vars[var].type = T_I32;
                vars[var].type_set = 1;
            }
            int tag = alloc_local();
            emit_local_get(addr);
            emit_i32_load(0);
            emit_local_set(tag);
            emit_local_get(tag);
            emit_i32_const(1);
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
    expr();
    if (vsp > 0 && vstack[vsp-1] != T_STR) { error_at("MID$ replacement must be a string"); return; }
    int repl_is_temp = (vsp > 0 && vown[vsp-1] == STR_TEMP);
    vpop();
    int repl_local = alloc_local();
    emit_local_set(repl_local);

    emit_global_get(vars[target].global_idx);
    emit_local_get(start_local);
    emit_local_get(len_local);
    emit_local_get(repl_local);
    emit_call(IMP_STR_MID_ASSIGN);
    if (repl_is_temp) bw_str_release(repl_local);

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

static void compile_open(void) {
    expr();
    int fname_hold = bw_str_hold();
    VType ft = vpop();
    if (ft != T_STR) { error_at("OPEN filename must be a string"); return; }

    need(TOK_FOR);

    read_tok();
    int mode = -1;
    if (tok == TOK_NAME) {
        if (strcmp(vars[tokv].name, "INPUT") == 0) mode = 0;
        else if (strcmp(vars[tokv].name, "OUTPUT") == 0) mode = 1;
        else if (strcmp(vars[tokv].name, "APPEND") == 0) mode = 2;
    }
    if (mode < 0) { error_at("expected INPUT, OUTPUT, or APPEND"); return; }

    need(TOK_AS);

    need(TOK_HASH);
    need(TOK_NUMBER);
    int ch = tokv;
    if (ch < 1 || ch > 4) { error_at("channel must be 1-4"); return; }

    emit_i32_const(mode);
    emit_call(IMP_FILE_OPEN);
    bw_str_release(fname_hold);

    int tmp = alloc_local();
    emit_local_set(tmp);
    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_local_get(tmp);
    emit_i32_store(0);
}

static void compile_close_file(void) {
    need(TOK_HASH);
    need(TOK_NUMBER);
    int ch = tokv;
    if (ch < 1 || ch > 4) { error_at("channel must be 1-4"); return; }

    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_i32_load(0);
    emit_call(IMP_FILE_CLOSE);

    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_i32_const(-1);
    emit_i32_store(0);
}

static void compile_kill(void) {
    expr();
    int h = bw_str_hold();
    VType t = vpop();
    if (t != T_STR) { error_at("KILL requires a string path"); return; }
    emit_call(IMP_FILE_DELETE);
    emit_drop();
    bw_str_release(h);
}

static void compile_name_stmt(void) {
    expr();
    int old_temp = (vsp > 0 && vstack[vsp-1] == T_STR && vown[vsp-1] == STR_TEMP);
    VType t1 = vpop();
    if (t1 != T_STR) { error_at("NAME requires a string path"); return; }
    int old_ptr = alloc_local();
    emit_local_set(old_ptr);

    need(TOK_AS);

    expr();
    int new_temp = (vsp > 0 && vstack[vsp-1] == T_STR && vown[vsp-1] == STR_TEMP);
    VType t2 = vpop();
    if (t2 != T_STR) { error_at("NAME requires a string path"); return; }
    int new_ptr = alloc_local();
    emit_local_set(new_ptr);

    emit_local_get(old_ptr);
    emit_local_get(new_ptr);
    emit_call(IMP_FILE_RENAME);
    emit_drop();
    if (old_temp) bw_str_release(old_ptr);
    if (new_temp) bw_str_release(new_ptr);
}

static void compile_mkdir(void) {
    expr();
    int h = bw_str_hold();
    VType t = vpop();
    if (t != T_STR) { error_at("MKDIR requires a string path"); return; }
    emit_call(IMP_FILE_MKDIR);
    emit_drop();
    bw_str_release(h);
}

static void compile_rmdir(void) {
    expr();
    int h = bw_str_hold();
    VType t = vpop();
    if (t != T_STR) { error_at("RMDIR requires a string path"); return; }
    emit_call(IMP_FILE_RMDIR);
    emit_drop();
    bw_str_release(h);
}

static void compile_print_file(void) {
    need(TOK_NUMBER);
    int ch = tokv;
    if (ch < 1 || ch > 4) { error_at("channel must be 1-4"); return; }
    need(TOK_COMMA);

    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_i32_load(0);
    int handle = alloc_local();
    emit_local_set(handle);

    expr();
    /* The text is a temporary if the expression made it, or if a number was
     * rendered to a string here; free it once written. */
    int str_is_temp = (vsp > 0 && (vstack[vsp-1] != T_STR || vown[vsp-1] == STR_TEMP));
    VType t = vpop();

    if (t == T_I32) {
        emit_call(IMP_STR_FROM_INT);
    } else if (t == T_I64) {
        emit_call(IMP_STR_FROM_I64);
    } else if (t == T_F32) {
        emit_call(IMP_STR_FROM_FLOAT);
    }

    int str = alloc_local();
    emit_local_set(str);

    emit_local_get(handle);
    emit_local_get(str);
    emit_call(IMP_FILE_PRINT);
    emit_drop();
    if (str_is_temp) bw_str_release(str);
}

static void compile_input_file(void) {
    need(TOK_NUMBER);
    int ch = tokv;
    if (ch < 1 || ch > 4) { error_at("channel must be 1-4"); return; }
    need(TOK_COMMA);

    need(TOK_NAME);
    int var = tokv;

    emit_i32_const(FILE_TABLE_BASE + (ch - 1) * 4);
    emit_i32_load(0);
    emit_call(IMP_FILE_READLN);

    if (vars[var].type == T_STR) {
        int new_val = alloc_local();
        emit_local_set(new_val);
        emit_global_get(vars[var].global_idx);
        emit_call(IMP_STR_FREE);
        emit_local_get(new_val);
        emit_global_set(vars[var].global_idx);
    } else {
        /* The line read from the file is a pool string: convert it, then free
         * it (it used to be dropped on the floor). */
        int line = alloc_local();
        buf_byte(CODE, OP_LOCAL_TEE); buf_uleb(CODE, line);
        if (vars[var].type == T_F32) {
            emit_call(IMP_STR_TO_FLOAT);
        } else if (vars[var].type == T_I64) {
            emit_call(IMP_STR_TO_I64);
        } else {
            emit_call(IMP_STR_TO_INT);
            bw_var_lock_type(var);
        }
        emit_global_set(vars[var].global_idx);
        bw_str_release(line);
    }
}

/* A call used as a statement: `NAME(args)` (paren = 1) or `NAME args` /
 * bare `NAME` (paren = 0). NAME must be a SUB or FUNCTION that is already
 * defined or DECLAREd.
 *
 * Both forms used to have holes. The bare form emitted the call only inside
 * "if there are arguments", so a zero-argument SUB called as plain `SHOUT` did
 * nothing at all, and ANY unknown identifier on a line by itself (a typo)
 * compiled silently to nothing. The parenthesised form never checked that NAME
 * was a function, and since func_local_idx is 0 for anything else it emitted a
 * call to function 0 -- the program's own setup -- which recursed until the
 * stack overflowed. */
static void compile_call_stmt(int var, int paren) {
    int base_vsp = vsp;
    int nargs = 0;
    int holds[8];
    int have_args = paren ? !want(TOK_RP) : !want(TOK_EOF);
    if (!paren && have_args && tok == TOK_ELSE && single_line_if > 0) {
        ungot = 1;          /* `IF c THEN SHOUT ELSE ...`: no arguments */
        have_args = 0;
    }
    if (have_args) {
        ungot = 1;
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
        if (paren) need(TOK_RP);
    }
    vsp = base_vsp;
    if (had_error) return;

    if (vars[var].mode != VAR_SUB) {
        char msg[80];
        snprintf(msg, sizeof(msg), "unknown statement or undefined SUB '%s'", vars[var].name);
        error_at(msg);
        return;
    }
    if (nargs != vars[var].param_count) {
        error_at("wrong number of arguments");
        return;
    }
    emit_call(IMP_COUNT + vars[var].func_local_idx);
    for (int i = 0; i < nargs && i < 8; i++) bw_str_release(holds[i]);
    /* Every SUB/FUNCTION returns a value; discard it (a string FUNCTION's
     * result is an owned string, so free it). */
    if (vars[var].type_set && vars[var].type == T_STR) emit_call(IMP_STR_FREE);
    else emit_drop();
}

static void stmt_body(void);

/* One statement. Scratch locals allocated while compiling it are recycled when
 * it ends (see alloc_local in bas2wasm.h); nested statements (single-line IF)
 * release only their own. */
void stmt(void) {
    int tmp_mark = bw_ntmp;
    stmt_body();
    bw_tmp_release(tmp_mark);
}

static void stmt_body(void) {
    int t = read_tok();
    if (had_error) return;

    if (t != TOK_EOF) {
        emit_i32_const(line_num);
        emit_global_set(GLOBAL_LINE);
    }

    switch (t) {
    case TOK_EOF: break;
    case TOK_FORMAT:  compile_format(); break;
    case TOK_PRINTS:  compile_prints(); break;
    case TOK_FUNCTION:
    case TOK_KW_SUB:  compile_sub(); break;
    case TOK_DECLARE: compile_declare(); break;
    case TOK_END:     compile_end(); break;
    case TOK_RETURN:  compile_return(); break;
    case TOK_LOCAL:   compile_local(); break;
    case TOK_WHILE:   compile_while(); break;
    case TOK_FOR:     compile_for(); break;
    case TOK_IF:      compile_if(); break;
    case TOK_ELSE:    compile_else(); break;
    case TOK_ELSEIF:  compile_else_branch(1); break;
    case TOK_DIM:     compile_dim(); break;
    case TOK_REDIM:   compile_redim(); break;
    case TOK_ERASE:   compile_erase(); break;
    case TOK_OPTION:  compile_option(); break;
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
    case TOK_BYE:
    case TOK_BREAK:
        /* These end the program by returning from the main code. Inside a SUB
         * a bare `return` does not match the function's result type (an
         * invalid module, with no compile error), and it could only leave the
         * SUB anyway. */
        if (cur_func != 0) { error_at("BYE/BREAK cannot be used inside a SUB or FUNCTION; use RETURN"); break; }
        emit_return();
        break;
    case TOK_RESUME:  error_at("RESUME not supported in compiled code"); break;
    case TOK_OPEN:       compile_open(); break;
    case TOK_CLOSE_FILE: compile_close_file(); break;
    case TOK_KILL:       compile_kill(); break;
    case TOK_MKDIR:      compile_mkdir(); break;
    case TOK_RMDIR:      compile_rmdir(); break;
    case TOK_GT: {
        expr();
        int is_temp = (vsp > 0 && vstack[vsp-1] == T_STR && vown[vsp-1] == STR_TEMP);
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
            if (is_temp) bw_str_release(tmp);   /* printed: free the temporary */
        } else if (t2 == T_F32) {
            emit_call(IMP_PRINT_F32);
        } else if (t2 == T_I64) {
            emit_call(IMP_PRINT_I64);
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
            if (vars[var].is_const) { error_at("cannot assign to CONST"); break; }
            expr();
            VType et = (vsp > 0) ? vstack[vsp-1] : T_I32;
            if (vars[var].type == T_STR) {
                if (et != T_STR) { error_at("cannot assign a number to a string variable"); break; }
                /* The variable must own what it holds: a value borrowed from
                 * another variable (`B$ = A$`, even `A$ = A$`) is copied
                 * BEFORE the old value is freed. Storing the bare pointer made
                 * two variables own one pool block, and the next assignment to
                 * either freed it under the other. */
                bw_str_own();
                vpop();
                int new_val = alloc_local();
                emit_local_set(new_val);
                emit_global_get(vars[var].global_idx);
                emit_call(IMP_STR_FREE);
                emit_local_get(new_val);
                emit_global_set(vars[var].global_idx);
            } else {
                if (et == T_STR) { error_at("cannot assign a string to a numeric variable"); break; }
                if (!vars[var].type_set) {
                    vars[var].type = et;
                    vars[var].type_set = 1;
                } else {
                    /* Coerce while the value's type is still on the type stack.
                     * This used to vpop() first, and the coerce_* helpers do
                     * nothing on an empty stack -- so `X = 42 : X = 3.14`
                     * stored an f32 into an i32 global (an invalid module). */
                    coerce_to(vars[var].type);
                }
                vpop();
                emit_global_set(vars[var].global_idx);
            }
        } else if (want(TOK_LP)) {
            if (vars[var].mode == VAR_DIM) {
                int addr_local = -1;
                int ndims = 0;
                if (!parse_dim_indices_to_addr(var, &addr_local, &ndims)) return;
                need(TOK_EQ);
                if (vars[var].type_set && vars[var].type == T_STR) {
                    /* String array element: free old, copy new. RHS expression
                     * already returns a fresh pool allocation, but we run it
                     * through str_copy anyway to match scalar semantics. */
                    expr();
                    if (vsp > 0 && vstack[vsp-1] != T_STR) { error_at("cannot assign a number to a string array element"); return; }
                    bw_str_own();       /* copy if borrowed from a variable / element */
                    vpop();
                    int new_val = alloc_local();
                    emit_local_set(new_val);
                    emit_local_get(addr_local);
                    emit_i32_load(0);            /* old pointer */
                    emit_call(IMP_STR_FREE);
                    emit_local_get(addr_local);
                    emit_local_get(new_val);
                    emit_i32_store(0);
                } else {
                    expr(); coerce_to(vars[var].type_set ? vars[var].type : T_I32); vpop();
                    int val_local = alloc_local_for_vtype(vars[var].type_set ? vars[var].type : T_I32);
                    emit_local_set(val_local);
                    emit_local_get(addr_local);
                    emit_local_get(val_local);
                    if (vars[var].type_set && vars[var].type == T_I64) emit_i64_store(0);
                    else if (vars[var].type_set && vars[var].type == T_F32) emit_f32_store(0);
                    else emit_i32_store(0);
                }
                (void)ndims;
            } else {
                if (compile_builtin_expr(vars[var].name)) {
                    /* Builtin called for its side effect: discard the result
                     * (free it if it is a temporary string). */
                    if (vsp > 0) {
                        int is_temp = (vstack[vsp-1] == T_STR && vown[vsp-1] == STR_TEMP);
                        vpop();
                        if (is_temp) emit_call(IMP_STR_FREE); else emit_drop();
                    }
                } else {
                    compile_call_stmt(var, 1);
                }
            }
        } else {
            compile_call_stmt(var, 0);
        }
        break;
    }
    default:
        if (t) error_at("bad statement");
        break;
    }

    if (tok != TOK_EOF && !had_error) {
        read_tok();
        if (tok == TOK_ELSE && single_line_if > 0)
            ungot = 1;      /* the ELSE of the enclosing single-line IF */
        else if (tok != TOK_EOF)
            error_at("extra tokens after statement");
    }
}
