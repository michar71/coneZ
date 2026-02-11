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

/* Promote two operands to a common type */
static CType promote(CType a, CType b) {
    if (a == CT_DOUBLE || b == CT_DOUBLE) return CT_DOUBLE;
    if (a == CT_FLOAT  || b == CT_FLOAT)  return CT_FLOAT;
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
        uint8_t wt = (at == CT_DOUBLE) ? WASM_F64 : WASM_I32;
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
        emit_i32_const(tok_ival);
        next_token();
        return CT_INT;
    }
    if (tok == TOK_FLOAT_LIT) {
        emit_f32_const(tok_fval);
        next_token();
        return CT_FLOAT;
    }
    if (tok == TOK_CHAR_LIT) {
        emit_i32_const(tok_ival);
        next_token();
        return CT_INT;
    }
    if (tok == TOK_STR_LIT) {
        int off = add_string(tok_sval, tok_slen);
        emit_i32_const(off);
        next_token();
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
            return cast_to;
        }
        CType t = expr();
        expect(TOK_RPAREN);
        return t;
    }
    if (tok == TOK_SIZEOF) {
        next_token();
        expect(TOK_LPAREN);
        if (is_type_keyword(tok)) {
            CType ct = parse_type_spec();
            int size = 4;
            if (ct == CT_CHAR) size = 1;
            else if (ct == CT_DOUBLE) size = 8;
            emit_i32_const(size);
        } else {
            /* sizeof(expr) — parse into temp buffer to get type, discard code */
            FuncCtx *sf = &func_bufs[cur_func];
            int save_fixups = sf->ncall_fixups;
            Buf save_code = sf->code;
            Buf tmp_buf; buf_init(&tmp_buf);
            sf->code = tmp_buf;
            CType ct = expr();
            tmp_buf = sf->code;
            sf->code = save_code;
            sf->ncall_fixups = save_fixups; /* discard any fixups from expr */
            buf_free(&tmp_buf);
            int size = 4;
            if (ct == CT_CHAR) size = 1;
            else if (ct == CT_DOUBLE) size = 8;
            emit_i32_const(size);
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
                if      (strcmp(name, "sqrtf") == 0)  opcode = OP_F32_SQRT;
                else if (strcmp(name, "fabsf") == 0)  opcode = OP_F32_ABS;
                else if (strcmp(name, "floorf") == 0) opcode = OP_F32_FLOOR;
                else if (strcmp(name, "ceilf") == 0)  opcode = OP_F32_CEIL;
                else if (strcmp(name, "sqrt") == 0)   { opcode = OP_F64_SQRT;  btype = CT_DOUBLE; }
                else if (strcmp(name, "fabs") == 0)   { opcode = OP_F64_ABS;   btype = CT_DOUBLE; }
                else if (strcmp(name, "floor") == 0)  { opcode = OP_F64_FLOOR;  btype = CT_DOUBLE; }
                else if (strcmp(name, "ceil") == 0)   { opcode = OP_F64_CEIL;   btype = CT_DOUBLE; }
                if (opcode) {
                    next_token(); /* skip '(' */
                    CType at = expr();
                    emit_coerce(at, btype);
                    expect(TOK_RPAREN);
                    emit_op(opcode);
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

            return fn->ctype;
        }

        /* Variable reference */
        Symbol *sym = find_sym(name);
        if (!sym) {
            error_fmt("undefined variable '%s'", name);
            emit_i32_const(0);
            return CT_INT;
        }
        if (sym->kind == SYM_LOCAL) {
            emit_local_get(sym->idx);
        } else if (sym->kind == SYM_GLOBAL) {
            emit_global_get(sym->idx);
        } else if (sym->kind == SYM_IMPORT) {
            /* Calling import as a variable? Shouldn't happen. */
            error_fmt("'%s' is a function, not a variable", name);
            emit_i32_const(0);
        } else if (sym->kind == SYM_FUNC) {
            /* Function pointer — not supported */
            error_fmt("function pointers not supported");
            emit_i32_const(0);
        }
        return sym->ctype;
    }

    error_fmt("unexpected token %s in expression", tok_name(tok));
    next_token();
    emit_i32_const(0);
    return CT_INT;
}

