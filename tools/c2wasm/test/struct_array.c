/* Array of structs: layout is contiguous, accessed via subscript + member. */
#include "conez_api.h"

struct vec2 {
    int x;
    int y;
};

struct vec2 points[4];

void setup(void) {
    for (int i = 0; i < 4; i++) {
        points[i].x = i;
        points[i].y = i * 10;
    }
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += points[i].x + points[i].y;
    }
    print_i32(sum);  /* (0+0) + (1+10) + (2+20) + (3+30) = 66 */
}
