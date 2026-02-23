/*
 * type_ops.c — Extended type system operations for pointers and arrays
 */
#include "c2wasm.h"

/* Create a base type */
TypeInfo type_base(CType ct) {
    TypeInfo t;
    t.kinds[0] = TYPE_BASE;
    t.base = ct;
    t.sizes[0] = 0;
    t.depth = 0;
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
        return t.sizes[0] * type_element_size(t);
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
    for (int i = 0; i < a.depth; i++) {
        if (a.kinds[i] != b.kinds[i]) return 0;
        if (a.kinds[i] == TYPE_ARRAY && a.sizes[i] != b.sizes[i]) return 0;
    }
    return 1;
}
