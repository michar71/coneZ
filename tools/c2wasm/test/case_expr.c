/* Test: case labels with constant expressions (fix #13) */
#include "conez_api.h"

#define BASE 10
#define FLAG 0x80

void setup(void) {
    int val = 12;
    int result = 0;

    switch (val) {
    case BASE + 2:
        result = 1;
        break;
    case BASE * 3:
        result = 2;
        break;
    case FLAG | 1:
        result = 3;
        break;
    default:
        result = -1;
        break;
    }

    print_i32(result);  /* should be 1 */
}
