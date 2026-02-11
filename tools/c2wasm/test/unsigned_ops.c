// Test unsigned integer operations
#include "conez_api.h"

void setup() {
    // --- uint32_t comparison ---
    // 3000000000 > 0x7FFFFFFF, so signed comparison would treat it as negative
    uint32_t a = 3000000000u;
    print_i32(a > 0);          // 1 (unsigned: 3B > 0)
    print_i32(a > 2999999999u); // 1

    // --- uint32_t division ---
    uint32_t b = 4000000000u;
    print_i32(b / 2);          // 2000000000

    // --- uint32_t remainder ---
    uint32_t c = 4000000001u;
    print_i32(c % 2);          // 1

    // --- unsigned right shift (no sign extension) ---
    uint32_t d = 0x80000000;
    print_i32(d >> 1);         // 0x40000000 = 1073741824

    // --- mixed signed/unsigned promotes to unsigned ---
    int sa = -1;
    unsigned int ub = 1;
    print_i32(sa < ub);        // 0 (-1 becomes UINT_MAX, which is > 1)

    // --- unsigned 64-bit ---
    uint64_t big = 10000000000u;
    print_i64(big / 3);        // 3333333333

    // --- unsigned 64-bit comparison ---
    uint64_t x = 10000000000u;
    uint64_t y = 5000000000u;
    print_i32(x > y);          // 1

    // --- sizeof unchanged for unsigned types ---
    print_i32(sizeof(uint8_t));   // 1
    print_i32(sizeof(uint16_t));  // 2
    print_i32(sizeof(uint32_t));  // 4
    print_i32(sizeof(uint64_t));  // 8

    // --- unsigned keyword with base types ---
    unsigned int ui = 42;
    print_i32(ui);              // 42
    unsigned long long ull = 100;
    print_i64(ull);             // 100

    // --- unsigned increment/decrement ---
    uint32_t e = 10;
    e++;
    print_i32(e);               // 11
    e--;
    print_i32(e);               // 10

    // --- unsigned compound assignment ---
    uint32_t f = 100;
    f /= 3;
    print_i32(f);               // 33 (unsigned division)
    f %= 10;
    print_i32(f);               // 3

    // --- unsigned right shift assignment ---
    uint32_t g = 0x80000000;
    g >>= 4;
    print_i32(g);               // 0x08000000 = 134217728

    // --- unsigned to float conversion ---
    uint32_t h = 3000000000u;
    float fh = (float)h;
    // float can't represent exactly, but should be positive ~3e9
    print_i32(fh > 0.0f);      // 1
}
