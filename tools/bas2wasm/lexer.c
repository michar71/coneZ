/*
 * lexer.c — tokenizer for BASIC source
 */
#include "bas2wasm.h"

static const char *kwd[] = {
    "AND","OR","FORMAT","SUB","END","RETURN","LOCAL",
    "WHILE","FOR","TO","IF","ELSE","THEN","DIM","UBOUND",
    "BYE","BREAK","RESUME","PRINTS","STEP","CONST","NOT","XOR",
    "SELECT","CASE","DO","LOOP","UNTIL","EXIT","SWAP","IS",
    "DATA","READ","RESTORE","MOD","NEXT","WEND","FUNCTION",
    "OPEN","CLOSE","AS","KILL","MKDIR","RMDIR","ELSEIF",
    "REDIM","ERASE","PRESERVE","OPTION","BASE",NULL
};

int next_line(void) {
    if (src_pos >= src_len) return 0;
    int i = 0;
    while (src_pos < src_len && source[src_pos] != '\n' && i < (int)sizeof(line_buf)-1)
        line_buf[i++] = source[src_pos++];
    line_buf[i] = 0;
    if (src_pos < src_len && source[src_pos] == '\n') src_pos++;
    lp = line_buf;
    line_num++;
    ungot = 0;
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
            if (tp - tokn < 15) *tp++ = toupper((unsigned char)*lp);
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
                    if (data_len < MAX_STRINGS - 1) data_buf[data_len++] = '"';
                    lp += 2;
                    continue;
                }
                break;  /* End of string */
            }
            if (*lp == '\\' && lp[1]) {
                char esc = lp[1];
                if (esc == 'n') { if (data_len < MAX_STRINGS - 1) data_buf[data_len++] = '\n'; lp += 2; }
                else if (esc == 't') { if (data_len < MAX_STRINGS - 1) data_buf[data_len++] = '\t'; lp += 2; }
                else if (esc == '\\') { if (data_len < MAX_STRINGS - 1) data_buf[data_len++] = '\\'; lp += 2; }
                else if (esc == '"') { if (data_len < MAX_STRINGS - 1) data_buf[data_len++] = '"'; lp += 2; }
                else { if (data_len < MAX_STRINGS - 1) data_buf[data_len++] = *lp; lp++; }
                continue;
            }
            if (data_len < MAX_STRINGS - 1) data_buf[data_len++] = *lp;
            lp++;
        }
        if (data_len < MAX_STRINGS) data_buf[data_len++] = 0;
        if (*lp == '"') lp++;
        tokv = off;
        return tok = TOK_STRING;
    }

    error_at("bad token");
    return tok = TOK_EOF;
}

int want(int t) { return !(ungot = (read_tok() != t)); }
void need(int t) { if (!want(t)) error_at("syntax error"); }
