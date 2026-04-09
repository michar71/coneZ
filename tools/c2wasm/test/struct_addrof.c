/* Use & on a struct local and pass to a host function.
 * file_stat expects (const char *path, file_stat_t *out). */
#include "conez_api.h"

struct file_stat_t {
    int size;
    int type;
    int mtime;
};

void setup(void) {
    struct file_stat_t st;
    st.size = 0;
    int rc = file_stat("/test.bin", (void *)&st);
    print_i32(rc);
    print_i32(st.size);
    print_i32(st.type);
    print_i32(st.mtime);
}
