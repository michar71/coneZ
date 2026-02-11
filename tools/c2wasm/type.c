/*
 * type.c — C type parsing and promotion rules
 */
#include "c2wasm.h"

int is_type_keyword(int t) {
    return t == TOK_INT || t == TOK_FLOAT || t == TOK_DOUBLE ||
           t == TOK_VOID || t == TOK_CHAR || t == TOK_STATIC ||
           t == TOK_CONST || t == TOK_UNSIGNED || t == TOK_LONG ||
           t == TOK_SHORT || t == TOK_SIGNED || t == TOK_BOOL ||
           t == TOK_INT8 || t == TOK_INT16 || t == TOK_INT32 ||
           t == TOK_INT64 || t == TOK_SIZE_T ||
           t == TOK_UINT8 || t == TOK_UINT16 || t == TOK_UINT32 ||
           t == TOK_UINT64;
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
        if (tok == TOK_INT8 || tok == TOK_INT16 || tok == TOK_INT32)
                               { base = CT_INT; has_base = 1; next_token(); break; }
        if (tok == TOK_INT64)  { base = CT_LONG_LONG; has_base = 1; next_token(); break; }
        if (tok == TOK_UINT8 || tok == TOK_UINT16 || tok == TOK_UINT32 || tok == TOK_SIZE_T)
                               { is_unsigned = 1; base = CT_INT; has_base = 1; next_token(); break; }
        if (tok == TOK_UINT64) { is_unsigned = 1; base = CT_LONG_LONG; has_base = 1; next_token(); break; }
        break;
    }

    /* Consume trailing qualifiers after base type (e.g. "int const") */
    for (;;) {
        if (tok == TOK_CONST)  { is_const = 1; next_token(); continue; }
        if (tok == TOK_LONG)   { long_count++; next_token(); continue; }
        break;
    }

    (void)is_static;
    (void)is_const;

    /* 'long long' / 'long long int' → CT_LONG_LONG (i64) */
    if (long_count >= 2 && (base == CT_INT || !has_base)) {
        base = CT_LONG_LONG;
    } else if (long_count == 1 && !has_base) {
        fprintf(stderr, "%s:%d: warning: 'long' treated as int (32-bit on WASM)\n",
                src_file ? src_file : "<input>", line_num);
        base = CT_INT;
    } else if (!has_base && is_unsigned) {
        base = CT_INT;
    }

    /* Apply unsigned: CT_INT→CT_UINT, CT_LONG_LONG→CT_ULONG_LONG, CT_CHAR→CT_UINT */
    if (is_unsigned) {
        if (base == CT_INT || base == CT_CHAR) base = CT_UINT;
        else if (base == CT_LONG_LONG) base = CT_ULONG_LONG;
    }

    /* Skip pointer star — we treat pointers as i32 */
    int saw_pointer = 0;
    while (tok == TOK_STAR) { next_token(); saw_pointer = 1; }

    /* Store for callers' benefit */
    type_had_pointer = saw_pointer;
    type_had_const = is_const;
    type_had_unsigned = is_unsigned;

    return base;
}
