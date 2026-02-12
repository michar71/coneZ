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
    if (tok == TOK_INT_LIT) { int v = (int)tok_i64; next_token(); return v; }
    if (tok == TOK_CHAR_LIT) { int v = (int)tok_i64; next_token(); return v; }
    if (tok == TOK_NAME) {
        Symbol *mac = find_sym_kind(tok_sval, SYM_DEFINE);
        if (mac && mac->macro_val[0]) {
            int v = (int)strtol(mac->macro_val, NULL, 0);
            next_token();
            return v;
        }
    }
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

static int ctype_sizeof_bytes(CType ct) {
    if (ct == CT_CHAR) return 1;
    if (ct == CT_LONG_LONG || ct == CT_ULONG_LONG || ct == CT_DOUBLE) return 8;
    return 4;
}

static void emit_store_for_ctype(CType ct) {
    if (ct == CT_LONG_LONG || ct == CT_ULONG_LONG) {
        emit_op(OP_I64_STORE);
        buf_uleb(CODE, 3);
        buf_uleb(CODE, 0);
    } else if (ct == CT_DOUBLE) {
        emit_op(OP_F64_STORE);
        buf_uleb(CODE, 3);
        buf_uleb(CODE, 0);
    } else if (ct == CT_FLOAT) {
        emit_op(OP_F32_STORE);
        buf_uleb(CODE, 2);
        buf_uleb(CODE, 0);
    } else if (ct == CT_CHAR) {
        emit_op(OP_I32_STORE8);
        buf_uleb(CODE, 0);
        buf_uleb(CODE, 0);
    } else {
        emit_op(OP_I32_STORE);
        buf_uleb(CODE, 2);
        buf_uleb(CODE, 0);
    }
}

static void write_array_elem(int base_off, int idx, CType ct,
                             int32_t i32v, int64_t i64v, float f32v, double f64v) {
    int elem_size = ctype_sizeof_bytes(ct);
    int off = base_off + idx * elem_size;
    if (off < 0 || off + elem_size > MAX_STRINGS) {
        error_at("array initializer out of bounds");
        return;
    }
    if (ct == CT_CHAR) {
        data_buf[off] = (char)i32v;
    } else if (ct == CT_FLOAT) {
        memcpy(data_buf + off, &f32v, 4);
    } else if (ct == CT_DOUBLE) {
        memcpy(data_buf + off, &f64v, 8);
    } else if (ct == CT_LONG_LONG || ct == CT_ULONG_LONG) {
        memcpy(data_buf + off, &i64v, 8);
    } else {
        memcpy(data_buf + off, &i32v, 4);
    }
}

static void alloc_global_scalar_storage(Symbol *s) {
    if (!s) return;
    int elem_size = ctype_sizeof_bytes(s->ctype);
    int align = (elem_size >= 8) ? 8 : (elem_size >= 4 ? 4 : 1);
    int off = add_data_zeros(elem_size, align);
    s->is_mem_backed = 1;
    s->mem_off = off;
}

static void write_global_scalar_init(Symbol *s) {
    if (!s || !s->is_mem_backed) return;
    write_array_elem(s->mem_off, 0, s->ctype, s->init_ival, s->init_llval, s->init_fval, s->init_dval);
}

