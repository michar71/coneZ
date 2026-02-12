/*
 * expr.c — precedence-climbing expression parser for c2wasm
 *
 * Full C operator precedence with 15 levels.
 * Returns the CType of the resulting expression on the WASM stack.
 */
#include "c2wasm.h"

static CType unary_expr(void);
static CType postfix_expr(void);
static CType primary_expr(void);
static CType prec_expr(int min_prec);
static CType prec_expr_tail(CType left, int min_prec);
static int ctype_sizeof(CType ct);
static void emit_sym_load(Symbol *sym);
static void emit_sym_store(Symbol *sym);

/* Track the last variable symbol accessed for postfix ++/-- */
static Symbol *last_var_sym = NULL;

/* Lvalue tracking for assignment to array elements/pointers */
static int lvalue_addr_local = -1;  /* Local holding address for complex lvalue, or -1 */
static CType lvalue_type = CT_INT;  /* Type of the lvalue */

/* Lightweight expression pointer metadata (side-channel alongside CType). */
static int expr_last_is_ptr = 0;
static int expr_last_elem_size = 4;
static int expr_last_has_type = 0;
static TypeInfo expr_last_type;

static void expr_set_scalar_type(CType ct) {
    expr_last_has_type = 1;
    expr_last_type = type_base(ct);
    expr_last_is_ptr = 0;
    expr_last_elem_size = ctype_sizeof(ct);
}

static int ctype_sizeof(CType ct) {
    if (ct == CT_CHAR) return 1;
    if (ct == CT_LONG_LONG || ct == CT_ULONG_LONG || ct == CT_DOUBLE) return 8;
    return 4;
}

static void emit_mem_store_for_ctype(CType ct) {
    if (ct == CT_LONG_LONG || ct == CT_ULONG_LONG) {
        emit_op(OP_I64_STORE); buf_uleb(CODE, 3); buf_uleb(CODE, 0);
    } else if (ct == CT_DOUBLE) {
        emit_op(OP_F64_STORE); buf_uleb(CODE, 3); buf_uleb(CODE, 0);
    } else if (ct == CT_FLOAT) {
        emit_op(OP_F32_STORE); buf_uleb(CODE, 2); buf_uleb(CODE, 0);
    } else if (ct == CT_CHAR) {
        emit_op(OP_I32_STORE8); buf_uleb(CODE, 0); buf_uleb(CODE, 0);
    } else {
        emit_op(OP_I32_STORE); buf_uleb(CODE, 2); buf_uleb(CODE, 0);
    }
}

static void emit_mem_load_for_ctype(CType ct) {
    if (ct == CT_LONG_LONG || ct == CT_ULONG_LONG) {
        emit_op(OP_I64_LOAD); buf_uleb(CODE, 3); buf_uleb(CODE, 0);
    } else if (ct == CT_DOUBLE) {
        emit_op(OP_F64_LOAD); buf_uleb(CODE, 3); buf_uleb(CODE, 0);
    } else if (ct == CT_FLOAT) {
        emit_op(OP_F32_LOAD); buf_uleb(CODE, 2); buf_uleb(CODE, 0);
    } else if (ct == CT_CHAR) {
        emit_op(OP_I32_LOAD8_S); buf_uleb(CODE, 0); buf_uleb(CODE, 0);
    } else {
        emit_op(OP_I32_LOAD); buf_uleb(CODE, 2); buf_uleb(CODE, 0);
    }
}

static void ensure_local_mem_backed(Symbol *sym) {
    if (!sym || sym->kind != SYM_LOCAL) return;
    if (sym->is_mem_backed) return;
    int sz = ctype_sizeof(sym->ctype);
    int align = (sz >= 8) ? 8 : (sz >= 4 ? 4 : 1);
    sym->mem_off = add_data_zeros(sz, align);
    /* Initialize spill slot from current local value */
    emit_i32_const(sym->mem_off);
    emit_local_get(sym->idx);
    emit_mem_store_for_ctype(sym->ctype);
    sym->is_mem_backed = 1;
}

static void emit_sym_store_and_reload(Symbol *sym) {
    if (!sym) return;
    if (sym->kind == SYM_LOCAL) {
        if (sym->is_mem_backed) {
            int tmp = alloc_local(ctype_to_wasm(sym->ctype));
            emit_local_tee(tmp);
            emit_i32_const(sym->mem_off);
            emit_local_get(tmp);
            emit_mem_store_for_ctype(sym->ctype);
        } else {
            emit_local_tee(sym->idx);
        }
        return;
    }

    if (sym->kind == SYM_GLOBAL) {
        if (sym->is_mem_backed) {
            int tmp = alloc_local(ctype_to_wasm(sym->ctype));
            emit_local_tee(tmp);
            emit_i32_const(sym->mem_off);
            emit_local_get(tmp);
            emit_mem_store_for_ctype(sym->ctype);
        } else {
            emit_global_set(sym->idx);
            emit_global_get(sym->idx);
        }
    }
}

/* Helper to emit store to lvalue */
static void emit_lvalue_store(CType rhs_type) {
    if (last_var_sym) {
        /* Simple variable */
        emit_coerce(rhs_type, last_var_sym->ctype);
        emit_sym_store_and_reload(last_var_sym);
    } else if (lvalue_addr_local >= 0) {
        /* Complex lvalue (array element, dereferenced pointer) */
        /* Stack: [rhs_value] */
        int tmp = alloc_local(ctype_to_wasm(rhs_type));
        emit_local_set(tmp);              /* save rhs */
        emit_local_get(lvalue_addr_local); /* push addr */
        emit_local_get(tmp);              /* push rhs */
        /* Stack: [address, rhs_value] */
        emit_coerce(rhs_type, lvalue_type);
        /* Store */
        emit_mem_store_for_ctype(lvalue_type);
        /* Reload value for result */
        emit_local_get(lvalue_addr_local);
        emit_mem_load_for_ctype(lvalue_type);
    }
}

/* Promote two operands to a common type (C unsigned promotion rules) */
static CType promote(CType a, CType b) {
    if (a == CT_DOUBLE || b == CT_DOUBLE) return CT_DOUBLE;
    if (a == CT_FLOAT  || b == CT_FLOAT)  return CT_FLOAT;
    if (a == CT_ULONG_LONG || b == CT_ULONG_LONG) return CT_ULONG_LONG;
    if (a == CT_LONG_LONG || b == CT_LONG_LONG) return CT_LONG_LONG;
    if (a == CT_UINT || b == CT_UINT) return CT_UINT;
    return CT_INT;
}

/* ---- printf builtin ---- */
static CType compile_printf_call(void) {
    expect(TOK_LPAREN);

    if (tok != TOK_STR_LIT) {
        error_at("printf requires a string literal as first argument");
        /* Skip to closing paren */
        int depth = 1;
        while (tok != TOK_EOF && depth > 0) {
            if (tok == TOK_LPAREN) depth++;
            if (tok == TOK_RPAREN) depth--;
            next_token();
        }
        return CT_INT;
    }

    int fmt_off = add_string(tok_sval, tok_slen);
    next_token();

    /* Collect args into temp locals, then store all at once */
    int arg_locals[16];
    CType arg_types[16];
    int nargs = 0;

    while (accept(TOK_COMMA)) {
        if (nargs >= 16) { error_at("too many printf arguments"); break; }
        CType at = assignment_expr();

        /* Promote float to double per C rules */
        if (at == CT_FLOAT) {
            emit_op(OP_F64_PROMOTE_F32);
            at = CT_DOUBLE;
        }

        /* Save to temp local */
        uint8_t wt = (at == CT_DOUBLE) ? WASM_F64 :
                     (at == CT_LONG_LONG || at == CT_ULONG_LONG) ? WASM_I64 : WASM_I32;
        int loc = alloc_local(wt);
        emit_local_set(loc);
        arg_locals[nargs] = loc;
        arg_types[nargs] = at;
        nargs++;
    }
    expect(TOK_RPAREN);

    /* Now store args sequentially at FMT_BUF_ADDR */
    int arg_offset = 0;
    for (int i = 0; i < nargs; i++) {
        arg_offset = (arg_offset + 3) & ~3;
        if (arg_types[i] == CT_DOUBLE) {
            arg_offset = (arg_offset + 7) & ~7;
            emit_i32_const(FMT_BUF_ADDR + arg_offset);
            emit_local_get(arg_locals[i]);
            buf_byte(CODE, OP_F64_STORE);
            buf_uleb(CODE, 3);
            buf_uleb(CODE, 0);
            arg_offset += 8;
        } else if (arg_types[i] == CT_LONG_LONG || arg_types[i] == CT_ULONG_LONG) {
            arg_offset = (arg_offset + 7) & ~7;
            emit_i32_const(FMT_BUF_ADDR + arg_offset);
            emit_local_get(arg_locals[i]);
            buf_byte(CODE, OP_I64_STORE);
            buf_uleb(CODE, 3);
            buf_uleb(CODE, 0);
            arg_offset += 8;
        } else {
            emit_i32_const(FMT_BUF_ADDR + arg_offset);
            emit_local_get(arg_locals[i]);
            emit_i32_store(0);
            arg_offset += 4;
        }
    }

    /* Call host_printf(fmt_ptr, args_ptr) */
    emit_i32_const(fmt_off);
    emit_i32_const(FMT_BUF_ADDR);
    emit_call(IMP_HOST_PRINTF);

    return CT_INT;
}

