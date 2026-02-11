/* Test: #if constant expression evaluation */
#include "conez_api.h"

#define VERSION 3
#define LEVEL 10
#define ZERO 0

/* Comparison operators */
#if VERSION > 2
int gt_ok = 1;
#else
int gt_ok = 0;
#endif

#if VERSION < 2
int lt_true = 1;
#else
int lt_true = 0;
#endif

#if VERSION >= 3
int ge_ok = 1;
#else
int ge_ok = 0;
#endif

#if VERSION <= 3
int le_ok = 1;
#else
int le_ok = 0;
#endif

#if VERSION == 3
int eq_ok = 1;
#else
int eq_ok = 0;
#endif

#if VERSION != 5
int ne_ok = 1;
#else
int ne_ok = 0;
#endif

/* Logical operators */
#if VERSION > 1 && LEVEL > 5
int and_ok = 1;
#else
int and_ok = 0;
#endif

#if ZERO || VERSION
int or_ok = 1;
#else
int or_ok = 0;
#endif

/* Arithmetic */
#if VERSION + LEVEL == 13
int add_ok = 1;
#else
int add_ok = 0;
#endif

#if LEVEL - VERSION == 7
int sub_ok = 1;
#else
int sub_ok = 0;
#endif

#if VERSION * 4 == 12
int mul_ok = 1;
#else
int mul_ok = 0;
#endif

/* Parentheses */
#if (VERSION + 1) * 2 == 8
int paren_ok = 1;
#else
int paren_ok = 0;
#endif

/* Negation */
#if !ZERO
int neg_ok = 1;
#else
int neg_ok = 0;
#endif

/* Complex: defined() in expression */
#if defined(VERSION) && VERSION > 2
int def_expr_ok = 1;
#else
int def_expr_ok = 0;
#endif

/* Bitwise */
#if (0xFF & 0x0F) == 15
int bitand_ok = 1;
#else
int bitand_ok = 0;
#endif

#if (1 << 3) == 8
int shift_ok = 1;
#else
int shift_ok = 0;
#endif

void setup(void) {
    print_i32(gt_ok);        /* 1 */
    print_i32(lt_true);      /* 0 */
    print_i32(ge_ok);        /* 1 */
    print_i32(le_ok);        /* 1 */
    print_i32(eq_ok);        /* 1 */
    print_i32(ne_ok);        /* 1 */
    print_i32(and_ok);       /* 1 */
    print_i32(or_ok);        /* 1 */
    print_i32(add_ok);       /* 1 */
    print_i32(sub_ok);       /* 1 */
    print_i32(mul_ok);       /* 1 */
    print_i32(paren_ok);     /* 1 */
    print_i32(neg_ok);       /* 1 */
    print_i32(def_expr_ok);  /* 1 */
    print_i32(bitand_ok);    /* 1 */
    print_i32(shift_ok);     /* 1 */
}

void loop(void) {
    delay_ms(1000);
}
