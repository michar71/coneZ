/*
 * type.c — C type parsing and promotion rules
 */
#include "c2wasm.h"

int is_type_keyword(int t) {
    return t == TOK_INT || t == TOK_FLOAT || t == TOK_DOUBLE ||
           t == TOK_VOID || t == TOK_CHAR || t == TOK_STATIC ||
           t == TOK_CONST || t == TOK_UNSIGNED || t == TOK_LONG ||
           t == TOK_SHORT || t == TOK_SIGNED || t == TOK_BOOL;
}

CType parse_type_spec(void) {
    int is_static = 0;
    int is_const = 0;
    int is_unsigned = 0;
    int long_count = 0;
    CType base = CT_INT;
    int has_base = 0;

    /* Consume qualifiers and type keywords */
    for (;;) {
        if (tok == TOK_STATIC) { is_static = 1; next_token(); continue; }
        if (tok == TOK_CONST)  { is_const = 1; next_token(); continue; }
        if (tok == TOK_UNSIGNED) { is_unsigned = 1; next_token(); continue; }
        if (tok == TOK_LONG) { long_count++; next_token(); continue; }
        if (tok == TOK_SHORT) {
            fprintf(stderr, "%s:%d: warning: 'short' treated as int\n",
                    src_file ? src_file : "<input>", line_num);
            next_token(); continue;
        }
        if (tok == TOK_SIGNED) { next_token(); continue; }
        if (tok == TOK_BOOL)   { base = CT_INT; has_base = 1; next_token(); break; }
        if (tok == TOK_INT)    { base = CT_INT; has_base = 1; next_token(); break; }
        if (tok == TOK_FLOAT)  { base = CT_FLOAT; has_base = 1; next_token(); break; }
        if (tok == TOK_DOUBLE) { base = CT_DOUBLE; has_base = 1; next_token(); break; }
        if (tok == TOK_VOID)   { base = CT_VOID; has_base = 1; next_token(); break; }
        if (tok == TOK_CHAR)   { base = CT_CHAR; has_base = 1; next_token(); break; }
        break;
    }

    (void)is_static;
    (void)is_const;

    if (is_unsigned)
        fprintf(stderr, "%s:%d: warning: 'unsigned' ignored (all ops use signed semantics)\n",
                src_file ? src_file : "<input>", line_num);

    /* 'long long' → CT_LONG_LONG (i64) */
    if (long_count >= 2 && !has_base) {
        base = CT_LONG_LONG;
    } else if (!has_base && (is_unsigned || long_count > 0)) {
        base = CT_INT;
    }

    /* Skip pointer star — we treat pointers as i32 */
    int saw_pointer = 0;
    while (tok == TOK_STAR) { next_token(); saw_pointer = 1; }

    /* Store for callers' benefit */
    type_had_pointer = saw_pointer;
    type_had_const = is_const;

    return base;
}