/* print("string") builtin — calls print_str(ptr, len) */
static CType compile_print_call(void) {
    expect(TOK_LPAREN);

    if (tok == TOK_STR_LIT) {
        int off = add_string(tok_sval, tok_slen);
        int len = tok_slen;
        next_token();
        expect(TOK_RPAREN);
        emit_i32_const(off);
        emit_i32_const(len);
        emit_call(IMP_PRINT_STR);
        return CT_VOID;
    }

    /* Expression argument — evaluate as string pointer + length */
    CType at = assignment_expr();
    (void)at;
    expect(TOK_RPAREN);
    /* Assume it's a pointer, use strlen at runtime... */
    /* For now, only support string literals in print() */
    error_at("print() only supports string literal arguments in c2wasm");
    return CT_VOID;
}

/* ---- Primary expression ---- */
static CType primary_expr(void) {
    if (tok == TOK_INT_LIT) {
        int is_64 = tok_int_is_64;
        int is_unsigned = tok_int_unsigned;
        int64_t lit_i64 = tok_i64;
        int32_t lit_i32 = (int32_t)tok_ival;
        if (is_64) {
            emit_i64_const(lit_i64);
            next_token();
            expr_set_scalar_type(is_unsigned ? CT_ULONG_LONG : CT_LONG_LONG);
            return is_unsigned ? CT_ULONG_LONG : CT_LONG_LONG;
        }
        emit_i32_const(lit_i32);
        next_token();
        expr_set_scalar_type(is_unsigned ? CT_UINT : CT_INT);
        return is_unsigned ? CT_UINT : CT_INT;
    }
    if (tok == TOK_FLOAT_LIT) {
        emit_f32_const(tok_fval);
        next_token();
        expr_last_is_ptr = 0;
        return CT_FLOAT;
    }
    if (tok == TOK_DOUBLE_LIT) {
        emit_f64_const(tok_dval);
        next_token();
        expr_last_is_ptr = 0;
        return CT_DOUBLE;
    }
    if (tok == TOK_CHAR_LIT) {
        emit_i32_const(tok_ival);
        next_token();
        expr_last_is_ptr = 0;
        return CT_INT;
    }
    if (tok == TOK_STR_LIT) {
        int off = add_string(tok_sval, tok_slen);
        emit_i32_const(off);
        next_token();
        expr_last_is_ptr = 1;
        expr_last_elem_size = 1;
        return CT_CONST_STR;
    }
    if (tok == TOK_LPAREN) {
        next_token();
        /* Check for cast: (type)expr */
        if (is_type_keyword(tok)) {
            CType cast_to = parse_type_spec();
            expect(TOK_RPAREN);
            CType from = unary_expr();
            if (cast_to == CT_VOID) {
                /* (void)expr — drop the value */
                if (from != CT_VOID) emit_drop();
            } else {
                emit_coerce(from, cast_to);
            }
            expr_last_is_ptr = 0;
            return cast_to;
        }
        /* Save last_var_sym across parenthesized expression for postfix ++/-- */
        Symbol *saved_last_var = last_var_sym;
        int saved_lvalue_local = lvalue_addr_local;
        CType t = expr();
        expect(TOK_RPAREN);
        /* If inner expression was a simple variable, preserve it for postfix ops */
        if (saved_last_var && last_var_sym == NULL && saved_lvalue_local < 0) {
            last_var_sym = saved_last_var;
        }
        /* Preserve pointer metadata from inner expression. */
        return t;
    }
    if (tok == TOK_SIZEOF) {
        next_token();
        expect(TOK_LPAREN);
        if (is_type_keyword(tok)) {
            int size_tok = tok;  /* remember original token for stdint sizes */
            CType ct = parse_type_spec();
            int size = 4;  /* default for int, float, and pointers */
            if (type_had_pointer) size = 4;
            else if (ct == CT_VOID) size = 1;
            else if (ct == CT_CHAR) size = 1;
            else if (ct == CT_DOUBLE || ct == CT_LONG_LONG || ct == CT_ULONG_LONG) size = 8;
            else if (size_tok == TOK_INT8 || size_tok == TOK_UINT8) size = 1;
            else if (size_tok == TOK_INT16 || size_tok == TOK_UINT16) size = 2;
            emit_i32_const(size);
            expr_last_is_ptr = 0;
        } else {
            /* sizeof(expr) — parse into temp buffer to get type, discard code */
            FuncCtx *sf = &func_bufs[cur_func];
            int save_fixups = sf->ncall_fixups;
            int save_nlocals = sf->nlocals;
            int save_data_len = data_len;
            int save_nsym = nsym;
            uint8_t save_imp_used[IMP_COUNT];
            memcpy(save_imp_used, imp_used, sizeof(imp_used));
            Buf save_code = sf->code;
            Buf tmp_buf; buf_init(&tmp_buf);
            sf->code = tmp_buf;
            CType ct = expr();
            tmp_buf = sf->code;
            sf->code = save_code;
            sf->ncall_fixups = save_fixups; /* discard any fixups from expr */
            sf->nlocals = save_nlocals;     /* discard any locals from expr */
            data_len = save_data_len;       /* discard any string literals from expr */
            nsym = save_nsym;               /* discard any symbols from expr */
            memcpy(imp_used, save_imp_used, sizeof(imp_used));
            buf_free(&tmp_buf);
            int size = 4;
            if (ct == CT_VOID) size = 1;
            else if (ct == CT_CHAR) size = 1;
            else if (ct == CT_DOUBLE || ct == CT_LONG_LONG || ct == CT_ULONG_LONG) size = 8;
            emit_i32_const(size);
            expr_last_is_ptr = 0;
        }
        expect(TOK_RPAREN);
        return CT_INT;
    }
    if (tok == TOK_NAME) {
        char name[64];
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();

        /* Function call? */
        if (tok == TOK_LPAREN) {
            /* Check for builtins */
            if (strcmp(name, "printf") == 0) return compile_printf_call();
            if (strcmp(name, "print") == 0) return compile_print_call();

            /* WASM-native math builtins (single opcode, no import needed) */
            {
                int opcode = 0;
                CType btype = CT_FLOAT;
                int nargs = 1;
                if      (strcmp(name, "sqrtf") == 0)  opcode = OP_F32_SQRT;
                else if (strcmp(name, "fabsf") == 0)  opcode = OP_F32_ABS;
                else if (strcmp(name, "floorf") == 0) opcode = OP_F32_FLOOR;
                else if (strcmp(name, "ceilf") == 0)  opcode = OP_F32_CEIL;
                else if (strcmp(name, "truncf") == 0) opcode = OP_F32_TRUNC;
                else if (strcmp(name, "fminf") == 0)  { opcode = OP_F32_MIN; nargs = 2; }
                else if (strcmp(name, "fmaxf") == 0)  { opcode = OP_F32_MAX; nargs = 2; }
                else if (strcmp(name, "sqrt") == 0)   { opcode = OP_F64_SQRT;  btype = CT_DOUBLE; }
                else if (strcmp(name, "fabs") == 0)   { opcode = OP_F64_ABS;   btype = CT_DOUBLE; }
                else if (strcmp(name, "floor") == 0)  { opcode = OP_F64_FLOOR; btype = CT_DOUBLE; }
                else if (strcmp(name, "ceil") == 0)   { opcode = OP_F64_CEIL;  btype = CT_DOUBLE; }
                else if (strcmp(name, "trunc") == 0)  { opcode = OP_F64_TRUNC; btype = CT_DOUBLE; }
                else if (strcmp(name, "fmin") == 0)   { opcode = OP_F64_MIN; btype = CT_DOUBLE; nargs = 2; }
                else if (strcmp(name, "fmax") == 0)   { opcode = OP_F64_MAX; btype = CT_DOUBLE; nargs = 2; }
                if (opcode) {
                    next_token(); /* skip '(' */
                    CType at = assignment_expr();
                    emit_coerce(at, btype);
                    if (nargs == 2) {
                        expect(TOK_COMMA);
                        CType at2 = assignment_expr();
                        emit_coerce(at2, btype);
                    }
                    expect(TOK_RPAREN);
                    emit_op(opcode);
                    expr_set_scalar_type(btype);
                    return btype;
                }
            }

            /* Look up function */
            Symbol *fn = find_sym(name);
            if (!fn) {
                error_fmt("undefined function '%s'", name);
                /* Skip arguments */
                next_token();
                int depth = 1;
                while (tok != TOK_EOF && depth > 0) {
                    if (tok == TOK_LPAREN) depth++;
                    if (tok == TOK_RPAREN) depth--;
                    next_token();
                }
                expr_set_scalar_type(CT_INT);
                return CT_INT;
            }

            next_token(); /* skip '(' */
            /* Parse arguments */
            int nargs = 0;
            while (tok != TOK_RPAREN && tok != TOK_EOF) {
                if (nargs > 0) expect(TOK_COMMA);
                CType at = assignment_expr();
                /* Coerce argument to expected type */
                if (nargs < fn->param_count) {
                    CType expected = fn->param_types[nargs];
                    emit_coerce(at, expected);
                }
                nargs++;
            }
            expect(TOK_RPAREN);

            if (nargs != fn->param_count) {
                error_fmt("function '%s' expects %d args, got %d", name, fn->param_count, nargs);
            }

            if (fn->kind == SYM_IMPORT) {
                emit_call(fn->imp_id);
            } else if (fn->kind == SYM_FUNC) {
                emit_call(fn->idx);
            } else {
                error_fmt("'%s' is not callable", name);
            }

            expr_set_scalar_type(fn->ctype);
            return fn->ctype;
        }

        /* Variable reference */
        Symbol *sym = find_sym(name);
        if (!sym) {
            error_fmt("undefined variable '%s'", name);
            emit_i32_const(0);
            expr_set_scalar_type(CT_INT);
            return CT_INT;
        }
        if (sym->kind == SYM_LOCAL) {
            emit_sym_load(sym);
            last_var_sym = sym;  /* Track for potential postfix ++/-- */
            expr_last_is_ptr = type_is_pointer(sym->type_info) || type_is_array(sym->type_info);
            expr_last_elem_size = expr_last_is_ptr ? type_element_size(sym->type_info) : ctype_sizeof(sym->ctype);
            expr_last_has_type = 1;
            expr_last_type = sym->type_info;
        } else if (sym->kind == SYM_GLOBAL) {
            emit_sym_load(sym);
            last_var_sym = sym;  /* Track for potential postfix ++/-- */
            expr_last_is_ptr = type_is_pointer(sym->type_info) || type_is_array(sym->type_info);
            expr_last_elem_size = expr_last_is_ptr ? type_element_size(sym->type_info) : ctype_sizeof(sym->ctype);
            expr_last_has_type = 1;
            expr_last_type = sym->type_info;
        } else if (sym->kind == SYM_IMPORT) {
            /* Calling import as a variable? Shouldn't happen. */
            error_fmt("'%s' is a function, not a variable", name);
            emit_i32_const(0);
            expr_set_scalar_type(CT_INT);
            return CT_INT;
        } else if (sym->kind == SYM_FUNC) {
            /* Function pointer — not supported */
            error_fmt("function pointers not supported");
            emit_i32_const(0);
            expr_set_scalar_type(CT_INT);
            return CT_INT;
        }
        return sym->ctype;
    }

    error_fmt("unexpected token %s in expression", tok_name(tok));
    next_token();
    emit_i32_const(0);
    expr_set_scalar_type(CT_INT);
    return CT_INT;
}

