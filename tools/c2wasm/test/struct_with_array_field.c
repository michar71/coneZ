/* Struct containing a char[] field — the dir_entry_t pattern.
 * Accessing the inner array decays to a pointer (i32), from which
 * we can read individual bytes via subscript. */
#include "conez_api.h"

struct dir_entry_t {
    int  type;
    char name[256];
};

void setup(void) {
    /* sizeof should account for the array field */
    print_i32(sizeof(struct dir_entry_t));   /* 260, rounded up to 260 */

    struct dir_entry_t ent;
    ent.type = 7;
    /* Write directly into the char array via subscript */
    ent.name[0] = 'h';
    ent.name[1] = 'i';
    ent.name[2] = 0;

    print_i32(ent.type);
    print_i32(ent.name[0]);
    print_i32(ent.name[1]);
}
