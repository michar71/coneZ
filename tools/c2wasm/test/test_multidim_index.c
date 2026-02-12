int g[2][3];

int setup() {
  g[1][2] = 7;
  g[0][1] = 4;
  int m[2][3];
  m[1][0] = 9;
  return g[1][2] + g[0][1] + m[1][0];
}
void loop() {}