/* Helper: emit load for a symbol */
static void emit_sym_load(Symbol *sym) {
    if (sym->kind == SYM_LOCAL) {
        if (sym->is_mem_backed) {
            emit_i32_const(sym->mem_off);
            emit_mem_load_for_ctype(sym->ctype);
        } else {
            emit_local_get(sym->idx);
        }
    } else if (sym->kind == SYM_GLOBAL) {
        if (sym->is_mem_backed) {
            emit_i32_const(sym->mem_off);
            emit_mem_load_for_ctype(sym->ctype);
        } else {
            emit_global_get(sym->idx);
        }
    }
}

/* Helper: emit store for a symbol */
static void emit_sym_store(Symbol *sym) {
    if (sym->kind == SYM_LOCAL) {
        if (sym->is_mem_backed) {
            int tmp = alloc_local(ctype_to_wasm(sym->ctype));
            emit_local_set(tmp);
            emit_i32_const(sym->mem_off);
            emit_local_get(tmp);
            emit_mem_store_for_ctype(sym->ctype);
        } else {
            emit_local_set(sym->idx);
        }
    } else if (sym->kind == SYM_GLOBAL) {
        if (sym->is_mem_backed) {
            int tmp = alloc_local(ctype_to_wasm(sym->ctype));
            emit_local_set(tmp);
            emit_i32_const(sym->mem_off);
            emit_local_get(tmp);
            emit_mem_store_for_ctype(sym->ctype);
        } else {
            emit_global_set(sym->idx);
        }
    }
}