/* ---- Postfix expressions: a++, a-- ---- */
static CType postfix_expr(void) {
    CType t = primary_expr();

    while (tok == TOK_INC || tok == TOK_DEC) {
        /* Post-increment/decrement on complex expressions is handled
         * by assignment_expr() for simple variable names. If we get
         * here, the expression is too complex. */
        error_at("postfix ++/-- on complex expressions not yet supported");
        next_token();
        break;
    }
    return t;
}

/* Helper: emit load for a symbol */
static void emit_sym_load(Symbol *sym) {
    if (sym->kind == SYM_LOCAL) emit_local_get(sym->idx);
    else if (sym->kind == SYM_GLOBAL) emit_global_get(sym->idx);
}

/* Helper: emit store for a symbol */
static void emit_sym_store(Symbol *sym) {
    if (sym->kind == SYM_LOCAL) emit_local_set(sym->idx);
    else if (sym->kind == SYM_GLOBAL) emit_global_set(sym->idx);
}

/* ---- Unary expressions ---- */
static CType unary_expr(void) {
    if (tok == TOK_MINUS) {
        next_token();
        CType t = unary_expr();
        if (t == CT_FLOAT) { emit_op(OP_F32_NEG); return CT_FLOAT; }
        if (t == CT_DOUBLE) { emit_op(OP_F64_NEG); return CT_DOUBLE; }
        /* i32: val * -1 */
        emit_i32_const(-1);
        emit_op(OP_I32_MUL);
        return CT_INT;
    }
    if (tok == TOK_BANG) {
        next_token();
        CType t = unary_expr();
        if (t == CT_FLOAT) { emit_coerce_i32(CT_FLOAT); }
        if (t == CT_DOUBLE) { emit_coerce_i32(CT_DOUBLE); }
        emit_op(OP_I32_EQZ);
        return CT_INT;
    }
    if (tok == TOK_TILDE) {
        next_token();
        CType t = unary_expr();
        emit_coerce(t, CT_INT);
        emit_i32_const(-1);
        emit_op(OP_I32_XOR);
        return CT_INT;
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
        emit_sym_load(sym);
        if (sym->ctype == CT_FLOAT) {
            emit_f32_const(1.0f);
            emit_op(is_inc ? OP_F32_ADD : OP_F32_SUB);
        } else {
            emit_i32_const(1);
            emit_op(is_inc ? OP_I32_ADD : OP_I32_SUB);
        }
        /* Tee so the new value stays on stack and is stored */
        if (sym->kind == SYM_LOCAL) emit_local_tee(sym->idx);
        else { emit_sym_store(sym); emit_sym_load(sym); }
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

static CType prec_expr(int min_prec) {
    CType left = unary_expr();

    while (get_prec(tok) >= min_prec) {
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

        CType right = prec_expr(prec + 1);
        CType result = promote(left, right);

        /* Coerce both sides to common type */
        if (result == CT_FLOAT) {
            /* Right is on top of stack, left is below */
            if (right != CT_FLOAT && left == CT_FLOAT) {
                /* Need to convert right (top of stack) to float */
                emit_coerce(right, CT_FLOAT);
            } else if (left != CT_FLOAT && right == CT_FLOAT) {
                /* Need to convert left (below top) to float — use temp */
                int tmp = alloc_local(WASM_F32);
                emit_local_set(tmp);  /* save right */
                emit_coerce(left, CT_FLOAT);
                emit_local_get(tmp);  /* restore right */
            }
        }

        /* Bitwise and shift ops: always int */
        if (op == TOK_AMP || op == TOK_PIPE || op == TOK_CARET ||
            op == TOK_LSHIFT || op == TOK_RSHIFT) {
            if (right == CT_FLOAT) emit_coerce_i32(CT_FLOAT);
            if (left == CT_FLOAT) {
                int tmp = alloc_local(WASM_I32);
                emit_local_set(tmp);
                emit_coerce_i32(CT_FLOAT);
                emit_local_get(tmp);
            }
            result = CT_INT;
        }

        switch (op) {
        case TOK_PLUS:
            emit_op(result == CT_FLOAT ? OP_F32_ADD : OP_I32_ADD);
            break;
        case TOK_MINUS:
            emit_op(result == CT_FLOAT ? OP_F32_SUB : OP_I32_SUB);
            break;
        case TOK_STAR:
            emit_op(result == CT_FLOAT ? OP_F32_MUL : OP_I32_MUL);
            break;
        case TOK_SLASH:
            emit_op(result == CT_FLOAT ? OP_F32_DIV : OP_I32_DIV_S);
            break;
        case TOK_PERCENT:
            if (result == CT_FLOAT) {
                /* fmodf — use import */
                emit_call(IMP_FMODF);
            } else {
                emit_op(OP_I32_REM_S);
            }
            break;
        case TOK_EQ:
            emit_op(result == CT_FLOAT ? OP_F32_EQ : OP_I32_EQ);
            result = CT_INT;
            break;
        case TOK_NE:
            emit_op(result == CT_FLOAT ? OP_F32_NE : OP_I32_NE);
            result = CT_INT;
            break;
        case TOK_LT:
            emit_op(result == CT_FLOAT ? OP_F32_LT : OP_I32_LT_S);
            result = CT_INT;
            break;
        case TOK_GT:
            emit_op(result == CT_FLOAT ? OP_F32_GT : OP_I32_GT_S);
            result = CT_INT;
            break;
        case TOK_LE:
            emit_op(result == CT_FLOAT ? OP_F32_LE : OP_I32_LE_S);
            result = CT_INT;
            break;
        case TOK_GE:
            emit_op(result == CT_FLOAT ? OP_F32_GE : OP_I32_GE_S);
            result = CT_INT;
            break;
        case TOK_AMP:    emit_op(OP_I32_AND); break;
        case TOK_PIPE:   emit_op(OP_I32_OR);  break;
        case TOK_CARET:  emit_op(OP_I32_XOR); break;
        case TOK_LSHIFT: emit_op(OP_I32_SHL); break;
        case TOK_RSHIFT: emit_op(OP_I32_SHR_S); break;
        }

        left = result;
    }
    return left;
}

/* ---- Ternary and assignment ---- */

CType assignment_expr(void) {
    /* Check if this is a simple variable name followed by assignment op */
    if (tok == TOK_NAME) {
        char name[64];
        strncpy(name, tok_sval, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        int pt = peek_token();

        /* Simple assignment: name = expr */
        if (pt == TOK_ASSIGN) {
            next_token(); /* skip name */
            next_token(); /* skip '=' */
            Symbol *sym = find_sym(name);
            if (!sym) { error_fmt("undefined variable '%s'", name); return CT_INT; }
            CType rhs = assignment_expr();
            emit_coerce(rhs, sym->ctype);
            /* Tee so value stays on stack */
            if (sym->kind == SYM_LOCAL) emit_local_tee(sym->idx);
            else { emit_global_set(sym->idx); emit_global_get(sym->idx); }
            return sym->ctype;
        }

        /* Compound assignment: name OP= expr */
        if (pt >= TOK_PLUS_EQ && pt <= TOK_RSHIFT_EQ) {
            next_token(); /* skip name */
            int aop = tok;
            next_token(); /* skip op= */
            Symbol *sym = find_sym(name);
            if (!sym) { error_fmt("undefined variable '%s'", name); return CT_INT; }

            /* Load current value */
            emit_sym_load(sym);

            CType rhs = assignment_expr();
            CType result = promote(sym->ctype, rhs);

            /* Coerce both operands */
            if (result == CT_FLOAT && rhs != CT_FLOAT) {
                emit_coerce(rhs, CT_FLOAT);
            }
            if (result == CT_FLOAT && sym->ctype != CT_FLOAT) {
                /* Left (already on stack below right) needs conversion.
                 * Save right, convert left, restore right. */
                int tmp = alloc_local(ctype_to_wasm(result));
                emit_local_set(tmp);
                emit_coerce(sym->ctype, result);
                emit_local_get(tmp);
            }

            switch (aop) {
            case TOK_PLUS_EQ:
                emit_op(result == CT_FLOAT ? OP_F32_ADD : OP_I32_ADD); break;
            case TOK_MINUS_EQ:
                emit_op(result == CT_FLOAT ? OP_F32_SUB : OP_I32_SUB); break;
            case TOK_STAR_EQ:
                emit_op(result == CT_FLOAT ? OP_F32_MUL : OP_I32_MUL); break;
            case TOK_SLASH_EQ:
                emit_op(result == CT_FLOAT ? OP_F32_DIV : OP_I32_DIV_S); break;
            case TOK_PERCENT_EQ:  emit_op(OP_I32_REM_S);  break;
            case TOK_AMP_EQ:      emit_op(OP_I32_AND);    break;
            case TOK_PIPE_EQ:     emit_op(OP_I32_OR);     break;
            case TOK_CARET_EQ:    emit_op(OP_I32_XOR);    break;
            case TOK_LSHIFT_EQ:   emit_op(OP_I32_SHL);    break;
            case TOK_RSHIFT_EQ:   emit_op(OP_I32_SHR_S);  break;
            }

            /* Coerce result back to variable type */
            if (result != sym->ctype) emit_coerce(result, sym->ctype);

            if (sym->kind == SYM_LOCAL) emit_local_tee(sym->idx);
            else { emit_global_set(sym->idx); emit_global_get(sym->idx); }
            return sym->ctype;
        }

        /* Post-increment/decrement: name++ or name-- */
        if (pt == TOK_INC || pt == TOK_DEC) {
            next_token(); /* skip name */
            int is_inc = (tok == TOK_INC);
            next_token(); /* skip ++/-- */
            Symbol *sym = find_sym(name);
            if (!sym) { error_fmt("undefined variable '%s'", name); emit_i32_const(0); return CT_INT; }
            /* Load old value (this is the result) */
            emit_sym_load(sym);
            /* Compute new value: old ± 1 */
            emit_sym_load(sym);
            if (sym->ctype == CT_FLOAT) {
                emit_f32_const(1.0f);
                emit_op(is_inc ? OP_F32_ADD : OP_F32_SUB);
            } else {
                emit_i32_const(1);
                emit_op(is_inc ? OP_I32_ADD : OP_I32_SUB);
            }
            emit_sym_store(sym);
            /* Old value is still on stack */
            return sym->ctype;
        }
    }

    /* Not an assignment — parse as conditional/binary expression */
    CType t = prec_expr(1);

    /* Ternary: expr ? expr : expr */
    if (tok == TOK_QUESTION) {
        next_token();
        emit_coerce(t, CT_INT);
        /* Parse both branches to determine common type */
        CType then_t, else_t;
        /* Then branch */
        emit_if_i32();
        then_t = expr();
        expect(TOK_COLON);
        /* We need to know the else type to determine common type.
         * Compile else into a temp buffer, peek at its type, then decide. */
        FuncCtx *ternary_f = &func_bufs[cur_func];
        int fixups_before = ternary_f->ncall_fixups;
        Buf save = ternary_f->code;
        Buf else_buf; buf_init(&else_buf);
        ternary_f->code = else_buf;
        emit_else();
        else_t = expr();
        else_buf = ternary_f->code;
        ternary_f->code = save;
        /* Determine common result type */
        CType result = CT_INT;
        if (then_t == CT_FLOAT || else_t == CT_FLOAT) result = CT_FLOAT;
        if (then_t == CT_DOUBLE || else_t == CT_DOUBLE) result = CT_DOUBLE;
        /* Coerce then-branch to result type */
        emit_coerce(then_t, result);
        /* Splice else-branch code — adjust call fixups recorded during else */
        int splice_offset = ternary_f->code.len;
        buf_bytes(CODE, else_buf.data, else_buf.len);
        buf_free(&else_buf);
        for (int fx = fixups_before; fx < ternary_f->ncall_fixups; fx++)
            ternary_f->call_fixups[fx] += splice_offset;
        /* Coerce else-branch to result type */
        emit_coerce(else_t, result);
        emit_end();
        return result;
    }

    return t;
}

/* Top-level expression (comma operator — we just parse one assignment_expr) */
CType expr(void) {
    return assignment_expr();
}
