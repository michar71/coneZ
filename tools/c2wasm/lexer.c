/*
 * lexer.c — C tokenizer for c2wasm
 */
#include "c2wasm.h"

static int peek_tok;
static int peek_valid;
static int peek_ival;
static int64_t peek_i64;
static int peek_int_is_64;
static int peek_int_unsigned;
static float peek_fval;
static double peek_dval;
static char peek_sval[1024];
static int peek_slen;

/* Macro expansion depth guard (prevents mutual recursion) */
#define MAX_MACRO_DEPTH 16
static int macro_depth;
static int predefined_counter;
static char predefined_date[16];
static char predefined_time[16];

/* Guard to prevent macro expansion during lexer save/restore (e.g., switch pre-scan) */
static int lexer_save_active = 0;

void lex_init(void) {
    peek_valid = 0;
    tok = 0;
    tok_ival = 0;
    tok_i64 = 0;
    tok_int_is_64 = 0;
    tok_int_unsigned = 0;
    macro_depth = 0;
    lexer_save_active = 0;
    predefined_counter = 0;

    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (tmv) {
        strftime(predefined_date, sizeof(predefined_date), "%b %d %Y", tmv);
        strftime(predefined_time, sizeof(predefined_time), "%H:%M:%S", tmv);
    } else {
        snprintf(predefined_date, sizeof(predefined_date), "Jan 01 1970");
        snprintf(predefined_time, sizeof(predefined_time), "00:00:00");
    }
}

static int emit_predefined_macro_token(void) {
    if (strcmp(tok_sval, "__LINE__") == 0) {
        tok_i64 = (int64_t)line_num;
        tok_ival = line_num;
        tok_int_is_64 = 0;
        tok_int_unsigned = 0;
        return TOK_INT_LIT;
    }
    if (strcmp(tok_sval, "__COUNTER__") == 0) {
        tok_i64 = (int64_t)predefined_counter;
        tok_ival = predefined_counter;
        tok_int_is_64 = 0;
        tok_int_unsigned = 0;
        predefined_counter++;
        return TOK_INT_LIT;
    }
    if (strcmp(tok_sval, "__STDC__") == 0) {
        tok_i64 = 1;
        tok_ival = 1;
        tok_int_is_64 = 0;
        tok_int_unsigned = 0;
        return TOK_INT_LIT;
    }
    if (strcmp(tok_sval, "__STDC_VERSION__") == 0) {
        tok_i64 = 199901;
        tok_ival = 199901;
        tok_int_is_64 = 0;
        tok_int_unsigned = 0;
        return TOK_INT_LIT;
    }
    if (strcmp(tok_sval, "__STDC_HOSTED__") == 0) {
        tok_i64 = 0;
        tok_ival = 0;
        tok_int_is_64 = 0;
        tok_int_unsigned = 0;
        return TOK_INT_LIT;
    }
    if (strcmp(tok_sval, "__FILE__") == 0) {
        const char *f = src_file ? src_file : "<input>";
        tok_slen = (int)strlen(f);
        if (tok_slen > (int)sizeof(tok_sval) - 1)
            tok_slen = (int)sizeof(tok_sval) - 1;
        memcpy(tok_sval, f, tok_slen);
        tok_sval[tok_slen] = 0;
        return TOK_STR_LIT;
    }
    if (strcmp(tok_sval, "__DATE__") == 0) {
        tok_slen = (int)strlen(predefined_date);
        memcpy(tok_sval, predefined_date, tok_slen + 1);
        return TOK_STR_LIT;
    }
    if (strcmp(tok_sval, "__TIME__") == 0) {
        tok_slen = (int)strlen(predefined_time);
        memcpy(tok_sval, predefined_time, tok_slen + 1);
        return TOK_STR_LIT;
    }
    return 0;
}

static int ch(void) {
    if (src_pos >= src_len) return -1;
    return (unsigned char)source[src_pos];
}

static int advance(void) {
    if (src_pos >= src_len) return -1;
    int c = (unsigned char)source[src_pos++];
    if (c == '\n') line_num++;
    return c;
}

static int peek_ch(void) {
    if (src_pos + 1 >= src_len) return -1;
    return (unsigned char)source[src_pos + 1];
}

