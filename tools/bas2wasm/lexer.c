/*
 * lexer.c — tokenizer for BASIC source
 */
#include "bas2wasm.h"

/* data_buf byte access — PSRAM path goes through page cache */
#ifdef BAS2WASM_USE_PSRAM
static void dbuf_put(int pos, char c) {
    uint8_t b = (uint8_t)c;
    bw_psram_write(data_buf + pos, &b, 1);
}
#else
#define dbuf_put(pos, c) (data_buf[pos] = (c))
#endif

static const char *kwd[] = {
    "AND","OR","FORMAT","SUB","END","RETURN","LOCAL",
    "WHILE","FOR","TO","IF","ELSE","THEN","DIM","UBOUND",
    "BYE","BREAK","RESUME","PRINTS","STEP","CONST","NOT","XOR",
    "SELECT","CASE","DO","LOOP","UNTIL","EXIT","SWAP","IS",
    "DATA","READ","RESTORE","MOD","NEXT","WEND","FUNCTION",
    "OPEN","CLOSE","AS","KILL","MKDIR","RMDIR","ELSEIF",
    "REDIM","ERASE","PRESERVE","OPTION","BASE",NULL
};

/* ================================================================
 *  $INCLUDE metacommand (QuickBASIC-style)
 *
 *    '$INCLUDE: 'file.bi'
 *    REM $INCLUDE: 'file.bi'
 *
 *  The named file's contents are spliced into `source` at the point
 *  after the directive line (same technique c2wasm uses for #include).
 *  Filenames resolve relative to bw_include_dir. line_num is restored
 *  when the included region is exhausted so error messages stay sane.
 * ================================================================ */
#define BW_MAX_INCLUDE_DEPTH 8
typedef struct { int restore_line; int end_pos; } BwIncFrame;
static BwIncFrame bw_inc_stk[BW_MAX_INCLUDE_DEPTH];
static int bw_inc_sp;

void bw_include_reset(void) { bw_inc_sp = 0; }

static void bw_include_check_boundary(void) {
    while (bw_inc_sp > 0 && src_pos >= bw_inc_stk[bw_inc_sp - 1].end_pos) {
        bw_inc_sp--;
        line_num = bw_inc_stk[bw_inc_sp].restore_line;
    }
}

/* Returns 1 if `line` was a $INCLUDE directive (consumed/handled),
 * 0 if it's an ordinary line the caller should process normally. */
static int bw_try_include(const char *line) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\'') {
        p++;
    } else if ((p[0] == 'R' || p[0] == 'r') && (p[1] == 'E' || p[1] == 'e') &&
               (p[2] == 'M' || p[2] == 'm') && (p[3] == ' ' || p[3] == '\t')) {
        p += 3;
    } else {
        return 0;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '$') return 0;
    p++;
    static const char *kw = "INCLUDE";
    for (int k = 0; k < 7; k++) {
        if (tolower((unsigned char)p[k]) != tolower((unsigned char)kw[k])) return 0;
    }
    p += 7;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\'') { error_at("$INCLUDE: expected 'filename'"); return 1; }
    p++;
    char fname[128]; int fi = 0;
    while (*p && *p != '\'' && fi < 127) fname[fi++] = *p++;
    fname[fi] = 0;
    if (*p != '\'') { error_at("$INCLUDE: unterminated filename"); return 1; }

    if (bw_inc_sp >= BW_MAX_INCLUDE_DEPTH) {
        error_at("$INCLUDE nested too deep"); return 1;
    }

    char path[384];
    int dl = (int)strlen(bw_include_dir);
    if (dl + fi + 1 > (int)sizeof(path)) { error_at("$INCLUDE path too long"); return 1; }
    memcpy(path, bw_include_dir, dl);
    memcpy(path + dl, fname, fi + 1);

    FILE *f = fopen(path, "rb");
    if (!f) { error_at("$INCLUDE: cannot open file"); return 1; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 0) fsz = 0;
    char *fdata = (char *)bw_malloc(fsz + 2);
    if (!fdata) { fclose(f); error_at("$INCLUDE: out of memory"); return 1; }
    if (fsz > 0 && (long)fread(fdata, 1, fsz, f) != fsz) {
        fclose(f); bw_free(fdata); error_at("$INCLUDE: read error"); return 1;
    }
    fclose(f);
    fdata[fsz] = '\n';      /* ensure the last line terminates cleanly */
    fdata[fsz + 1] = 0;
    int flen = (int)fsz + 1;

    /* Splice fdata into source at src_pos (just past the directive line). */
    int remain = src_len - src_pos;
    char *ns = (char *)bw_malloc(src_pos + flen + remain + 1);
    if (!ns) { bw_free(fdata); error_at("$INCLUDE: out of memory"); return 1; }
    memcpy(ns, source, src_pos);
    memcpy(ns + src_pos, fdata, flen);
    memcpy(ns + src_pos + flen, source + src_pos, remain);
    ns[src_pos + flen + remain] = 0;
    bw_free(fdata);
    if (source_owned) bw_free(source);
    source = ns;
    source_owned = 1;
    src_len = src_pos + flen + remain;

    /* Shift any outer frames whose region extends past the splice point. */
    for (int i = 0; i < bw_inc_sp; i++)
        if (bw_inc_stk[i].end_pos > src_pos) bw_inc_stk[i].end_pos += flen;

    bw_inc_stk[bw_inc_sp].restore_line = line_num;
    bw_inc_stk[bw_inc_sp].end_pos = src_pos + flen;
    bw_inc_sp++;
    line_num = 0;   /* next_line()'s ++ makes the first included line == 1 */
    return 1;
}

