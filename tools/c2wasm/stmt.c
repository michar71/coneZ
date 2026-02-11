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
                (void)ct;
                emit_drop();
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

        /* Wrap in a block for break target */
        emit_block();

        if (ctrl_sp >= MAX_CTRL) { error_at("control stack overflow"); return; }
        ctrl_stk[ctrl_sp].kind = CTRL_SWITCH;
        ctrl_stk[ctrl_sp].break_depth = block_depth - 1;
        ctrl_stk[ctrl_sp].cont_depth = -1;
        ctrl_stk[ctrl_sp].incr_buf = NULL;
        ctrl_sp++;

        expect(TOK_LBRACE);

        int had_default = 0;
        while (tok != TOK_RBRACE && tok != TOK_EOF) {
            if (tok == TOK_CASE) {
                next_token();
                /* Parse constant expression */
                int case_val = 0;
                int negate = 0;
                if (tok == TOK_MINUS) { negate = 1; next_token(); }
                if (tok == TOK_INT_LIT) { case_val = tok_ival; next_token(); }
                else if (tok == TOK_CHAR_LIT) { case_val = tok_ival; next_token(); }
                else { error_at("expected constant in case label"); next_token(); }
                if (negate) case_val = -case_val;
                expect(TOK_COLON);

                /* Compare switch value with case value */
                emit_local_get(switch_local);
                emit_i32_const(case_val);
                emit_op(OP_I32_EQ);
                emit_if_void();

                /* Parse case body */
                while (tok != TOK_CASE && tok != TOK_DEFAULT && tok != TOK_RBRACE && tok != TOK_EOF)
                    parse_stmt();

                emit_end();
            } else if (tok == TOK_DEFAULT) {
                next_token();
                expect(TOK_COLON);
                had_default = 1;
                while (tok != TOK_CASE && tok != TOK_DEFAULT && tok != TOK_RBRACE && tok != TOK_EOF)
                    parse_stmt();
            } else {
                parse_stmt();
            }
        }
        (void)had_default;

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
    do {
        /* Skip const qualifier on the variable */
        while (tok == TOK_CONST) next_token();
        /* Skip pointer stars */
        while (tok == TOK_STAR) { next_token(); base_type = CT_INT; }

        if (tok != TOK_NAME) { error_at("expected variable name"); return; }
        char name[64];
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();

        uint8_t wtype = ctype_to_wasm(base_type);
        int local_idx = alloc_local(wtype);

        Symbol *s = add_sym(name, SYM_LOCAL, base_type);
        s->idx = local_idx;
        s->scope = cur_scope;

        if (accept(TOK_ASSIGN)) {
            CType rhs = assignment_expr();
            emit_coerce(rhs, base_type);
            emit_local_set(local_idx);
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
            if (tok == TOK_INT_LIT) {
                Symbol *s = add_sym(name, SYM_DEFINE, CT_INT);
                snprintf(s->macro_val, sizeof(s->macro_val), "%d", tok_ival);
                s->scope = 0;
                next_token();
            } else if (tok == TOK_FLOAT_LIT) {
                /* For float constants, allocate a global with the init value */
                int gidx = nglobals++;
                Symbol *s = add_sym(name, SYM_GLOBAL, base_type);
                s->idx = gidx;
                s->is_static = is_static;
                s->init_fval = tok_fval;
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
        if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
            s->init_ival = tok_ival;
            next_token();
        } else if (tok == TOK_FLOAT_LIT) {
            s->init_fval = tok_fval;
            next_token();
        } else if (tok == TOK_MINUS) {
            next_token();
            if (tok == TOK_INT_LIT) {
                s->init_ival = -tok_ival;
                next_token();
            } else if (tok == TOK_FLOAT_LIT) {
                s->init_fval = -tok_fval;
                next_token();
            }
        } else if (tok == TOK_NAME) {
            /* Might be a macro — let it expand then read the value */
            Symbol *mac = find_sym_kind(tok_sval, SYM_DEFINE);
            if (mac) {
                /* Macro will be expanded by lexer; re-lex to get the literal */
                next_token();
                if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
                    s->init_ival = tok_ival;
                    next_token();
                } else if (tok == TOK_FLOAT_LIT) {
                    s->init_fval = tok_fval;
                    next_token();
                }
            } else {
                error_at("global initializer must be a constant");
                next_token();
            }
        } else {
            error_at("global initializer must be a constant");
            next_token();
        }
    }

    /* Handle multiple declarations: static int a = 0, b = 0; */
    while (accept(TOK_COMMA)) {
        if (tok != TOK_NAME) { error_at("expected variable name"); break; }
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();
        gidx = nglobals++;
        s = add_sym(name, SYM_GLOBAL, base_type);
        s->idx = gidx;
        s->is_static = is_static;
        if (accept(TOK_ASSIGN)) {
            if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
                s->init_ival = tok_ival; next_token();
            } else if (tok == TOK_FLOAT_LIT) {
                s->init_fval = tok_fval; next_token();
            } else if (tok == TOK_MINUS) {
                next_token();
                if (tok == TOK_INT_LIT) { s->init_ival = -tok_ival; next_token(); }
                else if (tok == TOK_FLOAT_LIT) { s->init_fval = -tok_fval; next_token(); }
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
    fc->name = strdup(name);

    /* Parse parameter list */
    expect(TOK_LPAREN);
    cur_scope++;

    if (tok != TOK_RPAREN && tok != TOK_VOID) {
        do {
            if (tok == TOK_VOID) { next_token(); break; }
            CType ptype = parse_type_spec();
            if (tok == TOK_NAME) {
                char pname[64];
                strncpy(pname, tok_sval, sizeof(pname) - 1); pname[sizeof(pname) - 1] = 0;
                next_token();

                if (fc->nparams >= 8) { error_at("too many function parameters"); break; }
                fc->param_wasm_types[fc->nparams] = ctype_to_wasm(ptype);
                fc->param_ctypes[fc->nparams] = ptype;
                fc->nparams++;

                /* Add param as a local symbol */
                Symbol *ps = add_sym(pname, SYM_LOCAL, ptype);
                ps->idx = fc->nparams - 1;
                ps->scope = cur_scope;
            }
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

    /* Register/update symbol */
    Symbol *fs;
    if (existing) {
        fs = existing;
        func_idx = fs->idx;
        /* Update the function context to the right slot */
        fc = &func_bufs[func_idx - IMP_COUNT];
        fc->return_type = ret_type;
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
        if (ret_type == CT_FLOAT) emit_f32_const(0.0f);
        else emit_i32_const(0);
        emit_return();
    }

    /* Add end opcode */
    buf_byte(&func_bufs[cur_func].code, OP_END);

    cur_func = save_func;
    pop_scope(cur_scope - 1);
    cur_scope--;
}
