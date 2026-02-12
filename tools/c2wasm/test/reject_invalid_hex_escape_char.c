/* reject: malformed hex escape in character literal */

void setup(void) {
    int c = '\x';
    (void)c;
}

void loop(void) {
}
