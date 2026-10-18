#include "uatomic_clang_compat.h"
#include <limits.h>
_Static_assert(sizeof(long) == 4, "Windows long contract");
int main(void) {
    long (*operation)(volatile long *, long) = _interlockedadd;
    volatile long value = 17;
    if (operation(&value, -20) != -3 || value != -3) return 1;
    value = LONG_MAX;
    if (operation(&value, 1) != LONG_MIN || value != LONG_MIN) return 2;
    value = LONG_MIN;
    if (operation(&value, -1) != LONG_MAX || value != LONG_MAX) return 3;
    if (operation(&value, 0) != LONG_MAX || value != LONG_MAX) return 4;
    return 0;
}
