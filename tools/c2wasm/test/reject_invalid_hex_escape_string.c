/* reject: malformed hex escape in string literal */

void setup(void) {
    const char *s = "\x";
    (void)s;
}

void loop(void) {
}
