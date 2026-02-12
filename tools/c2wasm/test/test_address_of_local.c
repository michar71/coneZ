int g[4];
int setup() {
  int a[3] = {1,2,3};
  int *p = &a[1];
  int *q = &g[2];
  return *p + *q;
}
void loop() {}
