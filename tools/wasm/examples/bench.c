/**
 * bench.c — WASM VM benchmark for ConeZ
 *
 * Three tests:
 *   1. Prime sieve (computation speed)
 *   2. Memory write throughput
 *   3. Memory read throughput
 *
 * Reports milliseconds elapsed for each test. Run on old vs new firmware
 * to compare DRAM vs PSRAM linear memory performance.
 *
 * Build with c2wasm:
 *   cd tools/c2wasm && ./c2wasm ../wasm/examples/bench.c -o ../wasm/examples/bench.wasm
 */

#include "conez_api.h"

#define SIEVE_SIZE 1024
static unsigned char sieve[SIEVE_SIZE];

#define MEM_BUF_SIZE 1024
static unsigned char membuf[MEM_BUF_SIZE];

/* --- Test 1: Prime sieve --- */
static int count_primes(void)
{
    int i;
    int j;
    int count = 0;

    for (i = 0; i < SIEVE_SIZE; i++)
        sieve[i] = 0;

    sieve[0] = 1;
    sieve[1] = 1;

    for (i = 2; i < SIEVE_SIZE; i++) {
        if (sieve[i] == 0) {
            count++;
            for (j = i + i; j < SIEVE_SIZE; j = j + i)
                sieve[j] = 1;
        }
    }

    return count;
}

/* --- Test 2: Memory write throughput --- */
static void mem_write_test(void)
{
    int pass;
    int i;

    for (pass = 0; pass < 1000; pass++) {
        for (i = 0; i < MEM_BUF_SIZE; i++)
            membuf[i] = (unsigned char)(i + pass);
    }
}

/* --- Test 3: Memory read throughput --- */
static int mem_read_test(void)
{
    int pass;
    int i;
    int sum = 0;

    for (pass = 0; pass < 1000; pass++) {
        for (i = 0; i < MEM_BUF_SIZE; i++)
            sum = sum + membuf[i];
    }

    return sum;
}

void setup(void)
{
    int t0;
    int t1;
    int result;

    printf("=== ConeZ WASM Benchmark ===\n\n");

    /* Prime sieve: 100 iterations */
    printf("1. Prime sieve (%d bytes, 100 iters)...\n", SIEVE_SIZE);
    t0 = millis();
    int iter;
    for (iter = 0; iter < 100; iter++)
        result = count_primes();
    t1 = millis();
    printf("   Primes: %d, Time: %d ms\n\n", result, t1 - t0);

    /* Memory write: 1KB x 1000 = 1MB */
    printf("2. Memory write (1KB x 1000 = 1MB)...\n");
    t0 = millis();
    mem_write_test();
    t1 = millis();
    printf("   Time: %d ms\n\n", t1 - t0);

    /* Memory read: 1KB x 1000 = 1MB */
    printf("3. Memory read (1KB x 1000 = 1MB)...\n");
    t0 = millis();
    result = mem_read_test();
    t1 = millis();
    printf("   Checksum: %d, Time: %d ms\n\n", result, t1 - t0);

    printf("=== Done ===\n");
}

void loop(void)
{
    should_stop();
}
