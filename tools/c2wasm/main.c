/*
 * main.c — c2wasm driver: global state, argument parsing, top-level compile
 */
#include "c2wasm.h"

/* Global compiler state */
Symbol syms[MAX_SYMS];
int nsym;
int cur_scope;

FuncCtx func_bufs[MAX_FUNCS];
int nfuncs;
int cur_func;

CtrlEntry ctrl_stk[MAX_CTRL];
int ctrl_sp;
int block_depth;

char data_buf[MAX_STRINGS];
int data_len;

char *source;
int src_len;
int src_pos;
int line_num;
char *src_file;

int tok;
int tok_ival;
float tok_fval;
double tok_dval;
char tok_sval[1024];
int tok_slen;

int had_error;
int nglobals;

int has_setup;
int has_loop;
int type_had_pointer;
int type_had_const;

void compile(void) {
    lex_init();
    preproc_init();
    next_token();
    while (tok != TOK_EOF && !had_error) {
        parse_top_level();
    }
}

static char *read_file(const char *path, int *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "c2wasm: cannot open '%s'\n", path); exit(1); }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) { fprintf(stderr, "c2wasm: out of memory\n"); fclose(fp); exit(1); }
    if ((long)fread(buf, 1, sz, fp) != sz) {
        fprintf(stderr, "c2wasm: read error on '%s'\n", path);
        fclose(fp); free(buf); exit(1);
    }
    buf[sz] = 0;
    fclose(fp);
    *out_len = (int)sz;
    return buf;
}

int main(int argc, char **argv) {
    const char *infile = NULL;
    const char *outfile = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("c2wasm %d.%d.%04d\n", C2WASM_VERSION_MAJOR, C2WASM_VERSION_MINOR, BUILD_NUMBER);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outfile = argv[++i];
        } else if (argv[i][0] != '-') {
            infile = argv[i];
        } else {
            fprintf(stderr, "c2wasm: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!infile) {
        fprintf(stderr, "Usage: c2wasm <input.c> [-o output.wasm]\n");
        return 1;
    }

    /* Default output: replace .c with .wasm */
    char default_out[256];
    if (!outfile) {
        int len = strlen(infile);
        if (len > 2 && strcmp(infile + len - 2, ".c") == 0)
            snprintf(default_out, sizeof(default_out), "%.*s.wasm", len - 2, infile);
        else
            snprintf(default_out, sizeof(default_out), "%s.wasm", infile);
        outfile = default_out;
    }

    src_file = strdup(infile);
    source = read_file(infile, &src_len);
    src_pos = 0;
    line_num = 1;

    /* Global 0 = _heap_ptr */
    nglobals = 1;

    /* Initialize function buffers */
    for (int i = 0; i < MAX_FUNCS; i++)
        buf_init(&func_bufs[i].code);

    compile();

    /* Check for forward-declared but undefined functions */
    for (int i = 0; i < nsym; i++) {
        if (syms[i].kind == SYM_FUNC && !syms[i].is_defined) {
            fprintf(stderr, "%s: error: function '%s' declared but not defined\n",
                    src_file ? src_file : "<input>", syms[i].name);
            had_error = 1;
        }
    }

    if (had_error) {
        fprintf(stderr, "c2wasm: compilation failed\n");
        return 1;
    }

    if (!has_setup && !has_loop) {
        fprintf(stderr, "c2wasm: no setup() or loop() function defined\n");
        return 1;
    }

    assemble(outfile);
    return 0;
}
