/* reject: missing ')' after defined(identifier) */

#define FOO 1
#if defined(FOO
int x = 1;
#endif

void setup(void) {
}

void loop(void) {
}