static void skip_ws(void) {
    while (src_pos < src_len) {
        int c = ch();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        /* // comment */
        if (c == '/' && peek_ch() == '/') {
            advance(); advance();
            while (src_pos < src_len && ch() != '\n') advance();
            continue;
        }
        /* block comment */
        if (c == '/' && peek_ch() == '*') {
            advance(); advance();
            int found = 0;
            while (src_pos < src_len) {
                if (ch() == '*' && peek_ch() == '/') { advance(); advance(); found = 1; break; }
                advance();
            }
            if (!found) error_at("unterminated block comment");
            continue;
        }
        break;
    }
}

static int is_ident_start(int c) { return isalpha(c) || c == '_'; }
static int is_ident_char(int c) { return isalnum(c) || c == '_'; }

struct kw { const char *name; int tok; };
static struct kw keywords[] = {
    {"if",       TOK_IF},
    {"else",     TOK_ELSE},
    {"for",      TOK_FOR},
    {"while",    TOK_WHILE},
    {"do",       TOK_DO},
    {"switch",   TOK_SWITCH},
    {"case",     TOK_CASE},
    {"default",  TOK_DEFAULT},
    {"break",    TOK_BREAK},
    {"continue", TOK_CONTINUE},
    {"return",   TOK_RETURN},
    {"int",      TOK_INT},
    {"float",    TOK_FLOAT},
    {"double",   TOK_DOUBLE},
    {"void",     TOK_VOID},
    {"char",     TOK_CHAR},
    {"static",   TOK_STATIC},
    {"const",    TOK_CONST},
    {"unsigned", TOK_UNSIGNED},
    {"long",     TOK_LONG},
    {"short",    TOK_SHORT},
    {"signed",   TOK_SIGNED},
    {"_Bool",    TOK_BOOL},
    {"bool",     TOK_BOOL},
    {"int8_t",   TOK_INT8},
    {"uint8_t",  TOK_UINT8},
    {"int16_t",  TOK_INT16},
    {"uint16_t", TOK_UINT16},
    {"int32_t",  TOK_INT32},
    {"uint32_t", TOK_UINT32},
    {"int64_t",  TOK_INT64},
    {"uint64_t", TOK_UINT64},
    {"size_t",   TOK_SIZE_T},
    {"sizeof",   TOK_SIZEOF},
    {NULL, 0}
};

static int lookup_keyword(const char *name) {
    for (struct kw *k = keywords; k->name; k++)
        if (strcmp(name, k->name) == 0) return k->tok;
    return -1;
}

static void lex_int_suffix(int *has_u, int *l_count) {
    *has_u = 0;
    *l_count = 0;
    int u_count = 0;
    while (src_pos < src_len && (ch() == 'u' || ch() == 'U' || ch() == 'l' || ch() == 'L')) {
        if (ch() == 'u' || ch() == 'U') {
            u_count++;
            if (u_count > 1)
                error_at("invalid integer suffix");
            *has_u = 1;
        } else {
            (*l_count)++;
            if (*l_count > 2)
                error_at("invalid integer suffix");
        }
        advance();
    }
}

static void classify_int_literal(unsigned long long uv, int has_u, int l_count, int is_decimal) {
    int is_64 = 0;
    int is_unsigned = 0;

    if (l_count >= 2) {
        is_64 = 1;
        is_unsigned = has_u || uv > (unsigned long long)LLONG_MAX;
    } else if (l_count == 1) {
        if (has_u) {
            if (uv <= UINT_MAX) {
                is_64 = 0;
                is_unsigned = 1;
            } else {
                is_64 = 1;
                is_unsigned = 1;
            }
        } else {
            /* long is 32-bit in this compiler's model */
            if (uv <= INT_MAX) {
                is_64 = 0;
                is_unsigned = 0;
            } else if (!is_decimal && uv <= UINT_MAX) {
                is_64 = 0;
                is_unsigned = 1;
            } else if (uv <= (unsigned long long)LLONG_MAX) {
                is_64 = 1;
                is_unsigned = 0;
            } else {
                is_64 = 1;
                is_unsigned = 1;
            }
        }
    } else if (has_u) {
        if (uv <= UINT_MAX) {
            is_64 = 0;
            is_unsigned = 1;
        } else {
            is_64 = 1;
            is_unsigned = 1;
        }
    } else if (is_decimal) {
        if (uv <= INT_MAX) {
            is_64 = 0;
            is_unsigned = 0;
        } else if (uv <= (unsigned long long)LLONG_MAX) {
            is_64 = 1;
            is_unsigned = 0;
        } else {
            is_64 = 1;
            is_unsigned = 1;
        }
    } else {
        if (uv <= INT_MAX) {
            is_64 = 0;
            is_unsigned = 0;
        } else if (uv <= UINT_MAX) {
            is_64 = 0;
            is_unsigned = 1;
        } else if (uv <= (unsigned long long)LLONG_MAX) {
            is_64 = 1;
            is_unsigned = 0;
        } else {
            is_64 = 1;
            is_unsigned = 1;
        }
    }

    tok_i64 = (int64_t)uv;
    tok_ival = (int32_t)uv;
    tok_int_is_64 = is_64;
    tok_int_unsigned = is_unsigned;
}

