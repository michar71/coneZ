/* reject: malformed hex escape in #if character literal */

#if '\x'
int x = 1;
#endif

void setup(void) {
}

void loop(void) {
}
