/*
 * type_ops.c — Extended type system operations for pointers and arrays
 */
#include "c2wasm.h"

#ifdef C2WASM_EMBEDDED
StructType *struct_types;
#else
StructType struct_types[MAX_STRUCT_TYPES];
#endif
int n_struct_types;

/* Create a base type */
TypeInfo type_base(CType ct) {
    TypeInfo t;
    t.kinds[0] = TYPE_BASE;
    t.base = ct;
    t.sizes[0] = 0;
    t.depth = 0;
    t.struct_id = -1;
    return t;
}

/* Create a struct base type */
TypeInfo type_base_struct(int struct_id) {
    TypeInfo t = type_base(CT_STRUCT);
    t.struct_id = struct_id;
    return t;
}

/* Create a pointer to a type */
TypeInfo type_pointer(TypeInfo base) {
    TypeInfo t;
    if (base.depth >= MAX_TYPE_DEPTH - 1) {
        error_at("type nesting too deep");
        return base;
    }
    /* Shift existing levels up */
    for (int i = base.depth; i >= 0; i--) {
        t.kinds[i + 1] = base.kinds[i];
        t.sizes[i + 1] = base.sizes[i];
    }
    t.kinds[0] = TYPE_POINTER;
    t.sizes[0] = -1;  /* Pointer marker */
    t.base = base.base;
    t.struct_id = base.struct_id;
    t.depth = base.depth + 1;
    return t;
}

/* Create an array type */
TypeInfo type_array(TypeInfo base, int size) {
    TypeInfo t;
    if (base.depth >= MAX_TYPE_DEPTH - 1) {
        error_at("type nesting too deep");
        return base;
    }
    if (size <= 0) {
        error_at("array size must be positive");
        size = 1;  /* Fallback */
    }
    /* Shift existing levels up */
    for (int i = base.depth; i >= 0; i--) {
        t.kinds[i + 1] = base.kinds[i];
        t.sizes[i + 1] = base.sizes[i];
    }
    t.kinds[0] = TYPE_ARRAY;
    t.sizes[0] = size;
    t.base = base.base;
    t.struct_id = base.struct_id;
    t.depth = base.depth + 1;
    return t;
}

/* Array-to-pointer decay: array becomes pointer to first element */
TypeInfo type_decay(TypeInfo t) {
    if (!type_is_array(t)) return t;
    /* Change outermost array to pointer */
    t.kinds[0] = TYPE_POINTER;
    t.sizes[0] = -1;
    return t;
}

/* Check if type is a pointer */
int type_is_pointer(TypeInfo t) {
    return t.depth > 0 && t.kinds[0] == TYPE_POINTER;
}

/* Check if type is an array */
int type_is_array(TypeInfo t) {
    return t.depth > 0 && t.kinds[0] == TYPE_ARRAY;
}

/* Check if type is a scalar (not array/pointer) */
int type_is_scalar(TypeInfo t) {
    return t.depth == 0;
}

/* Check if type is a (non-pointer, non-array) struct instance */
int type_is_struct(TypeInfo t) {
    return t.depth == 0 && t.base == CT_STRUCT && t.struct_id >= 0;
}

/* Check if type is a pointer to a struct */
int type_is_struct_ptr(TypeInfo t) {
    return t.depth > 0 && t.kinds[0] == TYPE_POINTER &&
           t.base == CT_STRUCT && t.struct_id >= 0;
}

/* Get the base CType */
CType type_base_ctype(TypeInfo t) {
    return t.base;
}

/* Get the size of the element type (for pointer/array element access) */
int type_element_size(TypeInfo t) {
    if (t.depth == 0) {
        /* Scalar type */
        switch (t.base) {
        case CT_CHAR:
        case CT_UCHAR: return 1;
        case CT_INT:
        case CT_UINT: return 4;
        case CT_FLOAT: return 4;
        case CT_LONG_LONG:
        case CT_ULONG_LONG:
        case CT_DOUBLE: return 8;
        case CT_STRUCT:
            if (t.struct_id >= 0 && t.struct_id < n_struct_types)
                return struct_types[t.struct_id].size;
            return 4;
        default: return 4;
        }
    }
    /* Pointer/array: element size depends on what we point to */
    /* Create type without outermost level */
    TypeInfo inner = t;
    inner.depth--;
    for (int i = 0; i <= inner.depth; i++) {
        inner.kinds[i] = t.kinds[i + 1];
        inner.sizes[i] = t.sizes[i + 1];
    }
    return type_sizeof(inner);
}