static int parse_escape_sequence(void) {
    if (src_pos >= src_len || ch() == '\n')
        error_at("unterminated escape sequence");

    int c = ch();
    switch (c) {
    case 'n': advance(); return '\n';
    case 't': advance(); return '\t';
    case 'r': advance(); return '\r';
    case 'a': advance(); return '\a';
    case 'b': advance(); return '\b';
    case 'f': advance(); return '\f';
    case 'v': advance(); return '\v';
    case '\\': advance(); return '\\';
    case '"': advance(); return '"';
    case '\'': advance(); return '\'';
    case '?': advance(); return '?';
    case 'x': {
        advance();
        int val = 0;
        int hex_digits = 0;
        while (src_pos < src_len && isxdigit(ch()) && hex_digits < 2) {
            int d = ch();
            if (d >= '0' && d <= '9') val = val * 16 + d - '0';
            else if (d >= 'a' && d <= 'f') val = val * 16 + d - 'a' + 10;
            else val = val * 16 + d - 'A' + 10;
            advance();
            hex_digits++;
        }
        if (!hex_digits)
            error_at("invalid hex escape sequence");
        return val;
    }
    default:
        if (c >= '0' && c <= '7') {
            int val = 0;
            int oct_digits = 0;
            while (src_pos < src_len && ch() >= '0' && ch() <= '7' && oct_digits < 3) {
                val = (val << 3) + (ch() - '0');
                advance();
                oct_digits++;
            }
            return val;
        }
        advance();
        return c;
    }
}

static void append_string_segment(int *len, int max_len) {
    while (src_pos < src_len && ch() != '"' && ch() != '\n' && *len < max_len) {
        if (ch() == '\\') {
            advance();
            tok_sval[(*len)++] = (char)parse_escape_sequence();
        } else {
            tok_sval[(*len)++] = advance();
        }
    }
}

