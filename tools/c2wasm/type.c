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
           t == TOK_UINT64 || t == TOK_STRUCT;
}

/* Forward decl for struct-decl body parsing (used inside parse_type_spec) */
static int parse_struct_body(int sid);

/* Parse a "struct Tag" or "struct Tag { body }" type reference.
 * Sets type_last_struct_id. Returns CT_STRUCT. */
static CType parse_struct_spec(void) {
    next_token();  /* consume 'struct' */

    char tag[32] = {0};
    if (tok == TOK_NAME) {
        int n = (int)strlen(tok_sval);
        if (n > (int)sizeof(tag) - 1) n = (int)sizeof(tag) - 1;
        memcpy(tag, tok_sval, n);
        tag[n] = 0;
        next_token();
    }

    int sid;
    if (tok == TOK_LBRACE) {
        /* Declaration: struct [tag] { fields } */
        if (!tag[0]) {
            error_at("anonymous structs are not supported");
            /* create a placeholder to consume the body cleanly */
            snprintf(tag, sizeof(tag), "__anon_%d", n_struct_types);
        }
        sid = struct_register(tag);
        if (sid >= 0 && struct_types[sid].complete) {
            error_fmt("redefinition of struct %s", tag);
        }
        parse_struct_body(sid);
    } else {
        /* Reference: struct tag — must exist or forward-declare */
        if (!tag[0]) {
            error_at("expected struct tag or body after 'struct'");
            type_last_struct_id = -1;
            return CT_STRUCT;
        }
        sid = struct_find(tag);
        if (sid < 0) sid = struct_register(tag);  /* incomplete forward ref */
    }

    type_last_struct_id = sid;
    return CT_STRUCT;
}

/* Parse the body of a struct: { type name; type name[N]; ... } */
static int parse_struct_body(int sid) {
    expect(TOK_LBRACE);
    while (tok != TOK_RBRACE && tok != TOK_EOF) {
        /* Each field: type_spec declarator ; */
        int saved_ptr = type_had_pointer;
        int saved_sid = type_last_struct_id;
        type_had_pointer = 0;
        type_last_struct_id = -1;
        CType field_base = parse_type_spec();
        int base_is_ptr = type_had_pointer;
        int base_struct = type_last_struct_id;
        type_had_pointer = saved_ptr;
        (void)saved_sid;

        do {
            int is_ptr = base_is_ptr;
            while (tok == TOK_STAR) { is_ptr++; next_token(); }

            if (tok != TOK_NAME) { error_at("expected field name"); synchronize(1, 1, 0); break; }
            char fname[32];
            int fnlen = (int)strlen(tok_sval);
            if (fnlen > (int)sizeof(fname) - 1) fnlen = (int)sizeof(fname) - 1;
            memcpy(fname, tok_sval, fnlen);
            fname[fnlen] = 0;
            next_token();

            int is_array = 0;
            int array_size = 0;
            if (tok == TOK_LBRACKET) {
                next_token();
                if (tok == TOK_INT_LIT) { array_size = (int)tok_i64; next_token(); }
                else { error_at("struct field array needs constant size"); array_size = 1; }
                expect(TOK_RBRACKET);
                is_array = 1;
            }

            TypeInfo ft;
            if (field_base == CT_STRUCT && base_struct >= 0) {
                ft = type_base_struct(base_struct);
            } else {
                ft = type_base(field_base);
            }
            if (is_ptr) {
                /* Collapse multiple pointer levels to one (pointer = i32) */
                ft = type_pointer(ft);
            }
            if (is_array) {
                ft = type_array(ft, array_size);
            }
            if (sid >= 0) struct_add_field(sid, fname, ft);
        } while (accept(TOK_COMMA));
        expect(TOK_SEMI);
    }
    expect(TOK_RBRACE);
    if (sid >= 0) {
        StructType *st = &struct_types[sid];
        /* Round total size up to 4 for natural alignment */
        st->size = (st->size + 3) & ~3;
        st->complete = 1;
    }
    return sid;
}

CType parse_type_spec(void) {
    int is_static = 0;
    int is_const = 0;
    int is_unsigned = 0;
    int long_count = 0;
    CType base = CT_INT;
    int has_base = 0;

    type_last_struct_id = -1;

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
        if (tok == TOK_STRUCT) { base = parse_struct_spec(); has_base = 1; break; }
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

    /* Apply unsigned: CT_INT→CT_UINT, CT_CHAR→CT_UCHAR, CT_LONG_LONG→CT_ULONG_LONG */
    if (is_unsigned) {
        if (base == CT_CHAR) base = CT_UCHAR;
        else if (base == CT_INT) base = CT_UINT;
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
