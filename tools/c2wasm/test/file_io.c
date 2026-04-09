/* Test file I/O import compilation with struct-based result types.
 *
 * c2wasm now supports structs, so file_stat_t and dir_entry_t can be
 * declared inline and passed by address to the host imports. We still
 * use literal mode/whence values since c2wasm intercepts conez_api.h
 * for imports and doesn't see its #define constants. */
#include "conez_api.h"

struct file_stat_t {
    int size;
    int type;
    int mtime;
};

struct dir_entry_t {
    int  type;
    char name[256];
};

void setup(void) {
    /* Open for write (mode 1), write, close */
    int h = file_open("/test.txt", 1);
    if (h >= 0) {
        file_write(h, "/test.txt", 9);
        file_close(h);
    }

    /* Stat the file */
    struct file_stat_t st;
    int rc = file_stat("/test.txt", (void *)&st);
    print_i32(rc);
    print_i32(st.size);
    print_i32(st.type);
    print_i32(st.mtime);

    /* Read back */
    h = file_open("/test.txt", 0);  /* mode 0 = read */
    if (h >= 0) {
        int sz = file_size(h);
        int pos = file_tell(h);
        file_seek(h, 0, 0);   /* SET */
        int eof = file_eof(h);
        print_i32(sz);
        print_i32(pos);
        print_i32(eof);
        file_close(h);
    }

    /* Directory listing */
    struct dir_entry_t ent;
    int d = dir_open("/");
    if (d >= 0) {
        while (dir_read(d, (void *)&ent) == 1) {
            print_i32(ent.type);
            /* First character of the entry name, for a minimal smoke check */
            print_i32(ent.name[0]);
        }
        dir_close(d);
    }

    /* Cleanup */
    file_delete("/test.txt");
}
