/* Test pointer increment scaling by element size (fix 3) */

int arr[4];

int setup() {
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;

    int *p = &arr[0];

    /* Pre-increment: should advance by sizeof(int)=4 */
    ++p;
    int v1 = *p;   /* should be arr[1] = 20 */

    /* Postfix increment: should advance by sizeof(int)=4 */
    p++;
    int v2 = *p;   /* should be arr[2] = 30 */

    /* Pre-decrement */
    --p;
    int v3 = *p;   /* should be arr[1] = 20 */

    /* Postfix decrement */
    p--;
    int v4 = *p;   /* should be arr[0] = 10 */

    return v1 + v2 + v3 + v4;  /* 20+30+20+10 = 80 */
}

void loop() {
}