int next_line(void) {
    bw_include_check_boundary();
    if (src_pos >= src_len) return 0;
    int i = 0;
    while (src_pos < src_len && source[src_pos] != '\n' && i < (int)sizeof(line_buf)-1)
        line_buf[i++] = source[src_pos++];
    line_buf[i] = 0;
    if (src_pos < src_len && source[src_pos] == '\n') src_pos++;
    lp = line_buf;
    line_num++;
    ungot = 0;
    if (bw_try_include(line_buf)) {
        if (had_error) return 0;
        /* Directive consumed; fetch the first real line (now from the
         * spliced include, or the line after a no-op). */
        return next_line();
    }
    return 1;
}

int read_tok(void) {
    static const char *pun = "(),+-*/\\=<>";
    static const char *dub = "<><=>=";  /* pairs: <> <= >= */
    char *p, *d;
    const char **k;

    if (ungot) { ungot = 0; return tok; }
    while (isspace((unsigned char)*lp)) lp++;
    if (!*lp || *lp == '\'') return tok = TOK_EOF;

    /* Number (int or float) */
    if ((*lp == '&' && (lp[1] == 'H' || lp[1] == 'h')) ||
        isdigit((unsigned char)*lp) || (*lp == '.' && isdigit((unsigned char)lp[1]))) {
        tok_num_is_i64 = 0;
        int is_float = 0;
        char *start = lp;
        if (*lp == '&' && (lp[1] == 'H' || lp[1] == 'h')) {
            lp += 2;
            tokq = strtoll(lp, &lp, 16);
            tokv = (int)tokq;
            if (*lp == '&') { tok_num_is_i64 = 1; lp++; }
            if (tokq > 2147483647LL || tokq < -2147483648LL) tok_num_is_i64 = 1;
            return tok = TOK_NUMBER;
        }
        if (lp[0] == '0' && (lp[1] == 'x' || lp[1] == 'X')) {
            tokq = strtoll(lp, &lp, 16);
            tokv = (int)tokq;
            if (*lp == '&') { tok_num_is_i64 = 1; lp++; }
            if (tokq > 2147483647LL || tokq < -2147483648LL) tok_num_is_i64 = 1;
            return tok = TOK_NUMBER;
        }
        while (isdigit((unsigned char)*lp)) lp++;
        if (*lp == '.') {
            is_float = 1; lp++;
            while (isdigit((unsigned char)*lp)) lp++;
        }
        /* Optional exponent: [eE][+-]?digits. Requires at least one digit
         * after the e/E and optional sign — otherwise back out so e.g.
         * `1end` lexes as integer `1` followed by identifier `end`. */
        if (*lp == 'e' || *lp == 'E') {
            char *exp_start = lp;
            lp++;
            if (*lp == '+' || *lp == '-') lp++;
            if (isdigit((unsigned char)*lp)) {
                is_float = 1;
                while (isdigit((unsigned char)*lp)) lp++;
            } else {
                lp = exp_start;
            }
        }
        if (is_float) {
            tokf = strtof(start, NULL);
            return tok = TOK_FLOAT;
        }
        tokq = strtoll(start, &lp, 10);
        tokv = (int)tokq;
        if (*lp == '&') { tok_num_is_i64 = 1; lp++; }
        if (tokq > 2147483647LL || tokq < -2147483648LL) tok_num_is_i64 = 1;
        return tok = TOK_NUMBER;
    }

    /* Hash (file channel prefix) and caret (exponentiation) — before punctuation check */
    if (*lp == '#') { lp++; return tok = TOK_HASH; }
    if (*lp == '^') { lp++; return tok = TOK_POW; }

    /* Punctuation */
    if ((p = strchr(pun, *lp)) != NULL) {
        lp++;
        /* Check double-char operators */
        for (d = (char*)dub; *d; d += 2)
            if (d[0] == lp[-1] && d[1] == *lp) { lp++; return tok = (d - dub)/2 + TOK_NE; }
        return tok = (p - pun) + TOK_LP;
    }

    /* Identifier / keyword */
    if (isalpha((unsigned char)*lp) || *lp == '_') {
        char *tp = tokn;
        while (isalnum((unsigned char)*lp) || *lp == '_') {
            if (tp - tokn < 14) *tp++ = toupper((unsigned char)*lp);
            lp++;
        }
        /* Allow # suffix for float function names */
        if (*lp == '#') { if (tp - tokn < 15) *tp++ = '#'; lp++; }
        /* Allow $ suffix for string variable/function names */
        if (*lp == '$') { if (tp - tokn < 15) *tp++ = '$'; lp++; }
        /* Allow & suffix for 64-bit integer variable/function names */
        if (*lp == '&') { if (tp - tokn < 15) *tp++ = '&'; lp++; }
        *tp = 0;
        for (k = kwd; *k; k++)
            if (strcmp(tokn, *k) == 0) return tok = (k - kwd) + TOK_AND;
        /* DECLARE doesn't fit the kwd index→token mapping (slot 50 would
         * collide with TOK_HASH), so it's special-cased. */
        if (strcmp(tokn, "DECLARE") == 0) return tok = TOK_DECLARE;
        tokv = add_var(tokn);
        return tok = TOK_NAME;
    }

    /* String */
    if (*lp == '"') {
        lp++;
        int off = data_len;
        while (*lp) {
            if (*lp == '"') {
                if (lp[1] == '"') {
                    /* Doubled quote = embedded quote (BASIC standard) */
                    if (data_len < MAX_STRINGS - 1) dbuf_put(data_len++, '"');
                    lp += 2;
                    continue;
                }
                break;  /* End of string */
            }
            if (*lp == '\\' && lp[1]) {
                char esc = lp[1];
                if (esc == 'n') { if (data_len < MAX_STRINGS - 1) dbuf_put(data_len++, '\n'); lp += 2; }
                else if (esc == 't') { if (data_len < MAX_STRINGS - 1) dbuf_put(data_len++, '\t'); lp += 2; }
                else if (esc == '\\') { if (data_len < MAX_STRINGS - 1) dbuf_put(data_len++, '\\'); lp += 2; }
                else if (esc == '"') { if (data_len < MAX_STRINGS - 1) dbuf_put(data_len++, '"'); lp += 2; }
                else { if (data_len < MAX_STRINGS - 1) dbuf_put(data_len++, *lp); lp++; }
                continue;
            }
            if (data_len < MAX_STRINGS - 1) dbuf_put(data_len++, *lp);
            lp++;
        }
        if (data_len < MAX_STRINGS) dbuf_put(data_len++, 0);
        if (*lp == '"') {
            lp++;
        } else {
            error_at("unterminated string literal");
        }
        tokv = off;
        return tok = TOK_STRING;
    }

    error_at("bad token");
    return tok = TOK_EOF;
}

int want(int t) { return !(ungot = (read_tok() != t)); }
void need(int t) { if (!want(t)) error_at("syntax error"); }