/* ---- Postfix expressions: a++, a--, subscript ---- */
static CType postfix_expr(void) {
    last_var_sym = NULL;  /* Clear last variable tracking */
    lvalue_addr_local = -1;  /* Clear lvalue tracking */
    CType t = primary_expr();

    while (tok == TOK_INC || tok == TOK_DEC || tok == TOK_LBRACKET) {
        if (tok == TOK_LBRACKET) {
            /* Array subscript: ptr[index] or arr[index] */
            TypeInfo container = expr_last_has_type ? expr_last_type : type_base(t);
            if (type_is_array(container)) container = type_decay(container);
            if (!type_is_pointer(container)) {
                error_at("subscript requires pointer/array expression");
                next_token();
                (void)prec_expr(1);
                expect(TOK_RBRACKET);
                emit_i32_const(0);
                expr_set_scalar_type(CT_INT);
                t = CT_INT;
                continue;
            }
            TypeInfo elem_type = type_deref(container);
            int elem_size = type_sizeof(elem_type);
            next_token();
            CType idx = prec_expr(1);
            expect(TOK_RBRACKET);
            
            /* Compute element address: base + index * sizeof(element) */
            /* For now, assume all elements are 4 bytes (int/pointer) */
            if (idx != CT_INT) emit_coerce(idx, CT_INT);
            emit_i32_const(elem_size);
            emit_op(OP_I32_MUL);
            emit_op(OP_I32_ADD);

            if (type_is_array(elem_type)) {
                /* a[i] where element is array => decay to pointer for chaining a[i][j]. */
                expr_last_has_type = 1;
                expr_last_type = type_decay(elem_type);
                expr_last_is_ptr = 1;
                expr_last_elem_size = type_element_size(expr_last_type);
                t = CT_INT;
                last_var_sym = NULL;
            } else {
                /* Scalar element: keep lvalue address and load value. */
                lvalue_addr_local = alloc_local(WASM_I32);
                emit_local_set(lvalue_addr_local);
                lvalue_type = type_base_ctype(elem_type);

                emit_local_get(lvalue_addr_local);
                emit_mem_load_for_ctype(lvalue_type);
                expr_last_has_type = 1;
                expr_last_type = elem_type;
                expr_last_is_ptr = 0;
                expr_last_elem_size = ctype_sizeof(lvalue_type);
                t = lvalue_type;
                last_var_sym = NULL;
            }
        } else {
            /* Postfix ++ or -- */
            int is_inc = (tok == TOK_INC);
            next_token();
            
            /* Check if primary expression was a simple variable */
            if (last_var_sym) {
                Symbol *sym = last_var_sym;
                if (sym->is_const) {
                    error_fmt("modification of const variable '%s'", sym->name);
                }
                
                /* Postfix: result is the old value, then increment/decrement */
                /* Stack currently has the old value from primary_expr */
                
                /* Generate: old_value, compute new value, store, (old value remains as result) */
                /* We need: load var again, +/- 1, store */
                emit_sym_load(sym);  /* Load value again */
                
                if (sym->ctype == CT_DOUBLE) {
                    emit_f64_const(1.0);
                    emit_op(is_inc ? OP_F64_ADD : OP_F64_SUB);
                } else if (sym->ctype == CT_FLOAT) {
                    emit_f32_const(1.0f);
                    emit_op(is_inc ? OP_F32_ADD : OP_F32_SUB);
                } else if (sym->ctype == CT_LONG_LONG || sym->ctype == CT_ULONG_LONG) {
                    emit_i64_const(1);
                    emit_op(is_inc ? OP_I64_ADD : OP_I64_SUB);
                } else {
                    int step = 1;
                    if (type_is_pointer(sym->type_info))
                        step = type_element_size(sym->type_info);
                    emit_i32_const(step);
                    emit_op(is_inc ? OP_I32_ADD : OP_I32_SUB);
                }
                emit_sym_store(sym);
                /* Old value is still on stack as result */
                t = sym->ctype;
                last_var_sym = NULL;  /* Consumed */
                expr_last_is_ptr = 0;
            } else {
                /* Not a variable - check if we can recover or need to error */
                error_at("postfix ++/-- requires a variable");
                /* Try to continue parsing */
            }
        }
    }
    return t;
}

/* ---- Unary expressions ---- */
static CType unary_expr(void) {
    if (tok == TOK_MINUS) {
        next_token();
        CType t = unary_expr();
        if (t == CT_FLOAT) { emit_op(OP_F32_NEG); return CT_FLOAT; }
        if (t == CT_DOUBLE) { emit_op(OP_F64_NEG); return CT_DOUBLE; }
        if (t == CT_LONG_LONG || t == CT_ULONG_LONG) {
            emit_i64_const(-1);
            emit_op(OP_I64_MUL);
            return t;
        }
        /* i32: val * -1 */
        emit_i32_const(-1);
        emit_op(OP_I32_MUL);
        expr_last_is_ptr = 0;
        return (t == CT_UINT) ? CT_UINT : CT_INT;
    }
    if (tok == TOK_BANG) {
        next_token();
        CType t = unary_expr();
        if (t == CT_FLOAT) { emit_coerce_i32(CT_FLOAT); }
        else if (t == CT_DOUBLE) { emit_coerce_i32(CT_DOUBLE); }
        else if (t == CT_LONG_LONG || t == CT_ULONG_LONG) { emit_op(OP_I64_EQZ); return CT_INT; }
        emit_op(OP_I32_EQZ);
        expr_last_is_ptr = 0;
        return CT_INT;
    }
    if (tok == TOK_TILDE) {
        next_token();
        CType t = unary_expr();
        if (t == CT_LONG_LONG || t == CT_ULONG_LONG) {
            emit_i64_const(-1);
            emit_op(OP_I64_XOR);
            return t;
        }
        if (t != CT_INT && t != CT_UINT) emit_coerce(t, CT_INT);
        emit_i32_const(-1);
        emit_op(OP_I32_XOR);
        expr_last_is_ptr = 0;
        return (t == CT_UINT) ? CT_UINT : CT_INT;
    }
    if (tok == TOK_AMP) {
        /* Address-of operator & */
        next_token();
        if (tok == TOK_STAR) {
            /* &*p => p */
            next_token();
            (void)unary_expr();
            if (lvalue_addr_local < 0) {
                error_at("expected addressable expression after &*");
                emit_i32_const(0);
                expr_set_scalar_type(CT_INT);
                return CT_INT;
            }
            emit_drop();
            emit_local_get(lvalue_addr_local);
            expr_last_is_ptr = 1;
            expr_last_has_type = 1;
            expr_last_type = type_pointer(type_base(lvalue_type));
            expr_last_elem_size = type_element_size(expr_last_type);
            return CT_INT;
        }

        if (tok != TOK_NAME) {
            error_at("expected variable after &");
            emit_i32_const(0);
            expr_set_scalar_type(CT_INT);
            return CT_INT;
        }

        char name[64];
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();
        Symbol *sym = find_sym(name);
        if (!sym) {
            error_fmt("undefined variable '%s'", name);
            emit_i32_const(0);
            expr_set_scalar_type(CT_INT);
            return CT_INT;
        }

        TypeInfo cur = sym->type_info;

        if (type_is_array(cur)) {
            if (sym->kind == SYM_LOCAL) {
                emit_local_get(sym->idx);
            } else if (sym->kind == SYM_GLOBAL) {
                emit_i32_const(sym->init_ival);
            } else {
                error_fmt("cannot take address of '%s'", name);
                emit_i32_const(0);
                expr_set_scalar_type(CT_INT);
                return CT_INT;
            }
        } else if (type_is_pointer(cur)) {
            if (sym->kind == SYM_LOCAL) {
                ensure_local_mem_backed(sym);
                emit_i32_const(sym->mem_off);
            }
            else if (sym->kind == SYM_GLOBAL) {
                if (sym->is_mem_backed) {
                    emit_i32_const(sym->mem_off);
                } else {
                    error_fmt("address-of global pointer variable '%s' is not supported", name);
                    emit_i32_const(0);
                    expr_set_scalar_type(CT_INT);
                    return CT_INT;
                }
            }
            else {
                error_fmt("cannot take address of '%s'", name);
                emit_i32_const(0);
                expr_set_scalar_type(CT_INT);
                return CT_INT;
            }
        } else {
            if (sym->kind == SYM_GLOBAL) {
                if (sym->is_mem_backed) {
                    emit_i32_const(sym->mem_off);
                } else {
                    error_fmt("address-of global scalar variable '%s' is not supported", name);
                    emit_i32_const(0);
                    expr_set_scalar_type(CT_INT);
                    return CT_INT;
                }
            } else {
                ensure_local_mem_backed(sym);
                emit_i32_const(sym->mem_off);
            }
        }

        /* Support &arr[i][j] style address expressions directly. */
        while (tok == TOK_LBRACKET) {
            TypeInfo container = cur;
            if (type_is_array(container)) container = type_decay(container);
            if (!type_is_pointer(container)) {
                error_at("subscript requires pointer/array expression");
                break;
            }
            TypeInfo elem = type_deref(container);
            int elem_size = type_sizeof(elem);

            next_token();
            CType idx = prec_expr(1);
            expect(TOK_RBRACKET);
            if (idx != CT_INT) emit_coerce(idx, CT_INT);
            emit_i32_const(elem_size);
            emit_op(OP_I32_MUL);
            emit_op(OP_I32_ADD);
            cur = elem;
        }

        expr_last_is_ptr = 1;
        expr_last_has_type = 1;
        expr_last_type = type_pointer(cur);
        expr_last_elem_size = type_element_size(expr_last_type);
        return CT_INT;  /* Pointers are i32 */
    }
    if (tok == TOK_STAR) {
        /* Dereference operator * */
        next_token();
        lvalue_addr_local = -1;  /* Clear previous lvalue */
        CType t = unary_expr();
        TypeInfo container = expr_last_has_type ? expr_last_type : type_pointer(type_base(t));
        if (type_is_array(container)) container = type_decay(container);
        if (!type_is_pointer(container)) {
            /* Fallback: legacy pointer-as-i32 behavior. */
            if (t == CT_INT || t == CT_UINT || t == CT_CONST_STR) {
                container = type_pointer(type_base(CT_INT));
            } else {
                error_at("cannot dereference non-pointer expression");
                emit_i32_const(0);
                expr_set_scalar_type(CT_INT);
                return CT_INT;
            }
        }
        TypeInfo elem = type_deref(container);
        CType elem_ct = type_base_ctype(elem);
        /* t should be a pointer type (int for now) */
        /* Save address for potential lvalue assignment */
        lvalue_addr_local = alloc_local(WASM_I32);
        emit_local_set(lvalue_addr_local);
        lvalue_type = elem_ct;
        last_var_sym = NULL;  /* Not a simple variable */
        expr_last_is_ptr = 0;
        expr_last_has_type = 1;
        expr_last_type = elem;
        expr_last_elem_size = type_is_pointer(elem) || type_is_array(elem) ? type_element_size(elem) : ctype_sizeof(elem_ct);
        /* Load from the computed address */
        emit_local_get(lvalue_addr_local);
        emit_mem_load_for_ctype(elem_ct);
        return elem_ct;
    }
    if (tok == TOK_INC || tok == TOK_DEC) {
        /* Pre-increment/decrement */
        int is_inc = (tok == TOK_INC);
        next_token();
        if (tok != TOK_NAME) {
            error_at("expected variable after ++/--");
            emit_i32_const(0);
            return CT_INT;
        }
        char name[64];
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        next_token();
        Symbol *sym = find_sym(name);
        if (!sym) { error_fmt("undefined variable '%s'", name); emit_i32_const(0); return CT_INT; }
        if (sym->is_const) error_fmt("modification of const variable '%s'", name);
        emit_sym_load(sym);
        if (sym->ctype == CT_DOUBLE) {
            emit_f64_const(1.0);
            emit_op(is_inc ? OP_F64_ADD : OP_F64_SUB);
        } else if (sym->ctype == CT_FLOAT) {
            emit_f32_const(1.0f);
            emit_op(is_inc ? OP_F32_ADD : OP_F32_SUB);
        } else if (sym->ctype == CT_LONG_LONG || sym->ctype == CT_ULONG_LONG) {
            emit_i64_const(1);
            emit_op(is_inc ? OP_I64_ADD : OP_I64_SUB);
        } else {
            int step = 1;
            if (type_is_pointer(sym->type_info))
                step = type_element_size(sym->type_info);
            emit_i32_const(step);
            emit_op(is_inc ? OP_I32_ADD : OP_I32_SUB);
        }
        /* Tee so the new value stays on stack and is stored */
        emit_sym_store_and_reload(sym);
        expr_last_is_ptr = 0;
        return sym->ctype;
    }
    if (tok == TOK_PLUS) {
        next_token();
        return unary_expr();
    }
    return postfix_expr();
}

