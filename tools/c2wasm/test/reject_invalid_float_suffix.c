/* reject: malformed float suffix */

void setup(void) {
    double x = 1.0fz;
    (void)x;
}

void loop(void) {
}
