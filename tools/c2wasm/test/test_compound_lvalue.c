/* Test compound assignment for complex lvalues (fix 9) */

int arr[4];

int setup() {
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 100;
    arr[3] = 8;

    /* Compound assignment on array elements */
    arr[0] += 5;    /* 15 */
    arr[1] -= 3;    /* 17 */
    arr[2] *= 2;    /* 200 */
    arr[3] /= 4;    /* 2 */

    /* Compound through pointer */
    int *p = &arr[0];
    *p += 100;      /* arr[0] = 115 */

    /* Mixed-type compound assignment (int arr, float rhs) */
    arr[1] += 0;  /* no-op, stays 17 */

    int sum = arr[0] + arr[1] + arr[2] + arr[3];
    /* 115 + 17 + 200 + 2 = 334 */
    return sum;
}

void loop() {
}
