/* Test: constant folding produces correct values for each operator/type
 * combination. wasm-validate alone can't tell that folding actually ran —
 * the runtime runner verifies the printed values, and `make verify`
 * baselines pin the resulting bytecode. */

// EXPECTED:
// 14
// 12
// 1
// 15
// 240
// 4
// 1000000
// 30
#include <conez_api.h>

void setup(void) {
    print_i32(2 + 3 * 4);              /* 14 */
    print_i32((10 - 4) * 2);           /* 12 */
    print_i32(5 < 10);                 /* 1 (comparison fold) */
    print_i32(255 & 0x0F);             /* 15 (bitwise) */
    print_i32(0xF0 ^ 0x00);            /* 240 (xor) */
    print_i32(1 << 2);                 /* 4 (shift) */
    print_i64(1000LL * 1000LL);        /* 1000000 (i64 fold) */
    print_i32(60 / 2);                 /* 30 (division) */
}

void loop(void) {}
