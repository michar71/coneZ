// Arduino helper replacements for pure ESP-IDF builds.
// constrain(), map(), random(), PI, min(), max()

#ifndef CONEZ_COMPAT_H
#define CONEZ_COMPAT_H

#include <stdlib.h>
#include <math.h>
#include "esp_random.h"

#ifndef PI
#define PI M_PI
#endif

template<typename T>
static inline T constrain(T x, T lo, T hi) {
    return (x < lo) ? lo : (x > hi) ? hi : x;
}

static inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Arduino random(min, max) — returns [min, max)
static inline long random(long mn, long mx) {
    if (mn >= mx) return mn;
    return mn + (esp_random() % (mx - mn));
}

static inline long random(long mx) {
    if (mx <= 0) return 0;
    return esp_random() % mx;
}

#ifndef min
template<typename T>
static inline T min(T a, T b) { return (a < b) ? a : b; }
#endif

#ifndef max
template<typename T>
static inline T max(T a, T b) { return (a > b) ? a : b; }
#endif

#endif
