/* Test: printf builtin with various format specifiers */
#include "conez_api.h"

void setup(void) {
    /* Integer formats */
    printf("int: %d\n", 42);
    printf("negative: %d\n", -7);
    printf("hex: %x\n", 255);
    printf("HEX: %X\n", 255);

    /* Float format (requires double promotion) */
    printf("float: %.2f\n", (double)3.14f);
    printf("float: %f\n", (double)1.0f);

    /* Multiple args */
    printf("%d + %d = %d\n", 3, 4, 7);

    /* Mixed types */
    float val = 2.5f;
    printf("i=%d f=%.1f\n", 10, (double)val);

    /* Char format */
    printf("char: %c\n", 65);   /* 'A' */

    /* String format */
    printf("str: %s\n", "hello");

    /* Percent literal */
    printf("100%%\n");

    /* Width/padding */
    printf("[%5d]\n", 42);
    printf("[%-5d]\n", 42);
}

void loop(void) {
    delay_ms(1000);
}
