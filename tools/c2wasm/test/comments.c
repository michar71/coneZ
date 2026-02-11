/* Test: comments — line comments, block comments, nested contexts */
#include "conez_api.h"

// This is a line comment
/* This is a block comment */

/* Multi-line
   block
   comment */

void setup(void) {
    int a = 1; // end of line comment
    int b = /* inline block comment */ 2;
    print_i32(a);   /* 1 */
    print_i32(b);   /* 2 */

    // Comment containing special chars: /* not a block start */ and // etc
    /* Block comment with // inside */
    print_i32(3);
}

void loop(void) {
    // Empty loop
    delay_ms(1000);
}
