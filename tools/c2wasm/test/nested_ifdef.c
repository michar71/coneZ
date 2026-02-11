/* Test: nested #ifdef with #else in skipped outer block */
#include "conez_api.h"

#define DEFINED_MACRO 1

/* Outer block is skipped — inner #else must NOT enable code */
#ifdef UNDEFINED_MACRO
  #ifdef SOMETHING
    /* This should be skipped */
  #else
    /* This should ALSO be skipped — outer block is false */
    THIS_WOULD_CAUSE_AN_ERROR_IF_COMPILED
  #endif
#endif

/* Normal case: outer is true, inner #else works */
#ifdef DEFINED_MACRO
  #ifndef ALSO_UNDEFINED
    #define INNER_VALUE 42
  #else
    #define INNER_VALUE 0
  #endif
#endif

void setup(void) {
    int x = INNER_VALUE;
    print_i32(x);
}
