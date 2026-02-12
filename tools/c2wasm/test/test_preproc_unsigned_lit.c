/* Test U/UL suffix classification in preprocessor #if (fix 5).
 * Small U-suffixed values must still be classified as unsigned. */

#if 1U
#define HAS_U 1
#else
#define HAS_U 0
#endif

#if 0U
#define ZERO_U 1
#else
#define ZERO_U 0
#endif

#if 100UL
#define HAS_UL 1
#else
#define HAS_UL 0
#endif

int setup() {
    /* HAS_U=1, ZERO_U=0, HAS_UL=1 */
    return HAS_U + ZERO_U + HAS_UL;  /* 2 */
}

void loop() {
}
