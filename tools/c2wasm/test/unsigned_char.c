/* Test: unsigned char — sizeof, array element size, load/store, coercion */
#include "conez_api.h"

static unsigned char buf[16];
unsigned char global_uc = 200;

void setup(void) {
    /* sizeof(unsigned char) should be 1 */
    print_i32(sizeof(unsigned char));  /* 1 */

    /* Array element access */
    buf[0] = 255;
    buf[1] = 128;
    buf[2] = 0;

    /* Read back — should zero-extend (not sign-extend) */
    int a = buf[0];  /* 255, not -1 */
    int b = buf[1];  /* 128, not -128 */
    int c = buf[2];  /* 0 */
    print_i32(a);
    print_i32(b);
    print_i32(c);

    /* Global unsigned char */
    print_i32(global_uc);  /* 200 */

    /* Coercion to float — should use unsigned conversion */
    float f = (float)buf[0];
    print_f32(f);  /* 255.0 */

    /* Local unsigned char variable */
    unsigned char x = 250;
    print_i32(x);  /* 250 */

    /* Arithmetic with unsigned char */
    unsigned char sum = buf[0] + buf[1];
    print_i32(sum);  /* wraps: (255+128) & 0xFF = 127 */

    /* String init for unsigned char array */
    unsigned char msg[] = "hi";
    print_i32(msg[0]);  /* 'h' = 104 */
    print_i32(msg[1]);  /* 'i' = 105 */
    print_i32(msg[2]);  /* '\0' = 0 */
}

void loop(void) {
    delay_ms(1000);
}
