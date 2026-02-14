/*
 * assemble.c — WASM module assembly (adapted from bas2wasm)
 */
#include "c2wasm.h"

/* Function type deduplication */
typedef struct { int np; uint8_t p[8]; int nr; uint8_t r[2]; } FType;
static FType ftypes[128];
static int nftypes;

static int find_or_add_ftype(int np, const uint8_t *p, int nr, const uint8_t *r) {
    for (int i = 0; i < nftypes; i++) {
        if (ftypes[i].np != np || ftypes[i].nr != nr) continue;
        int match = 1;
        for (int j = 0; j < np && match; j++) if (ftypes[i].p[j] != p[j]) match = 0;
        for (int j = 0; j < nr && match; j++) if (ftypes[i].r[j] != r[j]) match = 0;
        if (match) return i;
    }
    if (nftypes >= 128) { cw_fatal("too many function types\n"); }
    ftypes[nftypes].np = np;
    memcpy(ftypes[nftypes].p, p, np);
    ftypes[nftypes].nr = nr;
    memcpy(ftypes[nftypes].r, r, nr);
    return nftypes++;
}

/* Find the function index for a named function (for export) */
static int find_func_by_name(const char *name) {
    for (int i = 0; i < nfuncs; i++)
        if (func_bufs[i].name && strcmp(func_bufs[i].name, name) == 0)
            return i;
    return -1;
}

