/*
 * stmt.c — statement and declaration parser for c2wasm
 */
#include "c2wasm.h"

static void parse_local_decl(CType base_type);
static void parse_func_def(CType ret_type, const char *name, int is_static);

/* Pop symbols down to given scope level */
static void pop_scope(int target_scope) {
    while (nsym > 0 && syms[nsym - 1].scope > target_scope)
        nsym--;
}

/* ---- Constant integer expression evaluator (for case labels) ---- */

static int cexpr_prec(int min_prec);

static int cexpr_get_prec(int t) {
    switch (t) {
    case TOK_OR_OR:    return 1;
    case TOK_AND_AND:  return 2;
    case TOK_PIPE:     return 3;
    case TOK_CARET:    return 4;
    case TOK_AMP:      return 5;
    case TOK_EQ: case TOK_NE: return 6;
    case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE: return 7;
    case TOK_LSHIFT: case TOK_RSHIFT: return 8;
    case TOK_PLUS: case TOK_MINUS: return 9;
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 10;
    default: return -1;
    }
}

static int cexpr_apply(int op, int l, int r) {
    switch (op) {
    case TOK_OR_OR:  return l || r;
    case TOK_AND_AND: return l && r;
    case TOK_PIPE:   return l | r;
    case TOK_CARET:  return l ^ r;
    case TOK_AMP:    return l & r;
    case TOK_EQ:     return l == r;
    case TOK_NE:     return l != r;
    case TOK_LT:     return l < r;
    case TOK_GT:     return l > r;
    case TOK_LE:     return l <= r;
    case TOK_GE:     return l >= r;
    case TOK_LSHIFT: return l << r;
    case TOK_RSHIFT: return l >> r;
    case TOK_PLUS:   return l + r;
    case TOK_MINUS:  return l - r;
    case TOK_STAR:   return l * r;
    case TOK_SLASH:  return r ? l / r : 0;
    case TOK_PERCENT: return r ? l % r : 0;
    default: return 0;
    }
}

static int cexpr_primary(void) {
    if (tok == TOK_INT_LIT) { int v = tok_ival; next_token(); return v; }
    if (tok == TOK_CHAR_LIT) { int v = tok_ival; next_token(); return v; }
    if (tok == TOK_LPAREN) {
        next_token();
        int v = cexpr_prec(1);
        expect(TOK_RPAREN);
        return v;
    }
    error_at("expected integer constant in case label");
    next_token();
    return 0;
}

static int cexpr_unary(void) {
    if (tok == TOK_MINUS) { next_token(); return -cexpr_unary(); }
    if (tok == TOK_TILDE) { next_token(); return ~cexpr_unary(); }
    if (tok == TOK_BANG)  { next_token(); return !cexpr_unary(); }
    if (tok == TOK_PLUS)  { next_token(); return cexpr_unary(); }
    return cexpr_primary();
}

static int cexpr_prec(int min_prec) {
    int left = cexpr_unary();
    while (cexpr_get_prec(tok) >= min_prec) {
        int op = tok;
        int prec = cexpr_get_prec(op);
        next_token();
        int right = cexpr_prec(prec + 1);
        left = cexpr_apply(op, left, right);
    }
    return left;
}

static int parse_case_value(void) {
    return cexpr_prec(1);
}

/* ---- Statement parser ---- */

