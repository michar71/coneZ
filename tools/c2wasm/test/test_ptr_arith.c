int setup() {
  int g;
  int *p;
  p = &g;
  p = p + 2;
  p = 1 + p;
  int d = p - p;
  return d;
}
void loop() {}
