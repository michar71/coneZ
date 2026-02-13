/*
 * assemble.c — WASM module assembly
 */
#include "bas2wasm.h"

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
    if (nftypes >= 128) { bw_fatal("too many function types\n"); }
    ftypes[nftypes].np = np;
    memcpy(ftypes[nftypes].p, p, np);
    ftypes[nftypes].nr = nr;
    memcpy(ftypes[nftypes].r, r, nr);
    return nftypes++;
}

Buf assemble_to_buf(void) {
    nftypes = 0;
    Buf out; buf_init(&out);

    /* WASM magic + version */
    buf_bytes(&out, "\0asm", 4);
    uint8_t ver[4] = {1,0,0,0};
    buf_bytes(&out, ver, 4);

    /* --- Build import remap table (compact to only used imports) --- */
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
        Buf nc; buf_init(&nc);
        int fix = 0;
        for (int pos = 0; pos < f->code.len; ) {
            if (fix < f->ncall_fixups && pos == f->call_fixups[fix]) {
                /* Decode old uleb128 */
                uint32_t old_idx = 0; int shift = 0; uint8_t b;
                do {
                    b = f->code.data[pos++];
                    old_idx |= (uint32_t)(b & 0x7F) << shift;
                    shift += 7;
                } while (b & 0x80);
                /* Remap */
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
        bw_free(f->code.data);
        f->code = nc;
    }

    /* --- Collect type indices for used imports --- */
    int imp_type_idx[IMP_COUNT];
    for (int i = 0; i < IMP_COUNT; i++) {
        if (!imp_used[i]) continue;
        const ImportDef *d = &imp_defs[i];
        imp_type_idx[i] = find_or_add_ftype(d->np, d->p, d->nr, d->r);
    }

    /* Local function types: setup is () → (), SUBs use actual param/return types */
    int local_type_idx[MAX_FUNCS];
    for (int i = 0; i < nfuncs; i++) {
        FuncCtx *f = &func_bufs[i];
        if (i == 0) {
            local_type_idx[i] = find_or_add_ftype(0, NULL, 0, NULL);
        } else {
            uint8_t result;
            if (vars[f->sub_var].type_set && vars[f->sub_var].type == T_F32) result = WASM_F32;
            else if (vars[f->sub_var].type_set && vars[f->sub_var].type == T_I64) result = WASM_I64;
            else result = WASM_I32;
            local_type_idx[i] = find_or_add_ftype(f->nparams, f->param_types, 1, &result);
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
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, 1);
        buf_byte(&sec, 0x00);
        buf_uleb(&sec, 1);
        buf_section(&out, 5, &sec);
        buf_free(&sec);
    }

    /* --- Global Section (6) --- */
    {
        int data_table_start = (data_len + 3) & ~3;
        int total_data = data_table_start;
        if (ndata_items > 0)
            total_data += 4 + ndata_items * 8;
        int heap_start = (total_data + 3) & ~3;

        Buf sec; buf_init(&sec);
        int nglobals = 4 + nvar;
        buf_uleb(&sec, nglobals);
        /* Global 0: __line */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, 0); buf_byte(&sec, OP_END);
        /* Global 1: _heap_ptr */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, heap_start); buf_byte(&sec, OP_END);
        /* Global 2: _data_base */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, data_table_start); buf_byte(&sec, OP_END);
        /* Global 3: _data_idx */
        buf_byte(&sec, WASM_I32); buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, 0); buf_byte(&sec, OP_END);
        /* Variable globals */
        for (int i = 0; i < nvar; i++) {
            uint8_t gt = WASM_I32;
            if (vars[i].type_set && vars[i].type == T_F32) gt = WASM_F32;
            else if (vars[i].type_set && vars[i].type == T_I64) gt = WASM_I64;
            buf_byte(&sec, gt); buf_byte(&sec, 0x01);
            if (gt == WASM_F32) {
                buf_byte(&sec, OP_F32_CONST);
                float z = 0.0f; buf_f32(&sec, z);
            } else if (gt == WASM_I64) {
                buf_byte(&sec, OP_I64_CONST);
                buf_sleb64(&sec, 0);
            } else {
                buf_byte(&sec, OP_I32_CONST);
                buf_sleb(&sec, 0);
            }
            buf_byte(&sec, OP_END);
        }
        buf_section(&out, 6, &sec);
        buf_free(&sec);
    }

    /* --- Export Section (7) --- */
    {
        Buf sec; buf_init(&sec);
        buf_uleb(&sec, 3);
        buf_str(&sec, "setup");
        buf_byte(&sec, 0x00);
        buf_uleb(&sec, num_used_imports + 0);
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
                int counts[128]; uint8_t types[128];
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

            if (i == 0) {
                buf_byte(&body, OP_END);
            }

            buf_uleb(&sec, body.len);
            buf_bytes(&sec, body.data, body.len);
            buf_free(&body);
        }
        buf_section(&out, 10, &sec);
        buf_free(&sec);
    }

    /* --- Data Section (11) --- */
    {
        int data_table_start = (data_len + 3) & ~3;
        int total_data = data_table_start;
        if (ndata_items > 0)
            total_data += 4 + ndata_items * 8;

        if (total_data > 0) {
            uint8_t *full_data = bw_calloc(total_data, 1);
#ifdef BAS2WASM_USE_PSRAM
            bw_psram_read(data_buf, full_data, data_len);
#else
            memcpy(full_data, data_buf, data_len);
#endif
            if (ndata_items > 0) {
                uint8_t *p = full_data + data_table_start;
                int32_t count = ndata_items;
                memcpy(p, &count, 4); p += 4;
                for (int i = 0; i < ndata_items; i++) {
                    DataItem di;
#ifdef BAS2WASM_USE_PSRAM
                    bw_psram_read(data_items + i * sizeof(DataItem),
                                  &di, sizeof(DataItem));
#else
                    di = data_items[i];
#endif
                    int32_t type_tag = 0;
                    int32_t value = 0;
                    switch (di.type) {
                    case T_I32: type_tag = 0; value = di.ival; break;
                    case T_F32: type_tag = 1; memcpy(&value, &di.fval, 4); break;
                    case T_STR: type_tag = 2; value = di.str_off; break;
                    case T_I64: type_tag = 0; value = (int32_t)di.ival; break;
                    }
                    memcpy(p, &type_tag, 4); p += 4;
                    memcpy(p, &value, 4); p += 4;
                }
            }

            Buf sec; buf_init(&sec);
            buf_uleb(&sec, 1);
            buf_byte(&sec, 0x00);
            buf_byte(&sec, OP_I32_CONST); buf_sleb(&sec, 0); buf_byte(&sec, OP_END);
            buf_uleb(&sec, total_data);
            buf_bytes(&sec, full_data, total_data);
            buf_section(&out, 11, &sec);
            buf_free(&sec);
            bw_free(full_data);
        }
    }

    return out;
}

void assemble(const char *outpath) {
    Buf out = assemble_to_buf();

    FILE *fp = fopen(outpath, "wb");
    if (!fp) { bw_fatal("Cannot open %s for writing\n", outpath); }
    if ((int)fwrite(out.data, 1, out.len, fp) != out.len) {
        fclose(fp);
        buf_free(&out);
        bw_fatal("Write error on %s\n", outpath);
    }
    fclose(fp);
    bw_info("Wrote %d bytes to %s\n", out.len, outpath);

    /* Count used imports for info message */
    int num_imp = 0;
    for (int i = 0; i < IMP_COUNT; i++)
        if (imp_used[i]) num_imp++;
    bw_info("  %d imports, %d local functions, %d globals, %d bytes data (%d DATA items)\n",
           num_imp, nfuncs, 4 + nvar, data_len, ndata_items);
    buf_free(&out);
}
