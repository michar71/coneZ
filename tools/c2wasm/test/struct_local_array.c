/* Local array of struct. */
#include "conez_api.h"

struct pair { int a; int b; };

void setup(void) {
    struct pair arr[3];
    arr[0].a = 1; arr[0].b = 2;
    arr[1].a = 3; arr[1].b = 4;
    arr[2].a = 5; arr[2].b = 6;

    int sum = 0;
    for (int i = 0; i < 3; i++) sum += arr[i].a + arr[i].b;
    print_i32(sum);  /* 1+2+3+4+5+6 = 21 */
}
