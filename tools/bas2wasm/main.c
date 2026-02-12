/*
 * main.c — compiler driver and global state
 */
#include "bas2wasm.h"

/* Global compiler state definitions */
Var vars[MAX_VARS];
int nvar;
FuncCtx func_bufs[MAX_FUNCS];
int nfuncs;
int cur_func;
CtrlEntry ctrl_stk[MAX_CTRL];
int ctrl_sp;
int block_depth;

char data_buf[MAX_STRINGS];
int data_len;

DataItem data_items[MAX_DATA_ITEMS];
int ndata_items;

char *source;
int src_len;
int src_pos;
char line_buf[512];
char *lp;
int line_num;

int tok, tokv, ungot;
int64_t tokq;
int tok_num_is_i64;
float tokf;
char tokn[16];

VType vstack[64];
int vsp;

int had_error;
int option_base;

FoldSlot fold_a, fold_b;

void compile(void) {
    nfuncs = 1;
    buf_init(&func_bufs[0].code);
    func_bufs[0].nparams = 0;
    func_bufs[0].nlocals = 0;
    func_bufs[0].sub_var = -1;
    cur_func = 0;
    block_depth = 0;
    ctrl_sp = 0;
    vsp = 0;
    nvar = 0;
    data_len = 0;
    ndata_items = 0;
    had_error = 0;
    option_base = 1;
    line_num = 0;
    src_pos = 0;

    /* Initialize file handle table to -1 (closed) */
    for (int i = 0; i < 4; i++) {
        emit_i32_const(FILE_TABLE_BASE + i * 4);
        emit_i32_const(-1);
        emit_i32_store(0);
    }

    while (next_line()) {
        ungot = 0;
        vsp = 0;
        stmt();
        if (had_error) break;
    }

    if (ctrl_sp > 0 && !had_error) {
        error_at("unterminated block (missing END)");
    }
}

int main(int argc, char **argv) {
    const char *inpath = NULL;
    const char *outpath = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc) {
            outpath = argv[++i];
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("bas2wasm %d.%d.%04d\n", BAS2WASM_VERSION_MAJOR, BAS2WASM_VERSION_MINOR, BUILD_NUMBER);
            return 0;
        } else if (argv[i][0] != '-') {
            inpath = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!inpath) {
        fprintf(stderr, "Usage: bas2wasm input.bas [-o output.wasm]\n");
        return 1;
    }

    /* Default output: replace .bas with .wasm */
    char default_out[512];
    if (!outpath) {
        strncpy(default_out, inpath, sizeof(default_out)-6);
        char *dot = strrchr(default_out, '.');
        if (dot) strcpy(dot, ".wasm");
        else strcat(default_out, ".wasm");
        outpath = default_out;
    }

    /* Read input file */
    FILE *fp = fopen(inpath, "r");
    if (!fp) { fprintf(stderr, "Cannot open %s\n", inpath); return 1; }
    fseek(fp, 0, SEEK_END);
    src_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    source = malloc(src_len + 1);
    if ((int)fread(source, 1, src_len, fp) != src_len) {
        fprintf(stderr, "Read error on %s\n", inpath);
        free(source);
        fclose(fp);
        return 1;
    }
    source[src_len] = 0;
    fclose(fp);

    printf("bas2wasm %d.%d.%04d compiling %s...\n",
           BAS2WASM_VERSION_MAJOR, BAS2WASM_VERSION_MINOR, BUILD_NUMBER, inpath);
    compile();

    if (had_error) {
        fprintf(stderr, "Compilation failed.\n");
        free(source);
        return 1;
    }

    assemble(outpath);
    free(source);
    return 0;
}