static int parse_const_for_type(CType base_type,
                                int32_t *out_i32, int64_t *out_i64,
                                float *out_f32, double *out_f64) {
    int negate = 0;
    if (tok == TOK_MINUS) { negate = 1; next_token(); }

    if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
        int64_t v = negate ? -tok_i64 : tok_i64;
        *out_i32 = (int32_t)v;
        *out_i64 = v;
        *out_f32 = (float)v;
        *out_f64 = (double)v;
        next_token();
        return 1;
    }

    if (tok == TOK_FLOAT_LIT || tok == TOK_DOUBLE_LIT) {
        double dv = (tok == TOK_DOUBLE_LIT) ? tok_dval : (double)tok_fval;
        if (negate) dv = -dv;
        *out_i32 = (int32_t)dv;
        *out_i64 = (int64_t)dv;
        *out_f32 = (float)dv;
        *out_f64 = dv;
        next_token();
        return 1;
    }

    if (tok == TOK_NAME) {
        Symbol *mac = find_sym_kind(tok_sval, SYM_DEFINE);
        if (mac && mac->macro_val[0]) {
            if (base_type == CT_DOUBLE) {
                *out_f64 = strtod(mac->macro_val, NULL);
                if (negate) *out_f64 = -(*out_f64);
                *out_f32 = (float)(*out_f64);
                *out_i64 = (int64_t)(*out_f64);
                *out_i32 = (int32_t)(*out_f64);
            } else if (base_type == CT_FLOAT) {
                *out_f32 = strtof(mac->macro_val, NULL);
                if (negate) *out_f32 = -(*out_f32);
                *out_f64 = (double)(*out_f32);
                *out_i64 = (int64_t)(*out_f32);
                *out_i32 = (int32_t)(*out_f32);
            } else if (base_type == CT_LONG_LONG || base_type == CT_ULONG_LONG) {
                if (base_type == CT_ULONG_LONG)
                    *out_i64 = (int64_t)strtoull(mac->macro_val, NULL, 0);
                else
                    *out_i64 = strtoll(mac->macro_val, NULL, 0);
                if (negate) *out_i64 = -(*out_i64);
                *out_i32 = (int32_t)(*out_i64);
                *out_f32 = (float)(*out_i64);
                *out_f64 = (double)(*out_i64);
            } else {
                *out_i32 = (int32_t)strtol(mac->macro_val, NULL, 0);
                if (negate) *out_i32 = -(*out_i32);
                *out_i64 = (int64_t)(*out_i32);
                *out_f32 = (float)(*out_i32);
                *out_f64 = (double)(*out_i32);
            }
            next_token();
            return 1;
        }
    }

    return 0;
}

static int dims_tail_product(const int *dims, int ndims, int start) {
    int p = 1;
    for (int i = start; i < ndims; i++) p *= (dims[i] > 0 ? dims[i] : 1);
    return p;
}

static void parse_global_array_init_level(Symbol *s, CType base_type,
                                          const int *dims, int ndims,
                                          int level, int base_elem_index) {
    int dim = dims[level] > 0 ? dims[level] : 1;
    int stride = dims_tail_product(dims, ndims, level + 1);
    int had_braces = accept(TOK_LBRACE);
    int idx = 0;

    while (tok != TOK_EOF) {
        if (had_braces && tok == TOK_RBRACE) break;

        int target = idx;
        if (had_braces && tok == TOK_LBRACKET) {
            next_token();
            target = parse_case_value();
            expect(TOK_RBRACKET);
            expect(TOK_ASSIGN);
            idx = target;
        }

        if (target >= dim || target < 0) {
            error_at("too many initializers for global array");
            break;
        }

        if (level == ndims - 1) {
            int32_t i32v = 0; int64_t i64v = 0; float f32v = 0.0f; double f64v = 0.0;
            if (!parse_const_for_type(base_type, &i32v, &i64v, &f32v, &f64v)) {
                error_at("global array initializer must be constant");
                break;
            }
            write_array_elem(s->init_ival, base_elem_index + target, base_type, i32v, i64v, f32v, f64v);
            idx = target + 1;
        } else {
            if (tok != TOK_LBRACE) {
                error_at("nested array initializer requires braces");
                break;
            }
            parse_global_array_init_level(s, base_type, dims, ndims, level + 1,
                                          base_elem_index + target * stride);
            idx = target + 1;
        }

        if (had_braces) {
            if (accept(TOK_COMMA)) {
                if (tok == TOK_RBRACE) break;
                continue;
            }
            break;
        } else {
            break;
        }
    }

    if (had_braces) expect(TOK_RBRACE);
}

static void parse_global_array_initializer(Symbol *s, CType base_type, const int *dims, int ndims) {
    parse_global_array_init_level(s, base_type, dims, ndims, 0, 0);
}

