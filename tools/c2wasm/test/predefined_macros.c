/* Test: common predefined preprocessor macros
 *
 * No EXPECTED: block — this test prints __DATE__/__TIME__ (and is
 * sensitive to its own __LINE__), so its output is inherently
 * non-deterministic. Structural (make test) coverage only.
 */
#include <conez_api.h>

int g_line = __LINE__;
int g_stdc = __STDC__;
int g_stdc_ver = __STDC_VERSION__;
int g_hosted = __STDC_HOSTED__;
int g_ctr0 = __COUNTER__;
int g_ctr1 = __COUNTER__;

#if __STDC__
int g_if_stdc = 1;
#else
int g_if_stdc = 0;
#endif

#if __LINE__ > 0
int g_if_line = 1;
#else
int g_if_line = 0;
#endif

void setup(void) {
    const char *g_file = __FILE__;
    const char *g_date = __DATE__;
    const char *g_time = __TIME__;
    int local_line = __LINE__;
    print_i32(g_stdc);
    print_i32(g_stdc_ver);
    print_i32(g_hosted);
    print_i32(g_ctr1 - g_ctr0);
    print_i32(g_if_stdc + g_if_line + local_line + g_line);
    print_str(g_file, 1);
    print_str(g_date, 1);
    print_str(g_time, 1);
}

void loop(void) {
}
