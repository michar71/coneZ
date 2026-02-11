/*
 * stmt.c — statement parser
 */
#include "bas2wasm.h"

static void compile_format(void) {
    need(TOK_STRING);
    int raw_off = tokv;
    char cfmt[512];
    int ci = 0;
    for (const char *p = data_buf + raw_off; *p; p++) {
        if (ci >= (int)sizeof(cfmt) - 3) { error_at("FORMAT string too long"); return; }
        if (*p == '%') { cfmt[ci++] = '%'; cfmt[ci++] = 'd'; }
        else if (*p == '$') { cfmt[ci++] = '%'; cfmt[ci++] = 's'; }
        else if (*p == '&') { cfmt[ci++] = '%'; cfmt[ci++] = 'f'; }
        else cfmt[ci++] = *p;
    }
    cfmt[ci++] = '\n'; cfmt[ci] = 0;
    data_len = raw_off;
    int fmt_off = add_string(cfmt, ci);

    int nargs = 0;
    while (want(TOK_COMMA)) {
        if (nargs >= FMT_BUF_SIZE / 4) { error_at("too many FORMAT arguments"); return; }
        emit_i32_const(0xF000);
        expr();
        VType t = vpop();
        if (t == T_F32) {
            buf_byte(CODE, OP_F32_STORE); buf_uleb(CODE, 2); buf_uleb(CODE, nargs * 4);
        } else {
            emit_i32_store(nargs * 4);
        }
        nargs++;
    }

    emit_i32_const(fmt_off);
    emit_i32_const(0xF000);
    emit_call(IMP_HOST_PRINTF);
    emit_drop();
}

static void compile_prints(void) {
    int before_vsp = vsp;
    expr();
    VType t = vpop();
    (void)t;
    (void)before_vsp;
    int fmt_buf = 0xF000;
    emit_i32_const(fmt_buf);
    int tmp = alloc_local();
    emit_local_set(tmp);
    int tmp2 = alloc_local();
    emit_local_set(tmp2);
    emit_local_get(tmp);
    emit_local_get(tmp2);
    emit_i32_store(0);

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
    for (int i = 0; i < np; i++)
        f->param_types[i] = (vars[params[i]].type_set && vars[params[i]].type == T_F32) ? WASM_F32 : WASM_I32;

    int prev_func = cur_func;
    int prev_depth = block_depth;
    cur_func = fi;
    block_depth = 0;

    int saved[8];
    for (int i = 0; i < np; i++)
        saved[i] = (f->param_types[i] == WASM_F32) ? alloc_local_f32() : alloc_local();

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

static void close_sub(void) {
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
        emit_local_get(np + np + i);
        emit_global_set(vars[lvar].global_idx);
    }

    if (vars[var].type_set && vars[var].type == T_F32)
        emit_f32_const(0.0f);
    else
        emit_i32_const(0);
    emit_end();

    cur_func = prev_func;
    block_depth = prev_depth;
}

static void close_while(void) {
    ctrl_sp--;
    if (ctrl_stk[ctrl_sp].kind != CTRL_WHILE) { error_at("WEND without WHILE"); return; }
    emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
    emit_end();
    emit_end();
}

static void close_for(void) {
    ctrl_sp--;
    if (ctrl_stk[ctrl_sp].kind != CTRL_FOR) { error_at("NEXT without FOR"); return; }
    int var = ctrl_stk[ctrl_sp].for_var;
    int is_float = (vars[var].type_set && vars[var].type == T_F32);
    emit_global_get(vars[var].global_idx);
    if (ctrl_stk[ctrl_sp].for_has_step) {
        emit_local_get(ctrl_stk[ctrl_sp].for_step_local);
    } else {
        if (is_float) emit_f32_const(1.0f); else emit_i32_const(1);
    }
    emit_op(is_float ? OP_F32_ADD : OP_I32_ADD);
    emit_global_set(vars[var].global_idx);
    emit_br(block_depth - ctrl_stk[ctrl_sp].cont_depth);
    emit_end();
    emit_end();
}