static void parse_local_array_init_level(int local_idx, CType base_type, int elem_size,
                                         const int *dims, int ndims,
                                         int level, int base_elem_index) {
    int dim = dims[level] > 0 ? dims[level] : 1;
    int stride = dims_tail_product(dims, ndims, level + 1);
    int had_braces = accept(TOK_LBRACE);
    int idx = 0;

    while (tok != TOK_EOF) {
        if (had_braces && tok == TOK_RBRACE) break;

        int target = idx;
        if (had_braces && tok == TOK_LBRACKET) {
            next_token();
            target = parse_case_value();
            expect(TOK_RBRACKET);
            expect(TOK_ASSIGN);
            idx = target;
        }

        if (target >= dim || target < 0) {
            error_at("too many initializers for local array");
            break;
        }

        if (level == ndims - 1) {
            emit_local_get(local_idx);
            emit_i32_const((base_elem_index + target) * elem_size);
            emit_op(OP_I32_ADD);
            CType rhs = assignment_expr();
            emit_coerce(rhs, base_type);
            emit_store_for_ctype(base_type);
            idx = target + 1;
        } else {
            if (tok != TOK_LBRACE) {
                error_at("nested array initializer requires braces");
                break;
            }
            parse_local_array_init_level(local_idx, base_type, elem_size, dims, ndims,
                                         level + 1, base_elem_index + target * stride);
            idx = target + 1;
        }

        if (had_braces) {
            if (accept(TOK_COMMA)) {
                if (tok == TOK_RBRACE) break;
                continue;
            }
            break;
        } else {
            break;
        }
    }

    if (had_braces) expect(TOK_RBRACE);
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
            int saved_error = had_error;
            LexerSave lsave;
            lexer_save(&lsave);
            int depth = 1; /* already consumed opening brace */
            while (depth > 0 && tok != TOK_EOF) {
                if (tok == TOK_LBRACE) depth++;
                else if (tok == TOK_RBRACE) { depth--; if (depth <= 0) break; }
                else if (depth == 1 && tok == TOK_DEFAULT) has_default = 1;
                else if (depth == 1 && tok == TOK_CASE) {
                    next_token(); /* skip 'case' */
                    int case_val = parse_case_value();
                    if (tok == TOK_COLON && ncase_vals < 256) {
                        case_vals[ncase_vals++] = case_val;
                        continue;
                    }
                    all_cases_resolved = 0;
                }
                next_token();
            }
            lexer_restore(&lsave);
            had_error = saved_error;
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
            else if (ret == CT_LONG_LONG || ret == CT_ULONG_LONG) emit_i64_const(0);
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
        if (had_error && tok != TOK_SEMI && tok != TOK_RBRACE && tok != TOK_EOF) {
            synchronize(1, 0, 0);  /* Try to recover to next statement */
            return;
        }
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
    int base_pointer = type_had_pointer ? 1 : 0; /* first declarator may have consumed '*' in parse_type_spec */
    do {
        CType var_type = base_type;
        int var_const = base_const;
        int is_array = 0;
        int array_size = 0;
        int array_dims[MAX_TYPE_DEPTH];
        int array_ndims = 0;
        int is_pointer = base_pointer;
        base_pointer = 0;
        /* Check per-declarator const qualifier */
        while (tok == TOK_CONST) { var_const = 1; next_token(); }
        /* Skip pointer stars - count depth */
        while (tok == TOK_STAR) { next_token(); is_pointer++; var_type = CT_INT; }

        if (tok != TOK_NAME) { 
            error_at("expected variable name"); 
            synchronize(1, 0, 0);  /* Skip to semicolon */
            return; 
        }
        char name[64];
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();

        /* Check for array declaration(s) */
        while (tok == TOK_LBRACKET) {
            next_token();
            if (tok == TOK_INT_LIT) {
                array_size = (int)tok_i64;
                if (array_ndims < MAX_TYPE_DEPTH) array_dims[array_ndims++] = array_size;
                next_token();
            } else if (tok == TOK_RBRACKET) {
                array_size = 0; /* infer from initializer (char[] = "...") */
                if (array_ndims < MAX_TYPE_DEPTH) array_dims[array_ndims++] = 0;
            } else {
                error_at("array size must be constant integer");
                array_size = 1;
                if (array_ndims < MAX_TYPE_DEPTH) array_dims[array_ndims++] = 1;
            }
            expect(TOK_RBRACKET);
            is_array = 1;
        }

        uint8_t wtype = ctype_to_wasm(var_type);
        int local_idx = -1;
        int elem_size = ctype_sizeof_bytes(var_type);
        int consumed_array_string_init = 0;
        char array_init_str[1024];
        int array_init_len = 0;

        if (is_array && array_size == 0) {
            if (array_ndims != 1) {
                error_at("inferred size only supported for single-dimensional char[]");
                array_size = 1;
                if (array_ndims > 0) array_dims[array_ndims - 1] = 1;
            }
            if (var_type != CT_CHAR) {
                error_at("inferred array size only supported for char[] with string initializer");
                array_size = 1;
                if (array_ndims > 0) array_dims[array_ndims - 1] = 1;
            } else if (tok == TOK_ASSIGN) {
                next_token();
                if (tok != TOK_STR_LIT) {
                    error_at("char[] requires string literal initializer");
                    array_size = 1;
                } else {
                    array_init_len = tok_slen;
                    if (array_init_len > (int)sizeof(array_init_str) - 1)
                        array_init_len = (int)sizeof(array_init_str) - 1;
                    memcpy(array_init_str, tok_sval, array_init_len);
                    array_init_str[array_init_len] = 0;
                    array_size = array_init_len + 1; /* include terminator */
                    if (array_ndims > 0) array_dims[array_ndims - 1] = array_size;
                    consumed_array_string_init = 1;
                    next_token();
                }
            } else {
                error_at("incomplete array type requires initializer");
                array_size = 1;
                if (array_ndims > 0) array_dims[array_ndims - 1] = 1;
            }
        }

        if (is_array) {
            /* Local arrays are backed by linear memory. Local symbol holds
             * base pointer as i32. */
            int total_elems = 1;
            for (int d = 0; d < array_ndims; d++) {
                int dim = array_dims[d] > 0 ? array_dims[d] : 1;
                total_elems *= dim;
            }
            int align = (elem_size >= 8) ? 8 : (elem_size >= 4 ? 4 : 1);
            int bytes = total_elems * elem_size;
            int off = add_data_zeros(bytes, align);
            local_idx = alloc_local(WASM_I32);
            emit_i32_const(off);
            emit_local_set(local_idx);
        } else {
            local_idx = alloc_local(wtype);
        }

        Symbol *s = add_sym(name, SYM_LOCAL, var_type);
        s->idx = local_idx;
        s->scope = cur_scope;
        s->is_const = var_const;
        s->type_info = type_base(var_type);
        if (is_array) {
            for (int d = array_ndims - 1; d >= 0; d--) {
                int dim = array_dims[d] > 0 ? array_dims[d] : 1;
                s->type_info = type_array(s->type_info, dim);
            }
        } else if (is_pointer) {
            for (int i = 0; i < is_pointer; i++) {
                s->type_info = type_pointer(s->type_info);
            }
        }
        s->stack_offset = local_idx * 4;  /* Simple stack layout metadata */
        s->is_lvalue = 1;

        if (consumed_array_string_init) {
            int ncopy = (array_init_len < array_size) ? array_init_len : array_size;
            for (int i = 0; i < ncopy; i++) {
                emit_local_get(local_idx);
                emit_i32_const(i);
                emit_op(OP_I32_ADD);
                emit_i32_const((unsigned char)array_init_str[i]);
                emit_op(OP_I32_STORE8);
                buf_uleb(CODE, 0);
                buf_uleb(CODE, 0);
            }
            if (array_size > array_init_len) {
                emit_local_get(local_idx);
                emit_i32_const(array_init_len);
                emit_op(OP_I32_ADD);
                emit_i32_const(0);
                emit_op(OP_I32_STORE8);
                buf_uleb(CODE, 0);
                buf_uleb(CODE, 0);
            }
        } else if (accept(TOK_ASSIGN)) {
            if (is_array) {
                if (var_type == CT_CHAR && array_ndims == 1 && tok == TOK_STR_LIT) {
                    int slen = tok_slen;
                    int ncopy = (slen < array_size) ? slen : array_size;
                    for (int i = 0; i < ncopy; i++) {
                        emit_local_get(local_idx);
                        emit_i32_const(i);
                        emit_op(OP_I32_ADD);
                        emit_i32_const((unsigned char)tok_sval[i]);
                        emit_op(OP_I32_STORE8);
                        buf_uleb(CODE, 0);
                        buf_uleb(CODE, 0);
                    }
                    if (array_size > slen) {
                        emit_local_get(local_idx);
                        emit_i32_const(slen);
                        emit_op(OP_I32_ADD);
                        emit_i32_const(0);
                        emit_op(OP_I32_STORE8);
                        buf_uleb(CODE, 0);
                        buf_uleb(CODE, 0);
                    }
                    next_token();
                } else {
                    parse_local_array_init_level(local_idx, var_type, elem_size,
                                                 array_dims, array_ndims, 0, 0);
                }
            }
            else {
                CType rhs = assignment_expr();
                emit_coerce(rhs, var_type);
                emit_local_set(local_idx);
            }
        } else if (var_const && !is_array) {
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
        synchronize(1, 0, 0);  /* Skip to semicolon */
        return;
    }

    CType base_type = parse_type_spec();
    is_const |= type_had_const;

    if (tok != TOK_NAME) {
        error_at("expected name after type");
        synchronize(1, 0, 0);  /* Skip to semicolon */
        return;
    }

    char name[64];
    strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
    next_token();

    /* Check for array declaration */
    int is_array = 0;
    int array_size = 0;
    int array_dims[MAX_TYPE_DEPTH];
    int array_ndims = 0;
    while (tok == TOK_LBRACKET) {
        next_token();
        if (tok == TOK_INT_LIT) {
            array_size = (int)tok_i64;
            if (array_ndims < MAX_TYPE_DEPTH) array_dims[array_ndims++] = array_size;
            next_token();
        } else if (tok == TOK_RBRACKET) {
            array_size = 0; /* infer from initializer (char[] = "...") */
            if (array_ndims < MAX_TYPE_DEPTH) array_dims[array_ndims++] = 0;
        } else {
            error_at("array size must be constant integer");
            array_size = 1;
            if (array_ndims < MAX_TYPE_DEPTH) array_dims[array_ndims++] = 1;
        }
        expect(TOK_RBRACKET);
        is_array = 1;
    }

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
                int64_t val64 = negate ? -tok_i64 : tok_i64;
                int32_t val32 = (int32_t)val64;
                if (base_type == CT_FLOAT || base_type == CT_DOUBLE || base_type == CT_LONG_LONG || base_type == CT_ULONG_LONG) {
                    int gidx = nglobals++;
                    Symbol *s = add_sym(name, SYM_GLOBAL, base_type);
                    s->idx = gidx;
                    s->is_static = is_static;
                    s->is_const = 1;
                    alloc_global_scalar_storage(s);
                    if (base_type == CT_DOUBLE) s->init_dval = (double)val64;
                    else if (base_type == CT_LONG_LONG || base_type == CT_ULONG_LONG) s->init_llval = val64;
                    else s->init_fval = (float)val64;
                    write_global_scalar_init(s);
                } else {
                    Symbol *s = add_sym(name, SYM_DEFINE, CT_INT);
                    snprintf(s->macro_val, sizeof(s->macro_val), "%d", (int)val32);
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
                alloc_global_scalar_storage(s);
                if (base_type == CT_DOUBLE) s->init_dval = negate ? -tok_dval : tok_dval;
                else s->init_fval = negate ? -tok_fval : tok_fval;
                write_global_scalar_init(s);
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
    s->type_info = type_base(base_type);
    if (!is_array)
        alloc_global_scalar_storage(s);
    int array_allocated = 0;
    if (is_array) {
        if (array_size > 0) {
            int total_elems = 1;
            for (int d = 0; d < array_ndims; d++) {
                int dim = array_dims[d] > 0 ? array_dims[d] : 1;
                total_elems *= dim;
            }
            int elem_size = ctype_sizeof_bytes(base_type);
            int align = (elem_size >= 8) ? 8 : (elem_size >= 4 ? 4 : 1);
            int bytes = total_elems * elem_size;
            int off = add_data_zeros(bytes, align);
            for (int d = array_ndims - 1; d >= 0; d--) {
                int dim = array_dims[d] > 0 ? array_dims[d] : 1;
                s->type_info = type_array(s->type_info, dim);
            }
            /* Array globals are represented as pointer-like globals that hold
             * the base address in linear memory. */
            s->init_ival = off;
            s->ctype = CT_INT;
            array_allocated = 1;
        }
    }

    /* Parse and store initializer value */
    if (is_array && array_size == 0 && tok != TOK_ASSIGN) {
        error_at("incomplete array type requires initializer");
        array_size = 1;
        if (array_ndims > 0) array_dims[array_ndims - 1] = 1;
    }

    if (accept(TOK_ASSIGN)) {
        if (is_array) {
            if (base_type == CT_CHAR && tok == TOK_STR_LIT) {
                int slen = tok_slen;
                if (array_ndims != 1) {
                    error_at("inferred size only supported for single-dimensional char[]");
                    array_size = 1;
                    if (array_ndims > 0) array_dims[array_ndims - 1] = 1;
                }
                if (array_size == 0) array_size = slen + 1;
                if (array_ndims > 0) array_dims[array_ndims - 1] = array_size;
                if (!array_allocated) {
                    int elem_size = ctype_sizeof_bytes(base_type);
                    int off = add_data_zeros(array_size * elem_size, 1);
                    for (int d = array_ndims - 1; d >= 0; d--) {
                        int dim = array_dims[d] > 0 ? array_dims[d] : 1;
                        s->type_info = type_array(s->type_info, dim);
                    }
                    s->init_ival = off;
                    s->ctype = CT_INT;
                    array_allocated = 1;
                }
                int ncopy = (slen < array_size) ? slen : array_size;
                for (int i = 0; i < ncopy; i++) data_buf[s->init_ival + i] = tok_sval[i];
                if (array_size > slen) data_buf[s->init_ival + slen] = 0;
                next_token();
                } else {
                    if (array_size == 0) {
                        error_at("inferred array size requires string literal initializer");
                        array_size = 1;
                }
                if (!array_allocated) {
                        int elem_size = ctype_sizeof_bytes(base_type);
                        int align = (elem_size >= 8) ? 8 : (elem_size >= 4 ? 4 : 1);
                        int off = add_data_zeros(array_size * elem_size, align);
                        for (int d = array_ndims - 1; d >= 0; d--) {
                            int dim = array_dims[d] > 0 ? array_dims[d] : 1;
                            s->type_info = type_array(s->type_info, dim);
                        }
                        s->init_ival = off;
                        s->ctype = CT_INT;
                        array_allocated = 1;
                }
                parse_global_array_initializer(s, base_type, array_dims, array_ndims);
            }
        } else {
            int negate = 0;
            if (tok == TOK_MINUS) { negate = 1; next_token(); }
            if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
                if (base_type == CT_DOUBLE) s->init_dval = negate ? -(double)tok_i64 : (double)tok_i64;
                else if (base_type == CT_FLOAT) s->init_fval = negate ? -(float)tok_i64 : (float)tok_i64;
                else if (base_type == CT_LONG_LONG || base_type == CT_ULONG_LONG) s->init_llval = negate ? -tok_i64 : tok_i64;
                else s->init_ival = negate ? -(int)tok_i64 : (int)tok_i64;
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
                    else if (base_type == CT_ULONG_LONG) s->init_llval = (int64_t)strtoull(mac->macro_val, NULL, 0);
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
    }

    if (!is_array)
        write_global_scalar_init(s);

    /* Handle multiple declarations: static int a = 0, b = 0; or int *a, *b; */
    while (accept(TOK_COMMA)) {
        while (tok == TOK_STAR) next_token();  /* skip pointer stars */
        if (tok != TOK_NAME) { error_at("expected variable name"); break; }
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();
        int decl_is_array = 0;
        int decl_array_size = 0;
        int decl_array_dims[MAX_TYPE_DEPTH];
        int decl_array_ndims = 0;
        while (tok == TOK_LBRACKET) {
            next_token();
            if (tok == TOK_INT_LIT) {
                decl_array_size = (int)tok_i64;
                if (decl_array_ndims < MAX_TYPE_DEPTH) decl_array_dims[decl_array_ndims++] = decl_array_size;
                next_token();
            }
            else if (tok == TOK_RBRACKET) {
                decl_array_size = 0;
                if (decl_array_ndims < MAX_TYPE_DEPTH) decl_array_dims[decl_array_ndims++] = 0;
            }
            else { error_at("array size must be constant integer"); decl_array_size = 1; if (decl_array_ndims < MAX_TYPE_DEPTH) decl_array_dims[decl_array_ndims++] = 1; }
            expect(TOK_RBRACKET);
            decl_is_array = 1;
        }
        gidx = nglobals++;
        s = add_sym(name, SYM_GLOBAL, base_type);
        s->idx = gidx;
        s->is_static = is_static;
        s->type_info = type_base(base_type);
        if (!decl_is_array)
            alloc_global_scalar_storage(s);
        int decl_array_allocated = 0;
        if (decl_is_array) {
            if (decl_array_size > 0) {
                int total_elems = 1;
                for (int d = 0; d < decl_array_ndims; d++) {
                    int dim = decl_array_dims[d] > 0 ? decl_array_dims[d] : 1;
                    total_elems *= dim;
                }
                int elem_size = ctype_sizeof_bytes(base_type);
                int align = (elem_size >= 8) ? 8 : (elem_size >= 4 ? 4 : 1);
                int bytes = total_elems * elem_size;
                int off = add_data_zeros(bytes, align);
                for (int d = decl_array_ndims - 1; d >= 0; d--) {
                    int dim = decl_array_dims[d] > 0 ? decl_array_dims[d] : 1;
                    s->type_info = type_array(s->type_info, dim);
                }
                s->init_ival = off;
                s->ctype = CT_INT;
                decl_array_allocated = 1;
            }
        }
        if (decl_is_array && decl_array_size == 0 && tok != TOK_ASSIGN) {
            error_at("incomplete array type requires initializer");
            decl_array_size = 1;
            if (decl_array_ndims > 0) decl_array_dims[decl_array_ndims - 1] = 1;
        }
        if (accept(TOK_ASSIGN)) {
            if (decl_is_array) {
                if (base_type == CT_CHAR && tok == TOK_STR_LIT) {
                    int slen = tok_slen;
                    if (decl_array_ndims != 1) {
                        error_at("inferred size only supported for single-dimensional char[]");
                        decl_array_size = 1;
                        if (decl_array_ndims > 0) decl_array_dims[decl_array_ndims - 1] = 1;
                    }
                    if (decl_array_size == 0) decl_array_size = slen + 1;
                    if (decl_array_ndims > 0) decl_array_dims[decl_array_ndims - 1] = decl_array_size;
                    if (!decl_array_allocated) {
                        int off = add_data_zeros(decl_array_size, 1);
                        for (int d = decl_array_ndims - 1; d >= 0; d--) {
                            int dim = decl_array_dims[d] > 0 ? decl_array_dims[d] : 1;
                            s->type_info = type_array(s->type_info, dim);
                        }
                        s->init_ival = off;
                        s->ctype = CT_INT;
                        decl_array_allocated = 1;
                    }
                    int ncopy = (slen < decl_array_size) ? slen : decl_array_size;
                    for (int i = 0; i < ncopy; i++) data_buf[s->init_ival + i] = tok_sval[i];
                    if (decl_array_size > slen) data_buf[s->init_ival + slen] = 0;
                    next_token();
                } else {
                    if (decl_array_size == 0) {
                        error_at("inferred array size requires string literal initializer");
                        decl_array_size = 1;
                    }
                    if (!decl_array_allocated) {
                        int elem_size = ctype_sizeof_bytes(base_type);
                        int align = (elem_size >= 8) ? 8 : (elem_size >= 4 ? 4 : 1);
                        int off = add_data_zeros(decl_array_size * elem_size, align);
                        for (int d = decl_array_ndims - 1; d >= 0; d--) {
                            int dim = decl_array_dims[d] > 0 ? decl_array_dims[d] : 1;
                            s->type_info = type_array(s->type_info, dim);
                        }
                        s->init_ival = off;
                        s->ctype = CT_INT;
                        decl_array_allocated = 1;
                    }
                    parse_global_array_initializer(s, base_type, decl_array_dims, decl_array_ndims);
                }
            } else {
                int neg = 0;
                if (tok == TOK_MINUS) { neg = 1; next_token(); }
                if (tok == TOK_INT_LIT || tok == TOK_CHAR_LIT) {
                    if (base_type == CT_DOUBLE) s->init_dval = neg ? -(double)tok_i64 : (double)tok_i64;
                    else if (base_type == CT_FLOAT) s->init_fval = neg ? -(float)tok_i64 : (float)tok_i64;
                    else if (base_type == CT_LONG_LONG || base_type == CT_ULONG_LONG) s->init_llval = neg ? -tok_i64 : tok_i64;
                    else s->init_ival = neg ? -(int)tok_i64 : (int)tok_i64;
                    next_token();
                } else if (tok == TOK_FLOAT_LIT || tok == TOK_DOUBLE_LIT) {
                    if (base_type == CT_DOUBLE) s->init_dval = neg ? -tok_dval : tok_dval;
                    else s->init_fval = neg ? -tok_fval : tok_fval;
                    next_token();
                }
            }
        }

        if (!decl_is_array)
            write_global_scalar_init(s);
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
                ps->type_info = type_base(ptype);
                ps->stack_offset = (fc->nparams - 1) * 4;  /* Params are at positive offsets from frame */
                ps->is_lvalue = 1;
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
        if (fc->name) free(fc->name);
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
        else if (ret_type == CT_LONG_LONG || ret_type == CT_ULONG_LONG) emit_i64_const(0);
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