Buf assemble_to_buf(void) {
    nftypes = 0;
    Buf out; buf_init(&out);

    /* WASM magic + version */
    buf_bytes(&out, "\0asm", 4);
    uint8_t ver[4] = {1,0,0,0};
    buf_bytes(&out, ver, 4);

    /* --- Build import remap table --- */
    int imp_remap[IMP_COUNT];
    int num_used_imports = 0;
    for (int i = 0; i < IMP_COUNT; i++) {
        if (imp_used[i]) imp_remap[i] = num_used_imports++;
        else imp_remap[i] = -1;
    }

    /* --- Patch call targets in all code buffers --- */
    for (int i = 0; i < nfuncs; i++) {
        FuncCtx *f = &func_bufs[i];
        if (f->ncall_fixups == 0) continue;
        /* Sort fixups by code position (for-loop increment splicing can
         * produce out-of-order entries) */
        for (int a = 0; a < f->ncall_fixups - 1; a++)
            for (int b = a + 1; b < f->ncall_fixups; b++)
                if (f->call_fixups[a] > f->call_fixups[b]) {
                    int tmp = f->call_fixups[a];
                    f->call_fixups[a] = f->call_fixups[b];
                    f->call_fixups[b] = tmp;
                }
        Buf nc; buf_init(&nc);
        int fix = 0;
        for (int pos = 0; pos < f->code.len; ) {
            if (fix < f->ncall_fixups && pos == f->call_fixups[fix]) {
                uint32_t old_idx = 0; int shift = 0; uint8_t b;
                do {
                    b = f->code.data[pos++];
                    old_idx |= (uint32_t)(b & 0x7F) << shift;
                    shift += 7;
                } while (b & 0x80);
                uint32_t new_idx;
                if ((int)old_idx < IMP_COUNT)
                    new_idx = imp_remap[old_idx];
                else
                    new_idx = num_used_imports + (old_idx - IMP_COUNT);
                buf_uleb(&nc, new_idx);
                fix++;
            } else {
                buf_byte(&nc, f->code.data[pos++]);
            }
        }
        if (fix != f->ncall_fixups) {
            cw_fatal("c2wasm: BUG: %d call fixups unconsumed in %s\n",
                    f->ncall_fixups - fix, f->name ? f->name : "?");
        }
        cw_free(f->code.data);
        f->code = nc;
    }

    /* --- Collect type indices for used imports --- */
    int imp_type_idx[IMP_COUNT];
    for (int i = 0; i < IMP_COUNT; i++) {
        if (!imp_used[i]) continue;
        const ImportDef *d = &imp_defs[i];
        imp_type_idx[i] = find_or_add_ftype(d->np, d->p, d->nr, d->r);
    }

    /* Local function types */
    int local_type_idx[MAX_FUNCS];
    for (int i = 0; i < nfuncs; i++) {
        FuncCtx *f = &func_bufs[i];
        uint8_t ret_wasm = ctype_to_wasm(f->return_type);
        if (f->return_type == CT_VOID) {
            local_type_idx[i] = find_or_add_ftype(f->nparams, f->param_wasm_types, 0, NULL);
        } else {
            local_type_idx[i] = find_or_add_ftype(f->nparams, f->param_wasm_types, 1, &ret_wasm);
        }
    }

    /* --- Type Section (1) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, nftypes);
        for (int i = 0; i < nftypes; i++) {
            buf_byte(&sec, 0x60);
            buf_uleb(&sec, ftypes[i].np);
            buf_bytes(&sec, ftypes[i].p, ftypes[i].np);
            buf_uleb(&sec, ftypes[i].nr);
            buf_bytes(&sec, ftypes[i].r, ftypes[i].nr);
        }
        buf_section(&out, 1, &sec);
        buf_free(&sec);
    }

    /* --- Import Section (2) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, num_used_imports);
        for (int i = 0; i < IMP_COUNT; i++) {
            if (!imp_used[i]) continue;
            buf_str(&sec, "env");
            buf_str(&sec, imp_defs[i].name);
            buf_byte(&sec, 0x00);
            buf_uleb(&sec, imp_type_idx[i]);
        }
        buf_section(&out, 2, &sec);
        buf_free(&sec);
    }

    /* --- Function Section (3) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, nfuncs);
        for (int i = 0; i < nfuncs; i++)
            buf_uleb(&sec, local_type_idx[i]);
        buf_section(&out, 3, &sec);
        buf_free(&sec);
    }

    /* --- Memory Section (5) --- */
    {
        /* Calculate minimum pages needed for data + FMT_BUF + some heap */
        int min_bytes = FMT_BUF_ADDR + 256;  /* printf buffer area + margin */
        if (data_len > min_bytes) min_bytes = data_len;
        int min_pages = (min_bytes + 0xFFFF) >> 16;
        if (min_pages < 1) min_pages = 1;

        Buf sec; buf_init(&sec);
        buf_uleb(&sec, 1);
        buf_byte(&sec, 0x00);
        buf_uleb(&sec, min_pages);
        buf_section(&out, 5, &sec);
        buf_free(&sec);
    }

    /* --- Global Section (6) --- */
    {
        int heap_start = (data_len + 3) & ~3;

        Buf sec; buf_init(&sec);
        buf_uleb(&sec, nglobals);

        /* Global 0: _heap_ptr */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, heap_start); buf_byte(&sec, OP_END);

        /* Global 1: __line */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, 0); buf_byte(&sec, OP_END);

        /* User globals (2-based) — use stored init values */
        for (int i = 0; i < nsym; i++) {
            if (syms[i].kind != SYM_GLOBAL) continue;
            uint8_t gt = ctype_to_wasm(syms[i].ctype);
            buf_byte(&sec, gt); buf_byte(&sec, 0x01);
            if (gt == WASM_F64) {
                buf_byte(&sec, OP_F64_CONST);
                buf_f64(&sec, syms[i].init_dval);
            } else if (gt == WASM_F32) {
                buf_byte(&sec, OP_F32_CONST);
                buf_f32(&sec, syms[i].init_fval);
            } else if (gt == WASM_I64) {
                buf_byte(&sec, OP_I64_CONST);
                buf_sleb64(&sec, syms[i].init_llval);
            } else {
                buf_byte(&sec, OP_I32_CONST);
                buf_sleb(&sec, syms[i].init_ival);
            }
            buf_byte(&sec, OP_END);
        }

        buf_section(&out, 6, &sec);
        buf_free(&sec);
    }

    /* --- Export Section (7) --- */
    {
        Buf sec; buf_init(&sec);
        int nexports = 2; /* memory + __line always exported */
        int setup_idx = find_func_by_name("setup");
        int loop_idx = find_func_by_name("loop");
        if (setup_idx >= 0) nexports++;
        if (loop_idx >= 0) nexports++;

        buf_uleb(&sec, nexports);

        if (setup_idx >= 0) {
            buf_str(&sec, "setup");
            buf_byte(&sec, 0x00);
            buf_uleb(&sec, num_used_imports + setup_idx);
        }
        if (loop_idx >= 0) {
            buf_str(&sec, "loop");
            buf_byte(&sec, 0x00);
            buf_uleb(&sec, num_used_imports + loop_idx);
        }

        buf_str(&sec, "memory");
        buf_byte(&sec, 0x02);
        buf_uleb(&sec, 0);

        buf_str(&sec, "__line");
        buf_byte(&sec, 0x03);
        buf_uleb(&sec, GLOBAL_LINE);

        buf_section(&out, 7, &sec);
        buf_free(&sec);
    }

    /* --- Code Section (10) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, nfuncs);
        for (int i = 0; i < nfuncs; i++) {
            FuncCtx *f = &func_bufs[i];
            Buf body; buf_init(&body);

            if (f->nlocals == 0) {
                buf_uleb(&body, 0);
            } else {
                int ngroups = 0;
                int counts[CW_MAX_LOCALS]; uint8_t types[CW_MAX_LOCALS];
                int j = 0;
                while (j < f->nlocals) {
                    uint8_t t = f->local_types[j];
                    int c = 0;
                    while (j < f->nlocals && f->local_types[j] == t) { c++; j++; }
                    counts[ngroups] = c;
                    types[ngroups] = t;
                    ngroups++;
                }
                buf_uleb(&body, ngroups);
                for (int g = 0; g < ngroups; g++) {
                    buf_uleb(&body, counts[g]);
                    buf_byte(&body, types[g]);
                }
            }

            buf_bytes(&body, f->code.data, f->code.len);

            buf_uleb(&sec, body.len);
            buf_bytes(&sec, body.data, body.len);
            buf_free(&body);
        }
        buf_section(&out, 10, &sec);
        buf_free(&sec);
    }

    /* --- Data Section (11) --- */
    if (data_len > 0) {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, 1);
        buf_byte(&sec, 0x00);
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, 0); buf_byte(&sec, OP_END);
        buf_uleb(&sec, data_len);
        buf_bytes(&sec, data_buf, data_len);
        buf_section(&out, 11, &sec);
        buf_free(&sec);
    }

    return out;
}

void assemble(const char *outpath) {
    Buf out = assemble_to_buf();

    FILE *fp = fopen(outpath, "wb");
    if (!fp) { cw_fatal("Cannot open %s for writing\n", outpath); }
    if ((int)fwrite(out.data, 1, out.len, fp) != out.len) {
        fclose(fp);
        buf_free(&out);
        cw_fatal("Write error on %s\n", outpath);
    }
    fclose(fp);
    cw_info("Wrote %d bytes to %s\n", out.len, outpath);

    /* Count used imports for info message */
    int num_imp = 0;
    for (int i = 0; i < IMP_COUNT; i++)
        if (imp_used[i]) num_imp++;
    cw_info("  %d imports, %d functions, %d globals, %d bytes data\n",
           num_imp, nfuncs, nglobals, data_len);
    buf_free(&out);
}