void parse_stmt(void) {
    if (tok == TOK_LBRACE) {
        parse_block();
        return;
    }

    if (tok == TOK_IF) {
        next_token();
        expect(TOK_LPAREN);
        CType ct = expr();
        emit_coerce(ct, CT_INT);
        expect(TOK_RPAREN);
        emit_if_void();
        if (ctrl_sp >= MAX_CTRL) { error_at("control stack overflow"); return; }
        ctrl_stk[ctrl_sp].kind = CTRL_IF;
        ctrl_stk[ctrl_sp].break_depth = -1;
        ctrl_stk[ctrl_sp].cont_depth = -1;
        ctrl_stk[ctrl_sp].incr_buf = NULL;
        ctrl_sp++;
        parse_stmt();
        ctrl_sp--;
        if (tok == TOK_ELSE) {
            next_token();
            emit_else();
            parse_stmt();
        }
        emit_end();
        return;
    }

    if (tok == TOK_WHILE) {
        next_token();
        /* block { loop { ... if !cond br 1 ... body ... br 0 } } */
        emit_block();   /* break target (depth 0 from inside = 1 from loop) */
        emit_loop();    /* continue target (depth 0 from inside = 0) */
        if (ctrl_sp >= MAX_CTRL) { error_at("control stack overflow"); return; }
        ctrl_stk[ctrl_sp].kind = CTRL_WHILE;
        ctrl_stk[ctrl_sp].break_depth = block_depth - 2;  /* outer block */
        ctrl_stk[ctrl_sp].cont_depth = block_depth - 1;   /* loop header */
        ctrl_stk[ctrl_sp].incr_buf = NULL;
        ctrl_sp++;

        expect(TOK_LPAREN);
        CType ct = expr();
        emit_coerce(ct, CT_INT);
        expect(TOK_RPAREN);
        emit_op(OP_I32_EQZ);
        emit_br_if(1);  /* if !cond, break out of block */

        parse_stmt();

        emit_br(0);     /* continue loop */
        ctrl_sp--;
        emit_end();     /* end loop */
        emit_end();     /* end block */
        return;
    }

    if (tok == TOK_DO) {
        next_token();
        emit_block();   /* break target */
        emit_loop();    /* continue target */
        if (ctrl_sp >= MAX_CTRL) { error_at("control stack overflow"); return; }
        ctrl_stk[ctrl_sp].kind = CTRL_DO;
        ctrl_stk[ctrl_sp].break_depth = block_depth - 2;
        ctrl_stk[ctrl_sp].cont_depth = block_depth - 1;
        ctrl_stk[ctrl_sp].incr_buf = NULL;
        ctrl_sp++;

        parse_stmt();

        ctrl_sp--;
        /* while (cond) */
        expect(TOK_WHILE);
        expect(TOK_LPAREN);
        CType ct = expr();
        emit_coerce(ct, CT_INT);
        expect(TOK_RPAREN);
        expect(TOK_SEMI);
        emit_br_if(0);  /* if cond, continue loop */
        emit_end();     /* end loop */
        emit_end();     /* end block */
        return;
    }

    if (tok == TOK_FOR) {
        next_token();
        expect(TOK_LPAREN);

        /* Push scope for init declaration */
        cur_scope++;

        /* Init */
        if (tok != TOK_SEMI) {
            if (is_type_keyword(tok)) {
                CType init_type = parse_type_spec();
                parse_local_decl(init_type);
                /* parse_local_decl already consumed the semicolon */
            } else {
                CType ct = expr();
                if (ct != CT_VOID) emit_drop();
                expect(TOK_SEMI);
            }
        } else {
            next_token(); /* skip ; */
        }

        /* for loop structure:
         *   block {                   ;; break target
         *     loop {                  ;; loop-back target
         *       cond; br_if_not 1;
         *       block {               ;; continue target — break out → falls to incr
         *         body
         *       }
         *       incr;
         *       br 0;                 ;; back to loop
         *     }
         *   }
         */
        emit_block();   /* outer: break target */
        emit_loop();    /* loop: back-edge target */

        /* Condition */
        if (tok != TOK_SEMI) {
            CType ct = expr();
            emit_coerce(ct, CT_INT);
            emit_op(OP_I32_EQZ);
            emit_br_if(1);  /* break if !cond */
        }
        expect(TOK_SEMI);

        /* Parse increment into a temp buffer */
        Buf incr_buf;
        buf_init(&incr_buf);
        FuncCtx *for_f = &func_bufs[cur_func];
        int incr_fixups_start = for_f->ncall_fixups;
        if (tok != TOK_RPAREN) {
            /* Temporarily redirect code emission to incr_buf */
            Buf save = for_f->code;
            for_f->code = incr_buf;
            CType ct = expr();
            if (ct != CT_VOID) emit_drop();
            incr_buf = for_f->code;
            for_f->code = save;
        }
        int incr_fixups_end = for_f->ncall_fixups;
        expect(TOK_RPAREN);

        /* Inner block: continue breaks out of this, falls through to increment */
        emit_block();

        /* Control stack entry — continue targets the inner block */
        if (ctrl_sp >= MAX_CTRL) { error_at("control stack overflow"); return; }
        ctrl_stk[ctrl_sp].kind = CTRL_FOR;
        ctrl_stk[ctrl_sp].break_depth = block_depth - 3;  /* outer block */
        ctrl_stk[ctrl_sp].cont_depth = block_depth - 1;   /* inner block */
        ctrl_stk[ctrl_sp].incr_buf = NULL;
        ctrl_sp++;

        /* Body */
        parse_stmt();

        ctrl_sp--;

        emit_end();     /* end inner block (continue lands here) */

        /* Splice increment (runs after body or after continue) */
        if (incr_buf.len > 0) {
            int splice_off = for_f->code.len;
            buf_bytes(CODE, incr_buf.data, incr_buf.len);
            /* Only adjust fixups from the increment, not from the body */
            for (int fx = incr_fixups_start; fx < incr_fixups_end; fx++)
                for_f->call_fixups[fx] += splice_off;
        }
        buf_free(&incr_buf);

        emit_br(0);     /* back to loop */
        emit_end();     /* end loop */
        emit_end();     /* end outer block */

        /* Pop scope */
        pop_scope(cur_scope - 1);
        cur_scope--;
        return;
    }

    if (tok == TOK_SWITCH) {
        next_token();
        expect(TOK_LPAREN);
        CType ct = expr();
        emit_coerce(ct, CT_INT);
        expect(TOK_RPAREN);

        /* Store switch value in a temp local */
        int switch_local = alloc_local(WASM_I32);
        emit_local_set(switch_local);

        /* Fall-through tracking: matched=1 once any case body is entered */
        int matched_local = alloc_local(WASM_I32);
        emit_i32_const(0);
        emit_local_set(matched_local);

        expect(TOK_LBRACE);

        /* Pre-scan to collect case values (needed for default-anywhere) */
        int case_vals[256];
        int ncase_vals = 0;
        int has_default = 0;
        int all_cases_resolved = 1;
        {
            LexerSave lsave;
            lexer_save(&lsave);
            int depth = 1; /* already consumed opening brace */
            while (depth > 0 && tok != TOK_EOF) {
                if (tok == TOK_LBRACE) depth++;
                else if (tok == TOK_RBRACE) { depth--; if (depth <= 0) break; }
                else if (depth == 1 && tok == TOK_DEFAULT) has_default = 1;
                else if (depth == 1 && tok == TOK_CASE) {
                    next_token(); /* skip 'case' */
                    int negate = 0;
                    if (tok == TOK_MINUS) { negate = 1; next_token(); }
                    if ((tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) && ncase_vals < 256) {
                        case_vals[ncase_vals] = negate ? -tok_ival : tok_ival;
                        next_token();
                        /* Verify simple constant (next must be ':') */
                        if (tok == TOK_COLON)
                            ncase_vals++;
                        else
                            all_cases_resolved = 0;
                        continue; /* already advanced past value */
                    } else {
                        all_cases_resolved = 0;
                    }
                }
                next_token();
            }
            lexer_restore(&lsave);
        }

        /* If default is present, pre-compute found = (val==c1)||(val==c2)||... */
        int found_local = -1;
        if (has_default) {
            found_local = alloc_local(WASM_I32);
            if (!all_cases_resolved) {
                /* Conservative: can't prove no case matches → default always enters */
                emit_i32_const(0);
            } else if (ncase_vals > 0) {
                emit_local_get(switch_local);
                emit_i32_const(case_vals[0]);
                emit_op(OP_I32_EQ);
                for (int i = 1; i < ncase_vals; i++) {
                    emit_local_get(switch_local);
                    emit_i32_const(case_vals[i]);
                    emit_op(OP_I32_EQ);
                    emit_op(OP_I32_OR);
                }
            } else {
                emit_i32_const(0);
            }
            emit_local_set(found_local);
        }

        /* Wrap in a block for break target */
        emit_block();

        if (ctrl_sp >= MAX_CTRL) { error_at("control stack overflow"); return; }
        ctrl_stk[ctrl_sp].kind = CTRL_SWITCH;
        ctrl_stk[ctrl_sp].break_depth = block_depth - 1;
        ctrl_stk[ctrl_sp].cont_depth = -1;
        ctrl_stk[ctrl_sp].incr_buf = NULL;
        ctrl_sp++;

        int in_case = 0;
        while (tok != TOK_RBRACE && tok != TOK_EOF) {
            if (tok == TOK_CASE) {
                in_case = 1;
                next_token();
                int case_val = parse_case_value();
                expect(TOK_COLON);

                /* Fall-through: if (matched || switch_val == case_val) */
                emit_local_get(matched_local);
                emit_local_get(switch_local);
                emit_i32_const(case_val);
                emit_op(OP_I32_EQ);
                emit_op(OP_I32_OR);
                emit_if_void();

                /* Mark as matched for fall-through into next case */
                emit_i32_const(1);
                emit_local_set(matched_local);

                /* Parse case body */
                while (tok != TOK_CASE && tok != TOK_DEFAULT && tok != TOK_RBRACE && tok != TOK_EOF)
                    parse_stmt();

                emit_end();
            } else if (tok == TOK_DEFAULT) {
                in_case = 1;
                next_token();
                expect(TOK_COLON);
                /* Default: if (matched || !found)
                 * - No case matched: found=0, !found=1 → enters default
                 * - Later case matched: found=1, !found=0 → skips default
                 * - Fall-through: matched=1 → enters default */
                emit_local_get(matched_local);
                emit_local_get(found_local);
                emit_op(OP_I32_EQZ);
                emit_op(OP_I32_OR);
                emit_if_void();
                emit_i32_const(1);
                emit_local_set(matched_local);
                while (tok != TOK_CASE && tok != TOK_DEFAULT && tok != TOK_RBRACE && tok != TOK_EOF)
                    parse_stmt();
                emit_end();
            } else {
                if (!in_case)
                    fprintf(stderr, "%s:%d: warning: statement before first case/default in switch\n",
                            src_file ? src_file : "<input>", line_num);
                parse_stmt();
            }
        }

        expect(TOK_RBRACE);
        ctrl_sp--;
        emit_end();  /* end break block */
        return;
    }

    if (tok == TOK_BREAK) {
        next_token();
        expect(TOK_SEMI);
        /* Find enclosing breakable */
        for (int i = ctrl_sp - 1; i >= 0; i--) {
            if (ctrl_stk[i].break_depth >= 0) {
                emit_br(block_depth - ctrl_stk[i].break_depth - 1);
                return;
            }
        }
        error_at("break outside loop/switch");
        return;
    }

    if (tok == TOK_CONTINUE) {
        next_token();
        expect(TOK_SEMI);
        for (int i = ctrl_sp - 1; i >= 0; i--) {
            if (ctrl_stk[i].cont_depth >= 0) {
                /* For FOR loops, continue breaks out of the inner block,
                 * falling through to the increment code before br 0 */
                emit_br(block_depth - ctrl_stk[i].cont_depth - 1);
                return;
            }
        }
        error_at("continue outside loop");
        return;
    }

    if (tok == TOK_RETURN) {
        next_token();
        CType ret = func_bufs[cur_func].return_type;
        if (tok != TOK_SEMI) {
            CType ct = expr();
            emit_coerce(ct, ret);
        } else if (ret != CT_VOID) {
            /* Bare return; in non-void function — push default value */
            if (ret == CT_DOUBLE) emit_f64_const(0.0);
            else if (ret == CT_FLOAT) emit_f32_const(0.0f);
            else if (ret == CT_LONG_LONG) emit_i64_const(0);
            else emit_i32_const(0);
        }
        expect(TOK_SEMI);
        emit_return();

        return;
    }

    /* Local variable declaration */
    if (is_type_keyword(tok)) {
        CType base_type = parse_type_spec();
        parse_local_decl(base_type);
        return;
    }

    /* Expression statement */
    if (tok != TOK_SEMI) {
        CType ct = expr();
        if (ct != CT_VOID) emit_drop();
    }
    expect(TOK_SEMI);
}

