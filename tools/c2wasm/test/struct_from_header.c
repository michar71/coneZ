/* Struct defined in a #include'd user header. */
#include "conez_api.h"
#include "struct_header.h"

void setup(void) {
    struct box b;
    b.width  = 10;
    b.height = 20;
    b.depth  = 30;
    print_i32(b.width + b.height + b.depth);   /* 60 */
    print_i32(sizeof(struct box));              /* 12 */
}
