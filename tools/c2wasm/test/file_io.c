/* Test file I/O import compilation */
#include "conez_api.h"

void setup(void) {
    /* Open, write, close */
    int h = file_open("/test.txt", 9, 1);
    if (h >= 0) {
        file_write(h, "/test.txt", 9);
        file_close(h);
    }

    /* Check existence */
    int exists = file_exists("/test.txt", 9);
    print_i32(exists);

    /* Read back */
    h = file_open("/test.txt", 9, 0);
    if (h >= 0) {
        int sz = file_size(h);
        int pos = file_tell(h);
        file_seek(h, 0);
        print_i32(sz);
        print_i32(pos);
        file_close(h);
    }

    /* Cleanup */
    file_delete("/test.txt", 9);
}
