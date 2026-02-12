/* Test: address-of global scalar variables */

int g;
int gp;

void setup(void) {
    int *p = &g;
    *p = 42;

    int **pp = &p;
    **pp = **pp + 1;

    int *pgp = &gp;
    *pgp = g;
}

void loop(void) {
}