/* Get total size of type */
int type_sizeof(TypeInfo t) {
    if (t.depth == 0) {
        return type_element_size(t);
    }
    if (t.kinds[0] == TYPE_ARRAY) {
        int elem = type_element_size(t);
        if (elem > 0 && t.sizes[0] > INT_MAX / elem) {
            error_at("array type too large");
            return INT_MAX;
        }
        return t.sizes[0] * elem;
    }
    /* Pointer is always 4 bytes (i32) */
    return 4;
}

/* Get inner type (what we get after dereferencing once) */
TypeInfo type_deref(TypeInfo t) {
    if (t.depth == 0) {
        error_at("cannot dereference scalar type");
        return t;
    }
    TypeInfo inner = t;
    inner.depth--;
    inner.struct_id = t.struct_id;
    for (int i = 0; i <= inner.depth; i++) {
        inner.kinds[i] = t.kinds[i + 1];
        inner.sizes[i] = t.sizes[i + 1];
    }
    return inner;
}

/* Check if two types are compatible */
int type_compatible(TypeInfo a, TypeInfo b) {
    if (a.depth != b.depth) return 0;
    if (a.base != b.base) return 0;
    if (a.base == CT_STRUCT && a.struct_id != b.struct_id) return 0;
    for (int i = 0; i < a.depth; i++) {
        if (a.kinds[i] != b.kinds[i]) return 0;
        if (a.kinds[i] == TYPE_ARRAY && a.sizes[i] != b.sizes[i]) return 0;
    }
    return 1;
}

/* ================================================================
 *  Struct type registry
 * ================================================================ */

int struct_find(const char *tag) {
    if (!tag || !tag[0]) return -1;
    for (int i = 0; i < n_struct_types; i++)
        if (strcmp(struct_types[i].tag, tag) == 0) return i;
    return -1;
}

int struct_register(const char *tag) {
    int existing = struct_find(tag);
    if (existing >= 0) return existing;
    if (n_struct_types >= MAX_STRUCT_TYPES) {
        error_at("too many struct types");
        return -1;
    }
    int id = n_struct_types++;
    StructType *st = &struct_types[id];
    memset(st, 0, sizeof(*st));
    snprintf(st->tag, sizeof(st->tag), "%s", tag ? tag : "");
    st->nfields = 0;
    st->size = 0;
    st->complete = 0;
    return id;
}

int struct_add_field(int sid, const char *name, TypeInfo t) {
    if (sid < 0 || sid >= n_struct_types) return -1;
    StructType *st = &struct_types[sid];
    if (st->nfields >= MAX_STRUCT_FIELDS) {
        error_at("too many fields in struct");
        return -1;
    }
    /* Field alignment: natural alignment by element size, capped at 4 (wasm i32) */
    int fsize;
    int falign;
    if (t.depth == 0 && t.base == CT_STRUCT) {
        /* Nested struct — must be complete */
        if (t.struct_id < 0 || !struct_types[t.struct_id].complete) {
            error_at("nested struct field must use a complete struct type");
            return -1;
        }
        fsize = struct_types[t.struct_id].size;
        falign = 4;
    } else if (type_is_array(t)) {
        fsize = type_sizeof(t);
        int elem = type_element_size(t);
        falign = (elem >= 4) ? 4 : (elem >= 2 ? 2 : 1);
    } else {
        fsize = type_sizeof(t);
        falign = (fsize >= 4) ? 4 : (fsize >= 2 ? 2 : 1);
    }
    int off = (st->size + (falign - 1)) & ~(falign - 1);
    StructField *f = &st->fields[st->nfields++];
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->type = t;
    f->offset = off;
    st->size = off + fsize;
    return st->nfields - 1;
}

int struct_find_field(int sid, const char *name) {
    if (sid < 0 || sid >= n_struct_types) return -1;
    StructType *st = &struct_types[sid];
    for (int i = 0; i < st->nfields; i++)
        if (strcmp(st->fields[i].name, name) == 0) return i;
    return -1;
}
