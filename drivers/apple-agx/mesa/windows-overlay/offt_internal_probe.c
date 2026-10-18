#include <stdint.h>
#include "util/u_dynarray.h"

_Static_assert(sizeof(unsigned) == 4, "AGX binary offsets require 32-bit unsigned");
_Static_assert(sizeof(((struct util_dynarray *)0)->size) == sizeof(unsigned),
               "AGX block offsets must match util_dynarray.size");

static int
probe_branch_patch(unsigned target, unsigned offset)
{
   int32_t patch = (int32_t)target - (int32_t)offset;
   return patch;
}

int
main(void)
{
   return probe_branch_patch(128u, 96u) == 32 ? 0 : 1;
}
