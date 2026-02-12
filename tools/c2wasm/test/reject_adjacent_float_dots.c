/* reject: malformed adjacent-dot float literal */

void setup(void) {
    double x = 1..2;
    (void)x;
}

void loop(void) {
}
