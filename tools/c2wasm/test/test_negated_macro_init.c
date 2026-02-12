/* Test negated macros in array initializers (fix 7) */

#define VAL 42
#define FVAL 3

int arr[3] = { VAL, -VAL, 0 };

int setup() {
    /* arr[0]=42, arr[1]=-42, arr[2]=0 */
    return arr[0] + arr[1] + arr[2];  /* 42 + (-42) + 0 = 0 */
}

void loop() {
}
