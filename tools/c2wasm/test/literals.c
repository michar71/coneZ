/* Test: integer, float, char, string, hex, octal literals */
#include "conez_api.h"

void setup(void) {
    /* Decimal int */
    print_i32(42);
    print_i32(0);
    print_i32(-1);

    /* Hex int */
    print_i32(0xFF);       /* 255 */
    print_i32(0x1A);       /* 26 */
    print_i32(0xDEAD);     /* 57005 */

    /* Octal int */
    print_i32(0755);       /* 493 */
    print_i32(010);        /* 8 */

    /* Float with f suffix */
    print_f32(3.14f);
    print_f32(0.5f);
    print_f32(1.0f);

    /* Float without f suffix */
    print_f32(2.718);

    /* Trailing dot float */
    print_f32(5.);

    /* Leading dot float */
    print_f32(.25);

    /* Char literal */
    print_i32('A');        /* 65 */
    print_i32('0');        /* 48 */
    print_i32(' ');        /* 32 */

    /* Char escape sequences */
    print_i32('\n');       /* 10 */
    print_i32('\t');       /* 9 */
    print_i32('\0');       /* 0 */
    print_i32('\\');       /* 92 */

    /* String literals */
    print("hello\n");
    print("with\ttab\n");
    print("with\\backslash\n");

    /* String concatenation */
    print("part1" " part2\n");
}

void loop(void) {
    delay_ms(1000);
}
