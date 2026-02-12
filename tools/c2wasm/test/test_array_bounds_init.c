/* Test array initializer within bounds (fix 8 — bounds check).
 * Exercises normal array initialization to ensure the bounds check
 * doesn't reject valid initializers. */

int small[2] = { 42, 99 };
int medium[4] = { 1, 2, 3, 4 };
float farr[3] = { 1.0f, 2.5f, 3.75f };

int setup() {
    int sum = small[0] + small[1];  /* 42 + 99 = 141 */
    sum += medium[0] + medium[1] + medium[2] + medium[3];  /* + 10 = 151 */
    return sum;
}

void loop() {
}
