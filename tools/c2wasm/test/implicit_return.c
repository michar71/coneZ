/* Test: implicit return for non-void functions without explicit return */
#include "conez_api.h"

/* Function with return only on some paths — needs implicit return */
static int maybe_return(int x) {
    if (x > 0) return x;
    /* Falls through — implicit return 0 needed */
}

/* Function whose last instruction's SLEB encoding ends in 0x0F (value 15) */
static int tricky_fifteen(int x) {
    if (x > 10) return x;
    x = 15;  /* i32.const 15 → SLEB 0x0F, same byte as OP_RETURN */
    /* Implicit return must still be emitted */
}

void setup(void) {
    int a = maybe_return(5);
    print_i32(a);
    int b = maybe_return(-1);
    print_i32(b);
    int c = tricky_fifteen(3);
    print_i32(c);
}
