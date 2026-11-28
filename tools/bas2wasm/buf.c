/*
 * buf.c — byte buffer operations
 */
#include "bas2wasm.h"

void buf_init(Buf *b) { b->data = NULL; b->len = b->cap = 0; }

/* Make room for `need` more bytes. Returns 0 if memory ran out: the buffer is
 * left as it was, the compile is failed, and the caller must not write. (The
 * realloc result used to be stored unchecked -- the old buffer leaked, cap was
 * raised anyway, and the next byte went through a null pointer.) */
int buf_grow(Buf *b, int need) {
    if (b->len + need <= b->cap) return 1;
    int nc = b->cap ? b->cap * 2 : 256;
    while (nc < b->len + need) nc *= 2;
    uint8_t *nd = (uint8_t *)bw_realloc(b->data, nc);
    if (!nd) {
        if (!had_error) { bw_error("bas2wasm: out of memory\n"); had_error = 1; }
        return 0;
    }
    b->data = nd;
    b->cap = nc;
    return 1;
}

void buf_byte(Buf *b, uint8_t v) {
    if (!buf_grow(b, 1)) return;
    b->data[b->len++] = v;
}

void buf_bytes(Buf *b, const void *p, int n) {
    if (n <= 0 || !buf_grow(b, n)) return;
    memcpy(b->data + b->len, p, n); b->len += n;
}

void buf_free(Buf *b) { bw_free(b->data); buf_init(b); }

void buf_uleb(Buf *b, uint32_t v) {
    do {
        uint8_t x = v & 0x7F; v >>= 7;
        if (v) x |= 0x80;
        buf_byte(b, x);
    } while (v);
}

void buf_sleb(Buf *b, int32_t v) {
    int more = 1;
    while (more) {
        uint8_t x = v & 0x7F; v >>= 7;
        if ((v == 0 && !(x & 0x40)) || (v == -1 && (x & 0x40)))
            more = 0;
        else x |= 0x80;
        buf_byte(b, x);
    }
}

void buf_sleb64(Buf *b, int64_t v) {
    int more = 1;
    while (more) {
        uint8_t x = v & 0x7F;
        v >>= 7;
        if ((v == 0 && !(x & 0x40)) || (v == -1 && (x & 0x40)))
            more = 0;
        else
            x |= 0x80;
        buf_byte(b, x);
    }
}

void buf_f32(Buf *b, float v) {
    uint8_t tmp[4]; memcpy(tmp, &v, 4); buf_bytes(b, tmp, 4);
}

void buf_str(Buf *b, const char *s) {
    int n = strlen(s); buf_uleb(b, n); buf_bytes(b, s, n);
}

void buf_section(Buf *out, int id, Buf *content) {
    buf_byte(out, id);
    buf_uleb(out, content->len);
    buf_bytes(out, content->data, content->len);
}