static int lex_raw(void) {
    tok_int_is_64 = 0;
    tok_int_unsigned = 0;
    tok_i64 = 0;

    skip_ws();
    if (src_pos >= src_len) return TOK_EOF;

    /* When inside #if 0 / #ifdef skip, only process # directives */
    while (preproc_skipping() && ch() != '#' && src_pos < src_len) {
        /* Skip to end of line */
        while (src_pos < src_len && ch() != '\n') advance();
        skip_ws();
        if (src_pos >= src_len) return TOK_EOF;
    }

    /* Preprocessor directive */
    if (ch() == '#') {
        if (preproc_line()) return TOK_PP_DONE;
        /* If preproc didn't consume, treat # as unexpected */
        advance();
        error_at("unexpected '#'");
        return TOK_PP_DONE;
    }

    int c = ch();

    /* Identifiers and keywords */
    if (is_ident_start(c)) {
        int len = 0;
        while (src_pos < src_len && is_ident_char(ch()) && len < 1023)
            tok_sval[len++] = advance();
        tok_sval[len] = 0;

        int kw = lookup_keyword(tok_sval);
        if (kw >= 0) return kw;

        int predef = emit_predefined_macro_token();
        if (predef) return predef;

        /* Check for macro expansion (with depth limit to prevent mutual recursion,
         * and disabled during lexer save/restore to prevent use-after-free) */
        Symbol *mac = find_sym_kind(tok_sval, SYM_DEFINE);
        if (mac && macro_depth < MAX_MACRO_DEPTH && !lexer_save_active) {
            /* Push macro value back into source for re-lexing */
            int vlen = strlen(mac->macro_val);
            if (vlen > 0) {
                int remain = src_len - src_pos;
                char *new_src = malloc(src_pos + vlen + remain + 1);
                memcpy(new_src, source, src_pos);
                memcpy(new_src + src_pos, mac->macro_val, vlen);
                memcpy(new_src + src_pos + vlen, source + src_pos, remain);
                new_src[src_pos + vlen + remain] = 0;
                free(source);
                source = new_src;
                src_len = src_pos + vlen + remain;
                macro_depth++;
                int result = lex_raw();
                macro_depth--;
                return result;
            }
            /* Empty macro — skip it and get the next token iteratively */
            macro_depth++;
            int result = lex_raw();
            macro_depth--;
            return result;
        }

        return TOK_NAME;
    }

    /* Numeric literals */
    if (isdigit(c) || (c == '.' && isdigit(peek_ch()))) {
        char nbuf[128];
        int len = 0;
        int is_float = 0;

        if (c == '0' && (peek_ch() == 'x' || peek_ch() == 'X')) {
            /* Hex */
            nbuf[len++] = advance(); nbuf[len++] = advance();
            int has_hex_digit = 0;
            while (src_pos < src_len && isxdigit(ch()) && len < 62) {
                nbuf[len++] = advance();
                has_hex_digit = 1;
            }
            nbuf[len] = 0;

            if (!has_hex_digit)
                error_at("invalid hex literal");

            /* Optional U/L suffixes */
            int has_u = 0, l_count = 0;
            lex_int_suffix(&has_u, &l_count);
            if (is_ident_start(ch()))
                error_at("invalid integer suffix");

            unsigned long long uv = strtoull(nbuf, NULL, 16);
            classify_int_literal(uv, has_u, l_count, 0);
            return TOK_INT_LIT;
        }

        if (c == '0' && isdigit(peek_ch())) {
            /* Octal integer (reject 8/9 digits) */
            int bad_octal = 0;
            nbuf[len++] = advance(); /* leading 0 */
            while (src_pos < src_len && isdigit(ch()) && len < 62) {
                if (ch() > '7') bad_octal = 1;
                nbuf[len++] = advance();
            }
            nbuf[len] = 0;

            if (bad_octal)
                error_at("invalid octal literal");

            int has_u = 0, l_count = 0;
            lex_int_suffix(&has_u, &l_count);
            if (is_ident_start(ch()))
                error_at("invalid integer suffix");

            unsigned long long uv = strtoull(nbuf, NULL, 8);
            classify_int_literal(uv, has_u, l_count, 0);
            return TOK_INT_LIT;
        }

        /* Decimal or float */
        while (src_pos < src_len && isdigit(ch()) && len < 62)
            nbuf[len++] = advance();
        if (src_pos < src_len && ch() == '.') {
            is_float = 1;
            nbuf[len++] = advance();
            int frac_digits = 0;
            while (src_pos < src_len && isdigit(ch()) && len < 62) {
                nbuf[len++] = advance();
                frac_digits = 1;
            }
            if (!frac_digits && ch() == '.')
                error_at("invalid float literal");
        }
        if (src_pos < src_len && (ch() == 'e' || ch() == 'E')) {
            is_float = 1;
            nbuf[len++] = advance();
            if (src_pos < src_len && (ch() == '+' || ch() == '-'))
                nbuf[len++] = advance();
            int exp_digits = 0;
            while (src_pos < src_len && isdigit(ch()) && len < 62) {
                nbuf[len++] = advance();
                exp_digits = 1;
            }
            if (!exp_digits)
                error_at("invalid float exponent");
        }
        nbuf[len] = 0;

        /* Check for f/F suffix — explicit float vs implicit double */
        int has_f_suffix = 0;
        if (is_float && src_pos < src_len && (ch() == 'f' || ch() == 'F')) {
            advance();
            is_float = 1;
            has_f_suffix = 1;
        }
        if (is_float && is_ident_start(ch()))
            error_at("invalid float suffix");

        if (is_float) {
            tok_fval = strtof(nbuf, NULL);
            tok_dval = strtod(nbuf, NULL);
            tok_i64 = 0;
            tok_int_is_64 = 0;
            tok_int_unsigned = 0;
            return has_f_suffix ? TOK_FLOAT_LIT : TOK_DOUBLE_LIT;
        }
        int has_u = 0, l_count = 0;
        lex_int_suffix(&has_u, &l_count);
        if (is_ident_start(ch()))
            error_at("invalid integer suffix");
        unsigned long long uv = strtoull(nbuf, NULL, 10);
        classify_int_literal(uv, has_u, l_count, 1);
        return TOK_INT_LIT;
    }

    /* String literal */
    if (c == '"') {
        advance();
        int len = 0;
        const int MAX_STRLEN = sizeof(tok_sval) - 2; /* Reserve 1 for null, 1 for safety margin */
        append_string_segment(&len, MAX_STRLEN);
        if (ch() == '"') advance();
        else if (len >= MAX_STRLEN) error_at("string literal too long (max 1022 characters)");
        else error_at("unterminated string literal");
        tok_sval[len] = 0;
        tok_slen = len;

        /* String concatenation: handle arbitrary number of adjacent strings */
        for (;;) {
            skip_ws();
            if (ch() != '"') break;
            advance();
            append_string_segment(&len, MAX_STRLEN);
            if (len >= MAX_STRLEN) {
                error_at("string literal too long after concatenation (max 1022 characters)");
                break;
            }
            if (ch() == '"') advance();
        }
        tok_sval[len] = 0;
        tok_slen = len;
        return TOK_STR_LIT;
    }

    /* Char literal */
    if (c == '\'') {
        advance();
        if (ch() == '\'') {
            error_at("empty character literal");
        }
        if (ch() == '\\') {
            advance();
            tok_ival = parse_escape_sequence();
        } else {
            if (ch() == -1 || ch() == '\n')
                error_at("unterminated character literal");
            tok_ival = ch();
            advance();
        }
        if (ch() == '\'') {
            advance();
        } else if (ch() == -1 || ch() == '\n') {
            error_at("unterminated character literal");
        } else {
            while (src_pos < src_len && ch() != '\'' && ch() != '\n')
                advance();
            if (ch() == '\'') advance();
            error_at("multi-character character literal not supported");
        }
        tok_i64 = (int64_t)tok_ival;
        tok_int_is_64 = 0;
        tok_int_unsigned = 0;
        return TOK_CHAR_LIT;
    }

    /* Multi-char operators */
    advance();
    switch (c) {
    case '(': return TOK_LPAREN;
    case ')': return TOK_RPAREN;
    case '{': return TOK_LBRACE;
    case '}': return TOK_RBRACE;
    case '[': return TOK_LBRACKET;
    case ']': return TOK_RBRACKET;
    case ';': return TOK_SEMI;
    case ',': return TOK_COMMA;
    case '.': return TOK_DOT;
    case '~': return TOK_TILDE;
    case '?': return TOK_QUESTION;
    case ':': return TOK_COLON;

    case '+':
        if (ch() == '+') { advance(); return TOK_INC; }
        if (ch() == '=') { advance(); return TOK_PLUS_EQ; }
        return TOK_PLUS;
    case '-':
        if (ch() == '-') { advance(); return TOK_DEC; }
        if (ch() == '=') { advance(); return TOK_MINUS_EQ; }
        if (ch() == '>') { advance(); return TOK_ARROW; }
        return TOK_MINUS;
    case '*':
        if (ch() == '=') { advance(); return TOK_STAR_EQ; }
        return TOK_STAR;
    case '/':
        if (ch() == '=') { advance(); return TOK_SLASH_EQ; }
        return TOK_SLASH;
    case '%':
        if (ch() == '=') { advance(); return TOK_PERCENT_EQ; }
        return TOK_PERCENT;
    case '&':
        if (ch() == '&') { advance(); return TOK_AND_AND; }
        if (ch() == '=') { advance(); return TOK_AMP_EQ; }
        return TOK_AMP;
    case '|':
        if (ch() == '|') { advance(); return TOK_OR_OR; }
        if (ch() == '=') { advance(); return TOK_PIPE_EQ; }
        return TOK_PIPE;
    case '^':
        if (ch() == '=') { advance(); return TOK_CARET_EQ; }
        return TOK_CARET;
    case '!':
        if (ch() == '=') { advance(); return TOK_NE; }
        return TOK_BANG;
    case '<':
        if (ch() == '<') { advance(); if (ch() == '=') { advance(); return TOK_LSHIFT_EQ; } return TOK_LSHIFT; }
        if (ch() == '=') { advance(); return TOK_LE; }
        return TOK_LT;
    case '>':
        if (ch() == '>') { advance(); if (ch() == '=') { advance(); return TOK_RSHIFT_EQ; } return TOK_RSHIFT; }
        if (ch() == '=') { advance(); return TOK_GE; }
        return TOK_GT;
    case '=':
        if (ch() == '=') { advance(); return TOK_EQ; }
        return TOK_ASSIGN;
    }

    error_fmt("unexpected character '%c' (0x%02x)", c, c);
    /* Skip unexpected chars iteratively instead of recursing */
    while (src_pos < src_len) {
        int nc = ch();
        if (nc == -1) return TOK_EOF;
        if (isalpha(nc) || nc == '_' || isdigit(nc) || nc == '"' || nc == '\'' ||
            nc == '(' || nc == ')' || nc == '{' || nc == '}' || nc == ';' ||
            nc == '#' || nc == '+' || nc == '-' || nc == '*' || nc == '/' ||
            nc == '=' || nc == '<' || nc == '>' || nc == '!' || nc == '&' ||
            nc == '|' || nc == '^' || nc == '~' || nc == '?' || nc == ':' ||
            nc == ',' || nc == '.' || nc == '[' || nc == ']' || nc == '%')
            return lex_raw();
        advance();
        error_fmt("unexpected character '%c' (0x%02x)", nc, nc);
    }
    return TOK_EOF;
}