void parse_block(void) {
    expect(TOK_LBRACE);
    cur_scope++;
    while (tok != TOK_RBRACE && tok != TOK_EOF)
        parse_stmt();
    pop_scope(cur_scope - 1);
    cur_scope--;
    expect(TOK_RBRACE);
}

/* Parse local variable declaration(s) after type specifier */
static void parse_local_decl(CType base_type) {
    int base_const = type_had_const;
    do {
        CType var_type = base_type;
        int var_const = base_const;
        /* Check per-declarator const qualifier */
        while (tok == TOK_CONST) { var_const = 1; next_token(); }
        /* Skip pointer stars */
        while (tok == TOK_STAR) { next_token(); var_type = CT_INT; }

        if (tok != TOK_NAME) { error_at("expected variable name"); return; }
        char name[64];
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();

        uint8_t wtype = ctype_to_wasm(var_type);
        int local_idx = alloc_local(wtype);

        Symbol *s = add_sym(name, SYM_LOCAL, var_type);
        s->idx = local_idx;
        s->scope = cur_scope;
        s->is_const = var_const;

        if (accept(TOK_ASSIGN)) {
            CType rhs = assignment_expr();
            emit_coerce(rhs, var_type);
            emit_local_set(local_idx);
        } else if (var_const) {
            fprintf(stderr, "%s:%d: warning: const variable '%s' without initializer\n",
                    src_file ? src_file : "<input>", line_num, name);
        }
    } while (accept(TOK_COMMA));
    expect(TOK_SEMI);
}

