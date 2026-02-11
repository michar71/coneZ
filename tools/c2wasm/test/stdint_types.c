// Test stdint types: int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
// int64_t, uint64_t, size_t
#include "conez_api.h"

void setup() {
    // 8-bit types (stored as i32)
    int8_t a = 42;
    uint8_t b = 200;
    print_i32(a);       // 42
    print_i32(b);       // 200

    // 16-bit types (stored as i32)
    int16_t c = 1000;
    uint16_t d = 50000;
    print_i32(c);       // 1000
    print_i32(d);       // 50000

    // 32-bit types (stored as i32)
    int32_t e = -1;
    uint32_t f = 123456;
    print_i32(e);       // -1
    print_i32(f);       // 123456

    // 64-bit types (stored as i64)
    int64_t g = 100;
    uint64_t h = 200;
    print_i64(g);       // 100
    print_i64(h);       // 200

    // size_t (i32 on wasm32)
    size_t sz = 64;
    print_i32(sz);      // 64

    // sizeof returns correct sizes
    print_i32(sizeof(int8_t));   // 1
    print_i32(sizeof(uint8_t));  // 1
    print_i32(sizeof(int16_t));  // 2
    print_i32(sizeof(uint16_t)); // 2
    print_i32(sizeof(int32_t));  // 4
    print_i32(sizeof(uint32_t)); // 4
    print_i32(sizeof(int64_t));  // 8
    print_i32(sizeof(uint64_t)); // 8
    print_i32(sizeof(size_t));   // 4

    // Mixed arithmetic
    uint8_t x = 10;
    int32_t y = 20;
    int32_t z = x + y;
    print_i32(z);       // 30

    // 64-bit arithmetic
    int64_t big = 1000000;
    int64_t bigger = big * big;
    print_i64(bigger);  // 1000000000000
}