int next_token(void) {
    if (peek_valid) {
        tok = peek_tok;
        tok_ival = peek_ival;
        tok_i64 = peek_i64;
        tok_int_is_64 = peek_int_is_64;
        tok_int_unsigned = peek_int_unsigned;
        tok_fval = peek_fval;
        tok_dval = peek_dval;
        memcpy(tok_sval, peek_sval, sizeof(tok_sval));
        tok_slen = peek_slen;
        peek_valid = 0;
        return tok;
    }
    do {
        tok = lex_raw();
    } while (tok == TOK_PP_DONE);
    return tok;
}

int peek_token(void) {
    if (peek_valid) return peek_tok;
    /* Save current state */
    int save_tok = tok, save_ival = tok_ival;
    int64_t save_i64 = tok_i64;
    int save_int_is_64 = tok_int_is_64;
    int save_int_unsigned = tok_int_unsigned;
    float save_fval = tok_fval;
    double save_dval = tok_dval;
    char save_sval[1024]; int save_slen = tok_slen;
    memcpy(save_sval, tok_sval, sizeof(save_sval));

    do {
        peek_tok = lex_raw();
    } while (peek_tok == TOK_PP_DONE);
    peek_ival = tok_ival;
    peek_i64 = tok_i64;
    peek_int_is_64 = tok_int_is_64;
    peek_int_unsigned = tok_int_unsigned;
    peek_fval = tok_fval;
    peek_dval = tok_dval;
    memcpy(peek_sval, tok_sval, sizeof(peek_sval));
    peek_slen = tok_slen;
    peek_valid = 1;

    /* Restore current state */
    tok = save_tok; tok_ival = save_ival; tok_i64 = save_i64;
    tok_int_is_64 = save_int_is_64; tok_int_unsigned = save_int_unsigned;
    tok_fval = save_fval; tok_dval = save_dval;
    memcpy(tok_sval, save_sval, sizeof(tok_sval));
    tok_slen = save_slen;

    return peek_tok;
}

