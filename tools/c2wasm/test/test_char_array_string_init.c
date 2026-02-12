char g1[] = "abc";
char g2[3] = "abc";

int setup() {
  char l1[] = "xy";
  char l2[2] = "pq";
  return (int)g1[0] + (int)g1[3] + (int)g2[2] + (int)l1[2] + (int)l2[1];
}
void loop() {}
