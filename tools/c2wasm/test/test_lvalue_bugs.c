/* Test critical lvalue assignment bugs:
 * - arr[i] = x stores value, not address (fix 1)
 * - no stack leak on arr[i] = x (fix 2)
 * - *p = x works correctly (fixes 1+2)
 * - arr[i] = x inside if/else (stack balance)
 */

int arr[4];

int setup() {
    /* Basic array assignment (fix 1: value stored, not address) */
    arr[0] = 100;
    arr[1] = 200;
    arr[2] = 300;
    arr[3] = 400;

    /* Verify values */
    int sum = arr[0] + arr[1] + arr[2] + arr[3];
    /* sum should be 1000 */

    /* Array assignment inside if/else (fix 2: no stack leak) */
    int x = 1;
    if (x) {
        arr[0] = 10;
    } else {
        arr[0] = 20;
    }

    /* Pointer dereference assignment */
    int *p = &arr[1];
    *p = 42;

    /* Chain: read back through pointer */
    int v = *p;

    /* Verify: arr[0]=10, arr[1]=42, arr[2]=300, arr[3]=400 */
    int total = arr[0] + arr[1] + arr[2] + arr[3];
    /* total = 10 + 42 + 300 + 400 = 752 */

    return total + v;  /* 752 + 42 = 794 */
}

void loop() {
}
