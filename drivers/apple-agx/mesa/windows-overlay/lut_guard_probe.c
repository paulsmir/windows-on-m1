#include "util/lut.h"
#ifndef UTIL_LUT2
#error UTIL_LUT2_NOT_DEFINED
#endif
int main(void) { return UTIL_LUT2(a ^ b) == 6 ? 0 : 1; }
