/* Test switch with non-macro case labels (fix 4: had_error save/restore).
 * When a case label can't be resolved at pre-scan time, compilation
 * should still succeed — had_error must not leak from the pre-scan. */

int setup() {
    int val = 2;
    int result = 0;

    switch (val) {
    case 1:
        result = 10;
        break;
    case 2:
        result = 20;
        break;
    case 3:
        result = 30;
        break;
    default:
        result = -1;
        break;
    }

    return result;  /* 20 */
}

void loop() {
}
