#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#ifndef static_assert
#define static_assert _Static_assert
#endif

#include "agx_index_fragment.h"

static void
print_index(const char *name, agx_index index)
{
   const unsigned char *bytes = (const unsigned char *)&index;
   printf("%s ", name);
   for (unsigned i = 0; i < sizeof(index); ++i)
      printf("%02x", bytes[i]);
   putchar('\n');
}

int
main(void)
{
   agx_index normal = agx_get_vec_index(0x12345u, AGX_SIZE_32, 4);
   agx_index immediate = agx_immediate(0x80);
   agx_index memory = agx_memory_register(23, AGX_SIZE_64);
   agx_index like = agx_register_like(31, memory);
   agx_index undef = agx_undef(AGX_SIZE_16);
   agx_index uniform = agx_uniform(511, AGX_SIZE_32);
   agx_index null_index = agx_null();
   agx_index zero = agx_zero();
   agx_index negzero = agx_negzero();
   agx_index modifiers = agx_neg(agx_abs(normal));
   agx_index replaced = agx_replace_index(modifiers, uniform);

   if (sizeof(agx_index) != 8 || _Alignof(agx_index) != 4 ||
       normal.type != AGX_INDEX_NORMAL || immediate.type != AGX_INDEX_IMMEDIATE ||
       memory.memory != 1 || like.type != AGX_INDEX_REGISTER ||
       undef.type != AGX_INDEX_UNDEF || uniform.type != AGX_INDEX_UNIFORM ||
       !agx_is_null(null_index) || zero.value != 0 || negzero.value != 0x80 ||
       !replaced.abs || !replaced.neg || replaced.type != AGX_INDEX_UNIFORM)
      return 1;

   print_index("normal", normal);
   print_index("immediate", immediate);
   print_index("memory", memory);
   print_index("like", like);
   print_index("undef", undef);
   print_index("uniform", uniform);
   print_index("null", null_index);
   print_index("zero", zero);
   print_index("negzero", negzero);
   print_index("modifiers", modifiers);
   print_index("replaced", replaced);
   return 0;
}
