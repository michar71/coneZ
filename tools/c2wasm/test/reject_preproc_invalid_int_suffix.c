/* reject: malformed integer suffix in #if */

#if 1uul
int x = 1;
#endif

void setup(void) {
}

void loop(void) {
}
