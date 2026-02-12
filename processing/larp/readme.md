This tests the LARP function used through there system to transition between two values.

It abstracts everything between a step function, a ramp and a transition with in/out-easing into two values, offset and window.


"Offset" defines the offset of the start of the transition from the left/right side. In % of half of the transition period. 0% generates a linear transition over the full range, 100% generates a step-function.

Window defines the size of the smoothing window over the transition in percent of the total transition.
0% preserves the original shape (linear) while bigger values apply more and more smoothing for a smooth transition.

(There is a 3rd parameter, "Stride" defined as percent of the whole transition used to set the step-size for the smoothing averaging. Generally "20" seems to work well so we fix it at that for the moment.)


Reference implementation in C:

#include <limits.h>

int larp_safe(int x_pos, int x_min, int x_max,
              int min_val, int max_val,
              int offset, int window, int stride)
{
    long long range, offset_int, window_int, half_window;
    long long start, stop, i;
    long long left, right, denom;
    long long sum = 0;
    long long count = 0;
    long long max_samples = 1000000; /* hard safety cap */
    int val;

    /* Normalize bad axis ordering */
    if (x_min > x_max) {
        int t;
        t = x_min;   x_min = x_max;   x_max = t;
        t = min_val; min_val = max_val; max_val = t;
    }

    /* Degenerate range */
    if (x_min == x_max) {
        return min_val;
    }

    /* Sanitize parameters */
    if (stride <= 0) stride = 1;
    if (offset < 0) offset = 0;
    if (offset > 100) offset = 100;
    if (window < 0) window = -window;
    if (window > 100) window = 100;

    range = (long long)x_max - (long long)x_min;
    offset_int = ((range / 2) * (long long)offset) / 100;
    if (offset_int < 0) offset_int = 0;
    if (offset_int > (range / 2)) offset_int = range / 2;

    window_int = (offset_int * (long long)window) / 100;
    if (window_int < 0) window_int = 0;
    half_window = window_int / 2;

    start = (long long)x_pos - half_window;
    stop  = (long long)x_pos + half_window;

    left  = (long long)x_min + offset_int;
    right = (long long)x_max - offset_int;
    denom = right - left;

    i = start;
    while (i <= stop && count < max_samples) {
        if (i < (long long)x_min) {
            val = min_val;
        } else if (i > (long long)x_max) {
            val = max_val;
        } else {
            if (denom <= 0) {
                /* Offset collapsed interpolation region */
                long long mid = ((long long)x_min + (long long)x_max) / 2;
                val = (i <= mid) ? min_val : max_val;
            } else if (i <= left) {
                val = min_val;
            } else if (i >= right) {
                val = max_val;
            } else {
                /* Rounded linear interpolation, no float/no function calls */
                long long delta = (long long)max_val - (long long)min_val;
                long long pos = i - left;
                long long scaled = delta * pos;
                long long adj = (scaled >= 0) ? (denom / 2) : -(denom / 2);
                long long interp = (long long)min_val + (scaled + adj) / denom;

                if (interp > INT_MAX) interp = INT_MAX;
                if (interp < INT_MIN) interp = INT_MIN;
                val = (int)interp;
            }
        }

        sum += (long long)val;
        count++;

        if (i > LLONG_MAX - (long long)stride) break; /* overflow guard */
        i += (long long)stride;
    }

    if (count <= 0) {
        return min_val;
    }

    /* Rounded average, no function calls */
    {
        long long out = (sum >= 0)
            ? (sum + count / 2) / count
            : (sum - count / 2) / count;

        if (out > INT_MAX) out = INT_MAX;
        if (out < INT_MIN) out = INT_MIN;
        return (int)out;
    }
}