void expect(int t) {
    if (tok != t) {
        error_fmt("expected %s, got %s", tok_name(t), tok_name(tok));
        return;
    }
    next_token();
}

int accept(int t) {
    if (tok == t) { next_token(); return 1; }
    return 0;
}

/* ---- Error recovery: skip to synchronization point ---- */
void synchronize(int stop_at_semi, int stop_at_brace, int stop_at_rparen) {
    while (tok != TOK_EOF) {
        if (stop_at_semi && tok == TOK_SEMI) {
            next_token();  /* Consume the semicolon */
            break;
        }
        if (stop_at_brace && tok == TOK_RBRACE) {
            break;  /* Don't consume - let caller handle it */
        }
        if (stop_at_rparen && tok == TOK_RPAREN) {
            break;  /* Don't consume - let caller handle it */
        }
        next_token();
    }
}

/* ---- Lexer save/restore for pre-scan ---- */

void lexer_save(LexerSave *s) {
    s->saved_source = malloc(src_len + 1);
    if (!s->saved_source) { fprintf(stderr, "c2wasm: out of memory\n"); exit(1); }
    memcpy(s->saved_source, source, src_len + 1);
    s->saved_src_pos = src_pos;
    s->saved_src_len = src_len;
    s->saved_line_num = line_num;
    s->saved_tok = tok;
    s->saved_tok_ival = tok_ival;
    s->saved_tok_i64 = tok_i64;
    s->saved_tok_int_is_64 = tok_int_is_64;
    s->saved_tok_int_unsigned = tok_int_unsigned;
    s->saved_tok_fval = tok_fval;
    s->saved_tok_dval = tok_dval;
    memcpy(s->saved_tok_sval, tok_sval, sizeof(tok_sval));
    s->saved_tok_slen = tok_slen;
    s->saved_peek_valid = peek_valid;
    s->saved_peek_tok = peek_tok;
    s->saved_peek_ival = peek_ival;
    s->saved_peek_i64 = peek_i64;
    s->saved_peek_int_is_64 = peek_int_is_64;
    s->saved_peek_int_unsigned = peek_int_unsigned;
    s->saved_peek_fval = peek_fval;
    s->saved_peek_dval = peek_dval;
    memcpy(s->saved_peek_sval, peek_sval, sizeof(peek_sval));
    s->saved_peek_slen = peek_slen;
    s->saved_macro_depth = macro_depth;
    lexer_save_active = 1;
}

