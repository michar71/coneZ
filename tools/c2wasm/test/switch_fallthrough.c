/* Test: switch with C-standard fall-through behavior */
#include "conez_api.h"

void setup(void) {
    int x = 2;

    /* Fall-through: case 2 falls through to case 3 */
    switch (x) {
    case 1:
        print_i32(10);
        break;
    case 2:
        print_i32(20);
        /* no break — falls through */
    case 3:
        print_i32(30);
        break;
    case 4:
        print_i32(40);
        break;
    }

    /* Default case */
    int y = 99;
    switch (y) {
    case 1:
        print_i32(100);
        break;
    default:
        print_i32(200);
        break;
    }

    /* Fall-through into default */
    int z = 3;
    switch (z) {
    case 3:
        print_i32(300);
        /* no break — falls through to default */
    default:
        print_i32(400);
        break;
    }
}
