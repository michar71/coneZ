/* Test: long long (i64) type support */
#include "conez_api.h"

long long global_time = 0;

void setup(void) {
    /* i64 variable and API calls */
    long long now = get_epoch_ms();
    long long uptime = get_uptime_ms();

    /* i64 arithmetic */
    long long diff = now - uptime;
    long long sum = now + 1000;
    long long product = diff * 2;

    /* i64 comparison */
    if (now > uptime) {
        print_i64(diff);
    }

    /* i64 increment/decrement */
    long long counter = 0;
    counter++;
    ++counter;
    counter--;

    /* i64 compound assignment */
    counter += 100;
    counter -= 50;
    counter *= 2;

    /* i64 to/from int conversion */
    int ms_low = (int)now;
    long long from_int = (long long)ms_low;

    /* i64 to/from double */
    double d = (double)now;
    long long from_double = (long long)d;

    /* Store to global */
    global_time = now;

    (void)sum; (void)product; (void)from_int; (void)from_double;
}