static void compile_end(void) {
    int kw = read_tok();
    if (kw == TOK_KW_SUB || kw == TOK_FUNCTION) {
        close_sub();
    } else if (kw == TOK_IF) {
        ctrl_sp--;
        if (ctrl_stk[ctrl_sp].kind != CTRL_IF) { error_at("END IF without IF"); return; }
        int extras = ctrl_stk[ctrl_sp].if_extra_ends;
        emit_end();
        for (int i = 0; i < extras; i++) emit_end();
    } else if (kw == TOK_SELECT) {
        ctrl_sp--;
        if (ctrl_stk[ctrl_sp].kind != CTRL_SELECT) { error_at("END SELECT without SELECT"); return; }
        int extras = ctrl_stk[ctrl_sp].if_extra_ends;
        for (int i = 0; i < extras; i++) emit_end();
        emit_end();
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
    int is_float = (vars[var].type_set && vars[var].type == T_F32);
    need(TOK_EQ);
    expr();
    if (is_float) coerce_f32(); else coerce_i32();
    vpop();
    emit_global_set(vars[var].global_idx);
    need(TOK_TO);
    expr();
    if (is_float) coerce_f32(); else coerce_i32();
    vpop();
    int limit_local = is_float ? alloc_local_f32() : alloc_local();
    emit_local_set(limit_local);

    int step_local = -1;
    int has_step = 0;
    if (want(TOK_STEP)) {
        expr();
        if (is_float) coerce_f32(); else coerce_i32();
        vpop();
        step_local = is_float ? alloc_local_f32() : alloc_local();
        emit_local_set(step_local);
        has_step = 1;
    }

    emit_block();
    emit_loop();

    if (has_step) {
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(is_float ? OP_F32_GT : OP_I32_GT_S);
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(is_float ? OP_F32_LT : OP_I32_LT_S);
        emit_local_get(step_local);
        if (is_float) emit_f32_const(0.0f); else emit_i32_const(0);
        emit_op(is_float ? OP_F32_GT : OP_I32_GT_S);
        emit_op(OP_SELECT);
        emit_br_if(1);
    } else {
        emit_global_get(vars[var].global_idx);
        emit_local_get(limit_local);
        emit_op(is_float ? OP_F32_GT : OP_I32_GT_S);
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
    if (want(TOK_THEN)) {
        emit_if_void();
        stmt();
        emit_end();
    } else {
        emit_if_void();
        if (ctrl_sp >= MAX_CTRL) { error_at("too many nested blocks"); return; }
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
    int n_local = alloc_local();
    emit_local_set(n_local);
    emit_global_get(GLOBAL_HEAP);
    emit_global_set(vars[var].global_idx);
    emit_global_get(vars[var].global_idx);
    emit_local_get(n_local);
    emit_i32_store(0);
    emit_global_get(GLOBAL_HEAP);
    emit_local_get(n_local);
    emit_i32_const(1); emit_op(OP_I32_ADD);
    emit_i32_const(4); emit_op(OP_I32_MUL);
    emit_op(OP_I32_ADD);
    emit_global_set(GLOBAL_HEAP);
}

static void compile_local(void) {
    if (cur_func == 0) { error_at("LOCAL outside SUB"); return; }
    int sub_var = func_bufs[cur_func].sub_var;
    do {
        need(TOK_NAME);
        int var = tokv;
        if (vars[sub_var].local_count >= 8) { error_at("too many LOCAL variables (max 8)"); return; }
        vars[sub_var].local_vars[vars[sub_var].local_count++] = var;
        int saved = (vars[var].type_set && vars[var].type == T_F32) ? alloc_local_f32() : alloc_local();
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
    int sub_is_str = (vars[sub_var].type_set && vars[sub_var].type == T_STR);

    if (!want(TOK_EOF)) {
        ungot = 1;
        expr();
        if (sub_is_str) { /* no coercion — string is already i32 pointer */ }
        else if (sub_is_float) coerce_f32();
        else coerce_i32();
        vpop();
        int ret_local = sub_is_float ? alloc_local_f32() : alloc_local();
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
            emit_local_get(np + np + i);
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
            emit_local_get(np + np + i);
            emit_global_set(vars[lvar].global_idx);
        }
        if (sub_is_float) emit_f32_const(0.0f); else emit_i32_const(0);
        emit_return();
    }
}

static void compile_select(void) {
    need(TOK_CASE);
    expr();
    VType test_type = vpop();
    int test_local;
    if (test_type == T_F32) {
        test_local = alloc_local_f32();
    } else {
        test_local = alloc_local();
    }
    emit_local_set(test_local);

    emit_block();

    if (ctrl_sp >= MAX_CTRL) { error_at("too many nested blocks"); return; }
    ctrl_stk[ctrl_sp].kind = CTRL_SELECT;
    ctrl_stk[ctrl_sp].for_var = test_local;
    ctrl_stk[ctrl_sp].for_limit_local = (int)test_type;
    ctrl_stk[ctrl_sp].break_depth = block_depth;
    ctrl_stk[ctrl_sp].if_extra_ends = 0;
    ctrl_sp++;
}

static void compile_case(void) {
    int si = -1;
    for (int i = ctrl_sp - 1; i >= 0; i--) {
        if (ctrl_stk[i].kind == CTRL_SELECT) { si = i; break; }
    }
    if (si < 0) { error_at("CASE without SELECT"); return; }

    int test_local = ctrl_stk[si].for_var;
    VType test_type = (VType)ctrl_stk[si].for_limit_local;

    if (ctrl_stk[si].if_extra_ends > 0) {
        emit_br(block_depth - ctrl_stk[si].break_depth);
        emit_end();
        ctrl_stk[si].if_extra_ends--;
    }

    if (want(TOK_ELSE)) {
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
    do {
        need(TOK_NAME);
        int var = tokv;

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
    expr(); vpop();
    int repl_local = alloc_local();
    emit_local_set(repl_local);

    emit_global_get(vars[target].global_idx);
    emit_local_get(start_local);
    emit_local_get(len_local);
    emit_local_get(repl_local);
    emit_call(IMP_STR_MID_ASSIGN);

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

static void emit_str_ptr_len(void) {
    int tmp = alloc_local();
    emit_local_set(tmp);
    emit_local_get(tmp);
    emit_local_get(tmp);
    emit_call(IMP_STR_LEN);
}

static void compile_kill(void) {
    expr();
    VType t = vpop();
    if (t != T_STR) { error_at("KILL requires a string path"); return; }
    emit_str_ptr_len();
    emit_call(IMP_FILE_DELETE);
    emit_drop();
}

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

    emit_local_get(old_ptr);
    emit_local_get(old_ptr);
    emit_call(IMP_STR_LEN);
    emit_local_get(new_ptr);
    emit_local_get(new_ptr);
    emit_call(IMP_STR_LEN);
    emit_call(IMP_FILE_RENAME);
    emit_drop();
}

static void compile_mkdir(void) {
    expr();
    VType t = vpop();
    if (t != T_STR) { error_at("MKDIR requires a string path"); return; }
    emit_str_ptr_len();
    emit_call(IMP_FILE_MKDIR);
    emit_drop();
}

static void compile_rmdir(void) {
    expr();
    VType t = vpop();
    if (t != T_STR) { error_at("RMDIR requires a string path"); return; }
    emit_str_ptr_len();
    emit_call(IMP_FILE_RMDIR);
    emit_drop();
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
    VType t = vpop();

    if (t == T_I32) {
        emit_call(IMP_STR_FROM_INT);
    } else if (t == T_F32) {
        emit_call(IMP_STR_FROM_FLOAT);
    }

    int str = alloc_local();
    emit_local_set(str);

    emit_local_get(handle);
    emit_local_get(str);
    emit_call(IMP_FILE_PRINT);
    emit_drop();
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
    } else if (vars[var].type == T_F32) {
        emit_call(IMP_STR_TO_FLOAT);
        if (!vars[var].type_set) { vars[var].type = T_F32; vars[var].type_set = 1; }
        emit_global_set(vars[var].global_idx);
    } else {
        emit_call(IMP_STR_TO_INT);
        if (!vars[var].type_set) { vars[var].type = T_I32; vars[var].type_set = 1; }
        emit_global_set(vars[var].global_idx);
    }
}

void stmt(void) {
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
        want(TOK_THEN);
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
            if (vars[var].is_const) { error_at("cannot assign to CONST"); break; }
            expr();
            VType et = vpop();
            if (vars[var].type == T_STR) {
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
                expr(); coerce_i32(); vpop();
                need(TOK_RP);
                need(TOK_EQ);
                int idx_local = alloc_local();
                emit_local_set(idx_local);
                emit_global_get(vars[var].global_idx);
                emit_local_get(idx_local);
                emit_i32_const(4); emit_op(OP_I32_MUL);
                emit_op(OP_I32_ADD);
                expr(); coerce_i32(); vpop();
                emit_i32_store(0);
            } else {
                if (!compile_builtin_expr(vars[var].name)) {
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
                    emit_call(IMP_COUNT + vars[var].func_local_idx);
                    vpush(vars[var].type_set ? vars[var].type : T_I32);
                }
                if (vsp > 0) { vpop(); emit_drop(); }
            }
        } else {
            if (!want(TOK_EOF)) {
                ungot = 1;
                int nargs = 0;
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

    if (tok != TOK_EOF && !had_error) {
        read_tok();
        if (tok != TOK_EOF) error_at("extra tokens after statement");
    }
}