void lexer_restore(LexerSave *s) {
    free(source);
    source = s->saved_source;
    src_pos = s->saved_src_pos;
    src_len = s->saved_src_len;
    line_num = s->saved_line_num;
    tok = s->saved_tok;
    tok_ival = s->saved_tok_ival;
    tok_i64 = s->saved_tok_i64;
    tok_int_is_64 = s->saved_tok_int_is_64;
    tok_int_unsigned = s->saved_tok_int_unsigned;
    tok_fval = s->saved_tok_fval;
    tok_dval = s->saved_tok_dval;
    memcpy(tok_sval, s->saved_tok_sval, sizeof(tok_sval));
    tok_slen = s->saved_tok_slen;
    peek_valid = s->saved_peek_valid;
    peek_tok = s->saved_peek_tok;
    peek_ival = s->saved_peek_ival;
    peek_i64 = s->saved_peek_i64;
    peek_int_is_64 = s->saved_peek_int_is_64;
    peek_int_unsigned = s->saved_peek_int_unsigned;
    peek_fval = s->saved_peek_fval;
    peek_dval = s->saved_peek_dval;
    memcpy(peek_sval, s->saved_peek_sval, sizeof(peek_sval));
    peek_slen = s->saved_peek_slen;
    macro_depth = s->saved_macro_depth;
    lexer_save_active = 0;
}

const char *tok_name(int t) {
    switch (t) {
    case TOK_EOF: return "end-of-file";
    case TOK_NAME: return "identifier";
    case TOK_INT_LIT: return "integer literal";
    case TOK_FLOAT_LIT: return "float literal";
    case TOK_DOUBLE_LIT: return "double literal";
    case TOK_STR_LIT: return "string literal";
    case TOK_CHAR_LIT: return "char literal";
    case TOK_LPAREN: return "'('";
    case TOK_RPAREN: return "')'";
    case TOK_LBRACE: return "'{'";
    case TOK_RBRACE: return "'}'";
    case TOK_SEMI: return "';'";
    case TOK_COMMA: return "','";
    case TOK_PLUS: return "'+'";
    case TOK_MINUS: return "'-'";
    case TOK_STAR: return "'*'";
    case TOK_SLASH: return "'/'";
    case TOK_ASSIGN: return "'='";
    case TOK_EQ: return "'=='";
    case TOK_NE: return "'!='";
    case TOK_LT: return "'<'";
    case TOK_GT: return "'>'";
    case TOK_LE: return "'<='";
    case TOK_GE: return "'>='";
    case TOK_IF: return "'if'";
    case TOK_ELSE: return "'else'";
    case TOK_FOR: return "'for'";
    case TOK_WHILE: return "'while'";
    case TOK_DO: return "'do'";
    case TOK_RETURN: return "'return'";
    case TOK_INT: return "'int'";
    case TOK_FLOAT: return "'float'";
    case TOK_VOID: return "'void'";
    case TOK_COLON: return "':'";
    default: return "<token>";
    }
}
