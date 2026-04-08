/* Test file I/O import compilation.
 *
 * Note: c2wasm does not parse conez_api.h structs — it treats all pointer
 * args as int. So file_stat_t / dir_entry_t are passed as raw int arrays.
 * Mode/whence constants are plain literals here, not FILE_MODE_* macros. */
#include "conez_api.h"

int st[3];       /* file_stat_t: size, type, mtime */
int ent[65];     /* dir_entry_t: type + char[256] = 260 bytes = 65 ints */

void setup(void) {
    /* Open for write (mode 1), write, close */
    int h = file_open("/test.txt", 1);
    if (h >= 0) {
        file_write(h, "/test.txt", 9);
        file_close(h);
    }

    /* Stat the file */
    int rc = file_stat("/test.txt", st);
    print_i32(rc);
    print_i32(st[0]);   /* size */
    print_i32(st[1]);   /* type */

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
    int d = dir_open("/");
    if (d >= 0) {
        while (dir_read(d, ent) == 1) {
            print_i32(ent[0]);  /* type */
        }
        dir_close(d);
    }

    /* Cleanup */
    file_delete("/test.txt");
}