/* ---- Binary operators with precedence climbing ---- */

/* Precedence levels (higher = tighter binding):
 * 1: ||
 * 2: &&
 * 3: |
 * 4: ^
 * 5: &
 * 6: == !=
 * 7: < > <= >=
 * 8: << >>
 * 9: + -
 * 10: * / %
 */

static int get_prec(int t) {
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

static CType prec_expr_tail(CType left, int min_prec) {
    while (get_prec(tok) >= min_prec) {
        /* Catch void used as operand */
        if (left == CT_VOID) {
            error_at("void expression used as operand");
            emit_i32_const(0);
            left = CT_INT;
        }
        int op = tok;
        int prec = get_prec(op);
        next_token();

        /* Short-circuit for && and || */
        if (op == TOK_AND_AND) {
            /* left && right: if left is 0, result is 0 without evaluating right */
            emit_coerce(left, CT_INT);
            emit_if_i32();
            CType right = prec_expr(prec + 1);
            emit_coerce(right, CT_INT);
            /* Convert to boolean: if nonzero → 1 */
            emit_op(OP_I32_CONST); buf_sleb(CODE, 0);
            emit_op(OP_I32_NE);
            emit_else();
            emit_i32_const(0);
            emit_end();
            left = CT_INT;
            continue;
        }
        if (op == TOK_OR_OR) {
            emit_coerce(left, CT_INT);
            emit_if_i32();
            emit_i32_const(1);
            emit_else();
            CType right = prec_expr(prec + 1);
            emit_coerce(right, CT_INT);
            emit_op(OP_I32_CONST); buf_sleb(CODE, 0);
            emit_op(OP_I32_NE);
            emit_end();
            left = CT_INT;
            continue;
        }

        int left_is_ptr = expr_last_is_ptr;
        int left_elem_size = expr_last_elem_size;
        CType right = prec_expr(prec + 1);
        int right_is_ptr = expr_last_is_ptr;
        int right_elem_size = expr_last_elem_size;
        if (right == CT_VOID) {
            error_at("void expression used as operand");
            emit_i32_const(0);
            right = CT_INT;
            right_is_ptr = 0;
        }

        /* Pointer arithmetic (lightweight): ptr +/- int, int + ptr, ptr - ptr. */
        if ((op == TOK_PLUS || op == TOK_MINUS) && (left_is_ptr || right_is_ptr)) {
            if (left_is_ptr && !right_is_ptr) {
                emit_coerce(right, CT_INT);
                emit_i32_const(left_elem_size > 0 ? left_elem_size : 4);
                emit_op(OP_I32_MUL);
                emit_op(op == TOK_PLUS ? OP_I32_ADD : OP_I32_SUB);
                left = CT_INT;
                expr_last_is_ptr = 1;
                expr_last_elem_size = left_elem_size > 0 ? left_elem_size : 4;
                continue;
            }
            if (!left_is_ptr && right_is_ptr && op == TOK_PLUS) {
                int tmp_ptr = alloc_local(WASM_I32);
                emit_local_set(tmp_ptr); /* pops ptr */
                emit_coerce(left, CT_INT);
                emit_i32_const(right_elem_size > 0 ? right_elem_size : 4);
                emit_op(OP_I32_MUL);
                emit_local_get(tmp_ptr);
                emit_op(OP_I32_ADD);
                left = CT_INT;
                expr_last_is_ptr = 1;
                expr_last_elem_size = right_elem_size > 0 ? right_elem_size : 4;
                continue;
            }
            if (left_is_ptr && right_is_ptr && op == TOK_MINUS) {
                emit_op(OP_I32_SUB);
                emit_i32_const(left_elem_size > 0 ? left_elem_size : 4);
                emit_op(OP_I32_DIV_S);
                left = CT_INT;
                expr_last_is_ptr = 0;
                continue;
            }
            error_at("unsupported pointer arithmetic expression");
            left = CT_INT;
            expr_last_is_ptr = 0;
            continue;
        }
        CType result = promote(left, right);

        /* Bitwise and shift ops: coerce each operand from its own type to i32 */
        int is_bitwise = (op == TOK_AMP || op == TOK_PIPE || op == TOK_CARET ||
                          op == TOK_LSHIFT || op == TOK_RSHIFT);
        if (is_bitwise && (result == CT_FLOAT || result == CT_DOUBLE)) {
            /* right is on top of stack, left is below */
            emit_coerce_i32(right);
            int tmp = alloc_local(WASM_I32);
            emit_local_set(tmp);
            emit_coerce_i32(left);
            emit_local_get(tmp);
            /* Preserve unsigned if either operand was unsigned */
            result = (ctype_is_unsigned(left) || ctype_is_unsigned(right)) ? CT_UINT : CT_INT;
        } else {
        /* Coerce both sides to common type */
        if (result == CT_FLOAT) {
            if (right != CT_FLOAT && left == CT_FLOAT) {
                emit_coerce(right, CT_FLOAT);
            } else if (left != CT_FLOAT && right == CT_FLOAT) {
                int tmp = alloc_local(WASM_F32);
                emit_local_set(tmp);
                emit_coerce(left, CT_FLOAT);
                emit_local_get(tmp);
            }
        } else if (result == CT_DOUBLE) {
            if (right != CT_DOUBLE && left == CT_DOUBLE) {
                emit_promote_f64(right);
            } else if (left != CT_DOUBLE && right == CT_DOUBLE) {
                int tmp = alloc_local(WASM_F64);
                emit_local_set(tmp);
                emit_promote_f64(left);
                emit_local_get(tmp);
            }
        } else if (result == CT_LONG_LONG || result == CT_ULONG_LONG) {
            int left_is_i64 = (left == CT_LONG_LONG || left == CT_ULONG_LONG);
            int right_is_i64 = (right == CT_LONG_LONG || right == CT_ULONG_LONG);
            if (!right_is_i64 && left_is_i64) {
                emit_coerce_i64(right);
            } else if (!left_is_i64 && right_is_i64) {
                int tmp = alloc_local(WASM_I64);
                emit_local_set(tmp);
                emit_coerce_i64(left);
                emit_local_get(tmp);
            }
        }
        } /* end else (non-bitwise) */

        switch (op) {
        case TOK_PLUS:
            emit_op(result == CT_DOUBLE ? OP_F64_ADD : result == CT_FLOAT ? OP_F32_ADD :
                    (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_ADD : OP_I32_ADD);
            break;
        case TOK_MINUS:
            emit_op(result == CT_DOUBLE ? OP_F64_SUB : result == CT_FLOAT ? OP_F32_SUB :
                    (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_SUB : OP_I32_SUB);
            break;
        case TOK_STAR:
            emit_op(result == CT_DOUBLE ? OP_F64_MUL : result == CT_FLOAT ? OP_F32_MUL :
                    (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_MUL : OP_I32_MUL);
            break;
        case TOK_SLASH:
            if (result == CT_DOUBLE) emit_op(OP_F64_DIV);
            else if (result == CT_FLOAT) emit_op(OP_F32_DIV);
            else if (result == CT_ULONG_LONG) emit_op(OP_I64_DIV_U);
            else if (result == CT_LONG_LONG) emit_op(OP_I64_DIV_S);
            else if (result == CT_UINT) emit_op(OP_I32_DIV_U);
            else emit_op(OP_I32_DIV_S);
            break;
        case TOK_PERCENT:
            if (result == CT_DOUBLE) {
                emit_call(IMP_FMOD);
            } else if (result == CT_FLOAT) {
                emit_call(IMP_FMODF);
            } else if (result == CT_ULONG_LONG) {
                emit_op(OP_I64_REM_U);
            } else if (result == CT_LONG_LONG) {
                emit_op(OP_I64_REM_S);
            } else if (result == CT_UINT) {
                emit_op(OP_I32_REM_U);
            } else {
                emit_op(OP_I32_REM_S);
            }
            break;
        case TOK_EQ:
            emit_op(result == CT_DOUBLE ? OP_F64_EQ : result == CT_FLOAT ? OP_F32_EQ :
                    (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_EQ : OP_I32_EQ);
            result = CT_INT;
            break;
        case TOK_NE:
            emit_op(result == CT_DOUBLE ? OP_F64_NE : result == CT_FLOAT ? OP_F32_NE :
                    (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_NE : OP_I32_NE);
            result = CT_INT;
            break;
        case TOK_LT:
            if (result == CT_DOUBLE) emit_op(OP_F64_LT);
            else if (result == CT_FLOAT) emit_op(OP_F32_LT);
            else if (result == CT_ULONG_LONG) emit_op(OP_I64_LT_U);
            else if (result == CT_LONG_LONG) emit_op(OP_I64_LT_S);
            else if (result == CT_UINT) emit_op(OP_I32_LT_U);
            else emit_op(OP_I32_LT_S);
            result = CT_INT;
            break;
        case TOK_GT:
            if (result == CT_DOUBLE) emit_op(OP_F64_GT);
            else if (result == CT_FLOAT) emit_op(OP_F32_GT);
            else if (result == CT_ULONG_LONG) emit_op(OP_I64_GT_U);
            else if (result == CT_LONG_LONG) emit_op(OP_I64_GT_S);
            else if (result == CT_UINT) emit_op(OP_I32_GT_U);
            else emit_op(OP_I32_GT_S);
            result = CT_INT;
            break;
        case TOK_LE:
            if (result == CT_DOUBLE) emit_op(OP_F64_LE);
            else if (result == CT_FLOAT) emit_op(OP_F32_LE);
            else if (result == CT_ULONG_LONG) emit_op(OP_I64_LE_U);
            else if (result == CT_LONG_LONG) emit_op(OP_I64_LE_S);
            else if (result == CT_UINT) emit_op(OP_I32_LE_U);
            else emit_op(OP_I32_LE_S);
            result = CT_INT;
            break;
        case TOK_GE:
            if (result == CT_DOUBLE) emit_op(OP_F64_GE);
            else if (result == CT_FLOAT) emit_op(OP_F32_GE);
            else if (result == CT_ULONG_LONG) emit_op(OP_I64_GE_U);
            else if (result == CT_LONG_LONG) emit_op(OP_I64_GE_S);
            else if (result == CT_UINT) emit_op(OP_I32_GE_U);
            else emit_op(OP_I32_GE_S);
            result = CT_INT;
            break;
        case TOK_AMP:    emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_AND : OP_I32_AND); break;
        case TOK_PIPE:   emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_OR  : OP_I32_OR);  break;
        case TOK_CARET:  emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_XOR : OP_I32_XOR); break;
        case TOK_LSHIFT: emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_SHL : OP_I32_SHL); break;
        case TOK_RSHIFT:
            if (result == CT_ULONG_LONG) emit_op(OP_I64_SHR_U);
            else if (result == CT_LONG_LONG) emit_op(OP_I64_SHR_S);
            else if (result == CT_UINT) emit_op(OP_I32_SHR_U);
            else emit_op(OP_I32_SHR_S);
            break;
        }

        left = result;
        expr_last_is_ptr = 0;
    }
    return left;
}

static CType prec_expr(int min_prec) {
    CType left = unary_expr();
    return prec_expr_tail(left, min_prec);
}

/* ---- Ternary and assignment ---- */

CType assignment_expr(void) {
    /* Fast path for simple name-based assignment forms to avoid
     * pre-loading the lvalue on stack. */
    if (tok == TOK_NAME) {
        char name[64];
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        int pt = peek_token();

        /* name = expr */
        if (pt == TOK_ASSIGN) {
            next_token(); /* skip name */
            next_token(); /* skip '=' */
            Symbol *sym = find_sym(name);
            if (!sym) { error_fmt("undefined variable '%s'", name); return CT_INT; }
            if (type_is_array(sym->type_info)) { error_fmt("assignment to array '%s' is not allowed", name); return CT_INT; }
            if (sym->is_const) error_fmt("assignment to const variable '%s'", name);
            CType rhs = assignment_expr();
            emit_coerce(rhs, sym->ctype);
            emit_sym_store_and_reload(sym);
            expr_last_is_ptr = type_is_pointer(sym->type_info) || type_is_array(sym->type_info);
            expr_last_elem_size = expr_last_is_ptr ? type_element_size(sym->type_info) : ctype_sizeof(sym->ctype);
            return sym->ctype;
        }

        /* name OP= expr */
        if (pt >= TOK_PLUS_EQ && pt <= TOK_RSHIFT_EQ) {
            next_token(); /* skip name */
            int aop = tok;
            next_token(); /* skip op= */
            Symbol *sym = find_sym(name);
            if (!sym) { error_fmt("undefined variable '%s'", name); return CT_INT; }
            if (type_is_array(sym->type_info)) { error_fmt("compound assignment to array '%s' is not allowed", name); return CT_INT; }
            if (sym->is_const) error_fmt("assignment to const variable '%s'", name);

            emit_sym_load(sym);
            CType rhs = assignment_expr();
            CType result = promote(sym->ctype, rhs);

            if (result == CT_FLOAT && rhs != CT_FLOAT) emit_coerce(rhs, CT_FLOAT);
            if (result == CT_FLOAT && sym->ctype != CT_FLOAT) {
                int tmp = alloc_local(ctype_to_wasm(result));
                emit_local_set(tmp); emit_coerce(sym->ctype, result); emit_local_get(tmp);
            }
            if (result == CT_DOUBLE && rhs != CT_DOUBLE) emit_promote_f64(rhs);
            if (result == CT_DOUBLE && sym->ctype != CT_DOUBLE) {
                int tmp = alloc_local(WASM_F64);
                emit_local_set(tmp); emit_promote_f64(sym->ctype); emit_local_get(tmp);
            }
            if ((result == CT_LONG_LONG || result == CT_ULONG_LONG) && rhs != CT_LONG_LONG && rhs != CT_ULONG_LONG)
                emit_coerce_i64(rhs);
            if ((result == CT_LONG_LONG || result == CT_ULONG_LONG) && sym->ctype != CT_LONG_LONG && sym->ctype != CT_ULONG_LONG) {
                int tmp = alloc_local(WASM_I64);
                emit_local_set(tmp); emit_coerce_i64(sym->ctype); emit_local_get(tmp);
            }

            if (aop == TOK_AMP_EQ || aop == TOK_PIPE_EQ || aop == TOK_CARET_EQ || aop == TOK_LSHIFT_EQ || aop == TOK_RSHIFT_EQ) {
                if (result == CT_FLOAT || result == CT_DOUBLE) {
                    emit_coerce_i32(result);
                    int tmp = alloc_local(WASM_I32);
                    emit_local_set(tmp); emit_coerce_i32(result); emit_local_get(tmp);
                    result = CT_INT;
                }
            }

            switch (aop) {
            case TOK_PLUS_EQ:   emit_op(result == CT_DOUBLE ? OP_F64_ADD : result == CT_FLOAT ? OP_F32_ADD : (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_ADD : OP_I32_ADD); break;
            case TOK_MINUS_EQ:  emit_op(result == CT_DOUBLE ? OP_F64_SUB : result == CT_FLOAT ? OP_F32_SUB : (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_SUB : OP_I32_SUB); break;
            case TOK_STAR_EQ:   emit_op(result == CT_DOUBLE ? OP_F64_MUL : result == CT_FLOAT ? OP_F32_MUL : (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_MUL : OP_I32_MUL); break;
            case TOK_SLASH_EQ:  if (result == CT_DOUBLE) emit_op(OP_F64_DIV); else if (result == CT_FLOAT) emit_op(OP_F32_DIV); else if (result == CT_ULONG_LONG) emit_op(OP_I64_DIV_U); else if (result == CT_LONG_LONG) emit_op(OP_I64_DIV_S); else if (result == CT_UINT) emit_op(OP_I32_DIV_U); else emit_op(OP_I32_DIV_S); break;
            case TOK_PERCENT_EQ: if (result == CT_DOUBLE) emit_call(IMP_FMOD); else if (result == CT_FLOAT) emit_call(IMP_FMODF); else if (result == CT_ULONG_LONG) emit_op(OP_I64_REM_U); else if (result == CT_LONG_LONG) emit_op(OP_I64_REM_S); else if (result == CT_UINT) emit_op(OP_I32_REM_U); else emit_op(OP_I32_REM_S); break;
            case TOK_AMP_EQ:    emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_AND : OP_I32_AND); break;
            case TOK_PIPE_EQ:   emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_OR : OP_I32_OR); break;
            case TOK_CARET_EQ:  emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_XOR : OP_I32_XOR); break;
            case TOK_LSHIFT_EQ: emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_SHL : OP_I32_SHL); break;
            case TOK_RSHIFT_EQ: if (result == CT_ULONG_LONG) emit_op(OP_I64_SHR_U); else if (result == CT_LONG_LONG) emit_op(OP_I64_SHR_S); else if (result == CT_UINT) emit_op(OP_I32_SHR_U); else emit_op(OP_I32_SHR_S); break;
            }

            if (result != sym->ctype) emit_coerce(result, sym->ctype);
            emit_sym_store_and_reload(sym);
            expr_last_is_ptr = type_is_pointer(sym->type_info) || type_is_array(sym->type_info);
            expr_last_elem_size = expr_last_is_ptr ? type_element_size(sym->type_info) : ctype_sizeof(sym->ctype);
            return sym->ctype;
        }

        /* name++ / name-- */
        if (pt == TOK_INC || pt == TOK_DEC) {
            next_token(); /* skip name */
            int is_inc = (tok == TOK_INC);
            next_token(); /* skip ++/-- */
            Symbol *sym = find_sym(name);
            if (!sym) { error_fmt("undefined variable '%s'", name); emit_i32_const(0); return CT_INT; }
            if (type_is_array(sym->type_info)) { error_fmt("increment/decrement on array '%s' is not allowed", name); emit_i32_const(0); return CT_INT; }
            if (sym->is_const) error_fmt("modification of const variable '%s'", name);
            emit_sym_load(sym); /* old value (result) */
            emit_sym_load(sym); /* compute new */
            if (sym->ctype == CT_DOUBLE) { emit_f64_const(1.0); emit_op(is_inc ? OP_F64_ADD : OP_F64_SUB); }
            else if (sym->ctype == CT_FLOAT) { emit_f32_const(1.0f); emit_op(is_inc ? OP_F32_ADD : OP_F32_SUB); }
            else if (sym->ctype == CT_LONG_LONG || sym->ctype == CT_ULONG_LONG) { emit_i64_const(1); emit_op(is_inc ? OP_I64_ADD : OP_I64_SUB); }
            else {
                int step = 1;
                if (type_is_pointer(sym->type_info))
                    step = type_element_size(sym->type_info);
                emit_i32_const(step); emit_op(is_inc ? OP_I32_ADD : OP_I32_SUB);
            }
            emit_sym_store(sym);
            expr_last_is_ptr = 0;
            return sym->ctype;
        }
    }

    /* Save lvalue state before parsing potential lvalue */
    int saved_lvalue_local = lvalue_addr_local;
    Symbol *saved_last_var = last_var_sym;
    CType saved_lvalue_type = lvalue_type;
    
    /* Clear lvalue tracking for new expression */
    lvalue_addr_local = -1;
    last_var_sym = NULL;
    
    /* Parse the left-hand side (could be a simple variable, subscript, or dereference) */
    CType lhs_type = unary_expr();
    
    /* Check for assignment operators */
    if (tok == TOK_ASSIGN) {
        /* Simple assignment: lvalue = expr */
        if (last_var_sym == NULL && lvalue_addr_local < 0) {
            error_at("left side of assignment is not an lvalue");
            next_token(); /* skip = */
            assignment_expr(); /* parse rhs for error recovery */
            return CT_INT;
        }

        /* Drop the old value loaded by unary_expr() */
        if (lhs_type != CT_VOID) emit_drop();

        next_token(); /* skip '=' */
        CType rhs = assignment_expr();
        
        /* Perform assignment using lvalue tracking */
        if (last_var_sym) {
            /* Simple variable */
            if (last_var_sym->is_const) error_fmt("assignment to const variable '%s'", last_var_sym->name);
            emit_coerce(rhs, last_var_sym->ctype);
            emit_sym_store_and_reload(last_var_sym);
            expr_last_is_ptr = type_is_pointer(last_var_sym->type_info) || type_is_array(last_var_sym->type_info);
            expr_last_elem_size = expr_last_is_ptr ? type_element_size(last_var_sym->type_info) : ctype_sizeof(last_var_sym->ctype);
            return last_var_sym->ctype;
        } else if (lvalue_addr_local >= 0) {
            /* Complex lvalue (array element or dereferenced pointer) */
            emit_lvalue_store(rhs);
            expr_last_is_ptr = 0;
            return lvalue_type;
        }
    }

    /* Compound assignment for complex lvalues: arr[i] += expr, *p -= expr, etc. */
    if (tok >= TOK_PLUS_EQ && tok <= TOK_RSHIFT_EQ &&
        (last_var_sym != NULL || lvalue_addr_local >= 0)) {
        int aop = tok;
        next_token(); /* skip op= */

        /* Stack: [old_value from unary_expr] */
        /* Parse rhs */
        CType rhs = assignment_expr();

        CType var_type = last_var_sym ? last_var_sym->ctype : lvalue_type;
        CType result = promote(var_type, rhs);

        /* Coerce operands to common type */
        if (result == CT_FLOAT && rhs != CT_FLOAT) emit_coerce(rhs, CT_FLOAT);
        if (result == CT_FLOAT && var_type != CT_FLOAT) {
            int tmp = alloc_local(ctype_to_wasm(result));
            emit_local_set(tmp); emit_coerce(var_type, result); emit_local_get(tmp);
        }
        if (result == CT_DOUBLE && rhs != CT_DOUBLE) emit_promote_f64(rhs);
        if (result == CT_DOUBLE && var_type != CT_DOUBLE) {
            int tmp = alloc_local(WASM_F64);
            emit_local_set(tmp); emit_promote_f64(var_type); emit_local_get(tmp);
        }
        if ((result == CT_LONG_LONG || result == CT_ULONG_LONG) && rhs != CT_LONG_LONG && rhs != CT_ULONG_LONG)
            emit_coerce_i64(rhs);
        if ((result == CT_LONG_LONG || result == CT_ULONG_LONG) && var_type != CT_LONG_LONG && var_type != CT_ULONG_LONG) {
            int tmp = alloc_local(WASM_I64);
            emit_local_set(tmp); emit_coerce_i64(var_type); emit_local_get(tmp);
        }

        /* Bitwise compound ops coerce float/double to int */
        if (aop == TOK_AMP_EQ || aop == TOK_PIPE_EQ || aop == TOK_CARET_EQ || aop == TOK_LSHIFT_EQ || aop == TOK_RSHIFT_EQ) {
            if (result == CT_FLOAT || result == CT_DOUBLE) {
                emit_coerce_i32(result);
                int tmp = alloc_local(WASM_I32);
                emit_local_set(tmp); emit_coerce_i32(result); emit_local_get(tmp);
                result = CT_INT;
            }
        }

        /* Apply operator */
        switch (aop) {
        case TOK_PLUS_EQ:   emit_op(result == CT_DOUBLE ? OP_F64_ADD : result == CT_FLOAT ? OP_F32_ADD : (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_ADD : OP_I32_ADD); break;
        case TOK_MINUS_EQ:  emit_op(result == CT_DOUBLE ? OP_F64_SUB : result == CT_FLOAT ? OP_F32_SUB : (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_SUB : OP_I32_SUB); break;
        case TOK_STAR_EQ:   emit_op(result == CT_DOUBLE ? OP_F64_MUL : result == CT_FLOAT ? OP_F32_MUL : (result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_MUL : OP_I32_MUL); break;
        case TOK_SLASH_EQ:  if (result == CT_DOUBLE) emit_op(OP_F64_DIV); else if (result == CT_FLOAT) emit_op(OP_F32_DIV); else if (result == CT_ULONG_LONG) emit_op(OP_I64_DIV_U); else if (result == CT_LONG_LONG) emit_op(OP_I64_DIV_S); else if (result == CT_UINT) emit_op(OP_I32_DIV_U); else emit_op(OP_I32_DIV_S); break;
        case TOK_PERCENT_EQ: if (result == CT_DOUBLE) emit_call(IMP_FMOD); else if (result == CT_FLOAT) emit_call(IMP_FMODF); else if (result == CT_ULONG_LONG) emit_op(OP_I64_REM_U); else if (result == CT_LONG_LONG) emit_op(OP_I64_REM_S); else if (result == CT_UINT) emit_op(OP_I32_REM_U); else emit_op(OP_I32_REM_S); break;
        case TOK_AMP_EQ:    emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_AND : OP_I32_AND); break;
        case TOK_PIPE_EQ:   emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_OR : OP_I32_OR); break;
        case TOK_CARET_EQ:  emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_XOR : OP_I32_XOR); break;
        case TOK_LSHIFT_EQ: emit_op((result == CT_LONG_LONG || result == CT_ULONG_LONG) ? OP_I64_SHL : OP_I32_SHL); break;
        case TOK_RSHIFT_EQ: if (result == CT_ULONG_LONG) emit_op(OP_I64_SHR_U); else if (result == CT_LONG_LONG) emit_op(OP_I64_SHR_S); else if (result == CT_UINT) emit_op(OP_I32_SHR_U); else emit_op(OP_I32_SHR_S); break;
        }

        /* Coerce result to variable type */
        if (result != var_type) emit_coerce(result, var_type);

        /* Store result */
        if (last_var_sym) {
            if (last_var_sym->is_const) error_fmt("assignment to const variable '%s'", last_var_sym->name);
            emit_sym_store_and_reload(last_var_sym);
            expr_last_is_ptr = type_is_pointer(last_var_sym->type_info) || type_is_array(last_var_sym->type_info);
            expr_last_elem_size = expr_last_is_ptr ? type_element_size(last_var_sym->type_info) : ctype_sizeof(var_type);
            return var_type;
        } else {
            /* Complex lvalue */
            emit_lvalue_store(var_type);
            expr_last_is_ptr = 0;
            return lvalue_type;
        }
    }

    /* Not an assignment — continue parsing from already-read lhs */
    CType t = prec_expr_tail(lhs_type, 1);

    /* Restore lvalue state after expression parse */
    lvalue_addr_local = saved_lvalue_local;
    last_var_sym = saved_last_var;
    lvalue_type = saved_lvalue_type;

    /* Ternary: expr ? expr : expr
     * Compile both branches into temp buffers first to determine
     * the common result type, then emit the if with correct block type. */
    if (tok == TOK_QUESTION) {
        next_token();
        emit_coerce(t, CT_INT);

        FuncCtx *ternary_f = &func_bufs[cur_func];

        /* Compile then-branch into temp buffer */
        int then_fixups_start = ternary_f->ncall_fixups;
        Buf save_code = ternary_f->code;
        Buf then_buf; buf_init(&then_buf);
        ternary_f->code = then_buf;
        CType then_t = expr();
        then_buf = ternary_f->code;
        ternary_f->code = save_code;
        int then_fixups_end = ternary_f->ncall_fixups;

        expect(TOK_COLON);

        /* Compile else-branch into temp buffer */
        int else_fixups_start = ternary_f->ncall_fixups;
        save_code = ternary_f->code;
        Buf else_buf; buf_init(&else_buf);
        ternary_f->code = else_buf;
        CType else_t = assignment_expr();
        else_buf = ternary_f->code;
        ternary_f->code = save_code;
        int else_fixups_end = ternary_f->ncall_fixups;

        /* Determine common result type */
        CType result = promote(then_t, else_t);
        if (then_t == CT_VOID && else_t == CT_VOID) result = CT_VOID;
        else if (then_t == CT_VOID || else_t == CT_VOID) {
            error_at("ternary: one branch is void, the other is not");
            result = CT_VOID;
        }

        /* Emit if with correct block type */
        if (result == CT_DOUBLE) emit_if_f64();
        else if (result == CT_FLOAT) emit_if_f32();
        else if (result == CT_LONG_LONG || result == CT_ULONG_LONG) emit_if_i64();
        else if (result == CT_VOID) emit_if_void();
        else emit_if_i32();

        /* Splice then-branch + coercion */
        {
            int splice_off = ternary_f->code.len;
            buf_bytes(CODE, then_buf.data, then_buf.len);
            for (int fx = then_fixups_start; fx < then_fixups_end; fx++)
                ternary_f->call_fixups[fx] += splice_off;
        }
        buf_free(&then_buf);
        emit_coerce(then_t, result);

        /* Emit else + splice else-branch + coercion */
        emit_else();
        {
            int splice_off = ternary_f->code.len;
            buf_bytes(CODE, else_buf.data, else_buf.len);
            for (int fx = else_fixups_start; fx < else_fixups_end; fx++)
                ternary_f->call_fixups[fx] += splice_off;
        }
        buf_free(&else_buf);
        emit_coerce(else_t, result);

        emit_end();
        return result;
    }

    return t;
}

/* Top-level expression with comma operator support */
CType expr(void) {
    CType t = assignment_expr();
    while (accept(TOK_COMMA)) {
        if (t != CT_VOID) emit_drop();
        t = assignment_expr();
    }
    return t;
}