/* ---- Top-level parser ---- */

void parse_top_level(void) {
    if (tok == TOK_EOF) return;

    /* Skip stray semicolons */
    if (tok == TOK_SEMI) { next_token(); return; }

    int is_static = 0;
    if (tok == TOK_STATIC) { is_static = 1; next_token(); }

    /* Parse type specifier */
    int is_const = 0;
    if (tok == TOK_CONST) { is_const = 1; next_token(); }

    if (!is_type_keyword(tok) && tok != TOK_NAME) {
        error_fmt("expected type or declaration, got %s", tok_name(tok));
        next_token();
        return;
    }

    CType base_type = parse_type_spec();
    is_const |= type_had_const;

    if (tok != TOK_NAME) {
        error_at("expected name after type");
        next_token();
        return;
    }

    char name[64];
    strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
    next_token();

    /* Function definition or declaration */
    if (tok == TOK_LPAREN) {
        parse_func_def(base_type, name, is_static);
        return;
    }

    /* Global variable declaration */
    if (is_const) {
        /* const int X = value → treat as #define for simple constants */
        if (accept(TOK_ASSIGN)) {
            int negate = 0;
            if (tok == TOK_MINUS) { negate = 1; next_token(); }
            if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
                int val = negate ? -tok_ival : tok_ival;
                if (base_type == CT_FLOAT || base_type == CT_DOUBLE || base_type == CT_LONG_LONG) {
                    int gidx = nglobals++;
                    Symbol *s = add_sym(name, SYM_GLOBAL, base_type);
                    s->idx = gidx;
                    s->is_static = is_static;
                    s->is_const = 1;
                    if (base_type == CT_DOUBLE) s->init_dval = (double)val;
                    else if (base_type == CT_LONG_LONG) s->init_llval = (int64_t)val;
                    else s->init_fval = (float)val;
                } else {
                    Symbol *s = add_sym(name, SYM_DEFINE, CT_INT);
                    snprintf(s->macro_val, sizeof(s->macro_val), "%d", val);
                    s->scope = 0;
                }
                next_token();
            } else if (tok == TOK_FLOAT_LIT || tok == TOK_DOUBLE_LIT) {
                /* const float or const double */
                int gidx = nglobals++;
                Symbol *s = add_sym(name, SYM_GLOBAL, base_type);
                s->idx = gidx;
                s->is_static = is_static;
                s->is_const = 1;
                if (base_type == CT_DOUBLE) s->init_dval = negate ? -tok_dval : tok_dval;
                else s->init_fval = negate ? -tok_fval : tok_fval;
                next_token();
            } else {
                error_at("expected constant value");
            }
        }
        expect(TOK_SEMI);
        return;
    }

    /* Regular global variable */
    int gidx = nglobals++;
    Symbol *s = add_sym(name, SYM_GLOBAL, base_type);
    s->idx = gidx;
    s->is_static = is_static;

    /* Parse and store initializer value */
    if (accept(TOK_ASSIGN)) {
        int negate = 0;
        if (tok == TOK_MINUS) { negate = 1; next_token(); }
        if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
            if (base_type == CT_DOUBLE) s->init_dval = negate ? -(double)tok_ival : (double)tok_ival;
            else if (base_type == CT_FLOAT) s->init_fval = negate ? -(float)tok_ival : (float)tok_ival;
            else if (base_type == CT_LONG_LONG) s->init_llval = negate ? -(int64_t)tok_ival : (int64_t)tok_ival;
            else s->init_ival = negate ? -tok_ival : tok_ival;
            next_token();
        } else if (tok == TOK_FLOAT_LIT || tok == TOK_DOUBLE_LIT) {
            if (base_type == CT_DOUBLE) s->init_dval = negate ? -tok_dval : tok_dval;
            else s->init_fval = negate ? -tok_fval : tok_fval;
            next_token();
        } else if (!negate && tok == TOK_NAME) {
            /* Unexpanded macro (depth exceeded) — use stored value directly */
            Symbol *mac = find_sym_kind(tok_sval, SYM_DEFINE);
            if (mac && mac->macro_val[0]) {
                if (base_type == CT_DOUBLE) s->init_dval = strtod(mac->macro_val, NULL);
                else if (base_type == CT_FLOAT) s->init_fval = strtof(mac->macro_val, NULL);
                else if (base_type == CT_LONG_LONG) s->init_llval = strtoll(mac->macro_val, NULL, 0);
                else s->init_ival = (int)strtol(mac->macro_val, NULL, 0);
                next_token();
            } else {
                error_at("global initializer must be a constant");
                next_token();
            }
        } else {
            error_at("global initializer must be a constant");
            next_token();
        }
    }

    /* Handle multiple declarations: static int a = 0, b = 0; or int *a, *b; */
    while (accept(TOK_COMMA)) {
        while (tok == TOK_STAR) next_token();  /* skip pointer stars */
        if (tok != TOK_NAME) { error_at("expected variable name"); break; }
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();
        gidx = nglobals++;
        s = add_sym(name, SYM_GLOBAL, base_type);
        s->idx = gidx;
        s->is_static = is_static;
        if (accept(TOK_ASSIGN)) {
            int neg = 0;
            if (tok == TOK_MINUS) { neg = 1; next_token(); }
            if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
                if (base_type == CT_DOUBLE) s->init_dval = neg ? -(double)tok_ival : (double)tok_ival;
                else if (base_type == CT_FLOAT) s->init_fval = neg ? -(float)tok_ival : (float)tok_ival;
                else if (base_type == CT_LONG_LONG) s->init_llval = neg ? -(int64_t)tok_ival : (int64_t)tok_ival;
                else s->init_ival = neg ? -tok_ival : tok_ival;
                next_token();
            } else if (tok == TOK_FLOAT_LIT || tok == TOK_DOUBLE_LIT) {
                if (base_type == CT_DOUBLE) s->init_dval = neg ? -tok_dval : tok_dval;
                else s->init_fval = neg ? -tok_fval : tok_fval;
                next_token();
            }
        }
    }
    expect(TOK_SEMI);
}

