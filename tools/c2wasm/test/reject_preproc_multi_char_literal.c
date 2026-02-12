/* reject: multi-character literal in #if */

#if 'ab'
int x = 1;
#endif

void setup(void) {
}

void loop(void) {
}
