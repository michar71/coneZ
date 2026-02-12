/* reject: f/F suffix on plain integer literal is invalid */

void setup(void) {
    float x = 42f;  /* ERROR: f suffix on integer */
    (void)x;
}

void loop(void) {
}