/* ---- Function definition parser ---- */

static void parse_func_def(CType ret_type, const char *name, int is_static) {
    /* Check for existing forward declaration */
    Symbol *existing = find_sym_kind(name, SYM_FUNC);

    /* Allocate function context */
    if (nfuncs >= MAX_FUNCS) { error_at("too many functions"); return; }
    int func_idx = IMP_COUNT + nfuncs;
    FuncCtx *fc = &func_bufs[nfuncs];
    buf_init(&fc->code);
    fc->nparams = 0;
    fc->nlocals = 0;
    fc->ncall_fixups = 0;

    fc->return_type = ret_type;
    free(fc->name);
    fc->name = strdup(name);

    /* Parse parameter list */
    expect(TOK_LPAREN);
    cur_scope++;

    if (tok != TOK_RPAREN && tok != TOK_VOID) {
        do {
            if (tok == TOK_VOID) { next_token(); break; }
            CType ptype = parse_type_spec();
            if (fc->nparams >= 8) { error_at("too many function parameters"); break; }
            fc->param_wasm_types[fc->nparams] = ctype_to_wasm(ptype);
            fc->param_ctypes[fc->nparams] = ptype;
            fc->nparams++;

            if (tok == TOK_NAME) {
                char pname[64];
                strncpy(pname, tok_sval, sizeof(pname) - 1); pname[sizeof(pname) - 1] = 0;
                next_token();

                /* Add param as a local symbol */
                Symbol *ps = add_sym(pname, SYM_LOCAL, ptype);
                ps->idx = fc->nparams - 1;
                ps->scope = cur_scope;
            }
            /* Unnamed param: counted but no symbol added */
        } while (accept(TOK_COMMA));
    } else if (tok == TOK_VOID) {
        next_token();
    }
    expect(TOK_RPAREN);

    /* Forward declaration? */
    if (tok == TOK_SEMI) {
        next_token();
        /* Register function symbol if not already */
        if (!existing) {
            Symbol *fs = add_sym(name, SYM_FUNC, ret_type);
            fs->idx = func_idx;
            fs->param_count = fc->nparams;
            for (int i = 0; i < fc->nparams; i++)
                fs->param_types[i] = fc->param_ctypes[i];
            fs->is_static = is_static;
            fs->scope = 0;
            nfuncs++;
        }
        pop_scope(cur_scope - 1);
        cur_scope--;
        return;
    }

    /* Function body */
    if (existing && existing->is_defined) {
        error_fmt("function '%s' already defined", name);
    }

    /* Bug #4: Check forward declaration parameter match */
    if (existing && !existing->is_defined) {
        int new_nparams = func_bufs[nfuncs].nparams;
        if (existing->param_count != new_nparams) {
            error_fmt("function '%s' definition has %d params, declaration had %d",
                      name, new_nparams, existing->param_count);
        } else {
            for (int i = 0; i < new_nparams; i++) {
                if (existing->param_types[i] != func_bufs[nfuncs].param_ctypes[i]) {
                    error_fmt("function '%s' param %d type mismatch with declaration", name, i + 1);
                    break;
                }
            }
        }
    }

    /* Register/update symbol */
    Symbol *fs;
    if (existing) {
        fs = existing;
        func_idx = fs->idx;
        /* Free the temp slot we allocated at nfuncs */
        buf_free(&func_bufs[nfuncs].code);
        free(func_bufs[nfuncs].name);
        func_bufs[nfuncs].name = NULL;
        /* Update the function context to the right slot */
        fc = &func_bufs[func_idx - IMP_COUNT];
        fc->return_type = ret_type;
        free(fc->name);
        fc->name = strdup(name);
        fc->nparams = 0;
        fc->nlocals = 0;
        fc->ncall_fixups = 0;

        buf_init(&fc->code);
        /* Re-parse params into the correct fc */
        /* Actually we already parsed params above — copy them */
        FuncCtx *orig = &func_bufs[nfuncs];
        fc->nparams = orig->nparams;
        memcpy(fc->param_wasm_types, orig->param_wasm_types, sizeof(fc->param_wasm_types));
        memcpy(fc->param_ctypes, orig->param_ctypes, sizeof(fc->param_ctypes));
    } else {
        fs = add_sym(name, SYM_FUNC, ret_type);
        fs->idx = func_idx;
        fs->param_count = fc->nparams;
        for (int i = 0; i < fc->nparams; i++)
            fs->param_types[i] = fc->param_ctypes[i];
        fs->is_static = is_static;
        fs->scope = 0;
        nfuncs++;
    }
    fs->is_defined = 1;

    /* Track setup/loop */
    if (strcmp(name, "setup") == 0) has_setup = 1;
    if (strcmp(name, "loop") == 0) has_loop = 1;

    /* Switch to this function's code buffer */
    int save_func = cur_func;
    int save_block_depth = block_depth;
    cur_func = func_idx - IMP_COUNT;
    block_depth = 0;

    /* Parse body */
    expect(TOK_LBRACE);
    while (tok != TOK_RBRACE && tok != TOK_EOF)
        parse_stmt();
    expect(TOK_RBRACE);

    /* Implicit return — always emit; harmless dead code after explicit returns
     * (WASM polymorphic stack accepts anything after unconditional branch) */
    if (ret_type == CT_VOID) {
        emit_return();
    } else {
        if (ret_type == CT_DOUBLE) emit_f64_const(0.0);
        else if (ret_type == CT_FLOAT) emit_f32_const(0.0f);
        else if (ret_type == CT_LONG_LONG) emit_i64_const(0);
        else emit_i32_const(0);
        emit_return();
    }

    /* Add end opcode */
    buf_byte(&func_bufs[cur_func].code, OP_END);

    cur_func = save_func;
    block_depth = save_block_depth;
    pop_scope(cur_scope - 1);
    cur_scope--;
}
