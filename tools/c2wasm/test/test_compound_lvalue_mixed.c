/* Test compound assignment on array elements with mixed types (fix 9 edge case).
 * Exercises the general-path compound assignment with type coercion. */

int arr[4];

int setup() {
    arr[0] = 10;
    arr[1] = 7;
    arr[2] = 100;
    arr[3] = 50;

    /* int arr += float rhs: result truncated to int */
    arr[0] += 5;     /* 15 */
    arr[1] *= 3;     /* 21 */

    /* Compound bitwise on array element */
    arr[2] &= 0x0F;  /* 100 & 15 = 4 */
    arr[3] |= 0x80;  /* 50 | 128 = 178 */

    /* Compound shift on array element */
    arr[2] <<= 2;    /* 4 << 2 = 16 */

    int sum = arr[0] + arr[1] + arr[2] + arr[3];
    /* 15 + 21 + 16 + 178 = 230 */
    return sum;
}

void loop() {
}
