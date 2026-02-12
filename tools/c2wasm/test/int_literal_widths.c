/* Integer literal width/signedness smoke tests */

unsigned int g_hex_u32 = 0xFFFFFFFF;
long long g_dec_i64 = 2147483648;
unsigned long long g_ull = 18446744073709551615ULL;
unsigned long long g_ul_hex = 0xFFFFFFFFUL;

int setup(void) {
    unsigned int a = 0xFFFFFFFF;
    long long b = 2147483648;
    unsigned long long c = 18446744073709551615ULL;

    if (a > 1u) b += 1;
    if (c > 0ULL) b += 1;

    return (int)b;
}

void loop(void) {
}
