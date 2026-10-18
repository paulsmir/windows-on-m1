#include <stdint.h>
#include <stdio.h>

#include "util/lut.h"

/* This is deliberately a scalar minterm oracle, not a copy of lut.h masks. */
static uint8_t
truth_lut2(unsigned expression)
{
   uint8_t result = 0;
   for (unsigned a = 0; a < 2; ++a) {
      for (unsigned b = 0; b < 2; ++b) {
         unsigned value;
         switch (expression) {
         case 0: value = !(a | b); break;
         case 1: value = a & !b; break;
         case 2: value = (!a) & b; break;
         case 3: value = a ^ b; break;
         case 4: value = !(a & b); break;
         case 5: value = a & b; break;
         case 6: value = !a ^ b; break;
         case 7: value = a | !b; break;
         case 8: value = !a | b; break;
         case 9: value = a | b; break;
         case 10: value = a; break;
         case 11: value = !a; break;
         case 12: value = a ^ b; break;
         default: return 0;
         }
         result |= (uint8_t)(value << (a | (b << 1)));
      }
   }
   return result;
}

static uint8_t
truth_permute(uint8_t lut, unsigned sources, unsigned inverted_source)
{
   uint8_t result = 0;
   for (unsigned destination = 0; destination < (1u << sources); ++destination) {
      unsigned source = destination ^ (1u << inverted_source);
      result |= ((lut >> source) & 1u) << destination;
   }
   return result;
}

int
main(void)
{
   const util_lut2 expressions[] = {
      UTIL_LUT2(~(a | b)), UTIL_LUT2(a & ~b), UTIL_LUT2(~a & b),
      UTIL_LUT2(a ^ b), UTIL_LUT2(~(a & b)), UTIL_LUT2(a & b),
      UTIL_LUT2(~a ^ b), UTIL_LUT2(a | ~b), UTIL_LUT2(~a | b),
      UTIL_LUT2(a | b), UTIL_LUT2(a), UTIL_LUT2(~a), UTIL_LUT2(a ^ b),
   };

   for (unsigned i = 0; i < sizeof(expressions) / sizeof(expressions[0]); ++i)
      if (expressions[i] != truth_lut2(i))
         return 1 + (int)i;

   for (unsigned lut = 0; lut < 16; ++lut)
      for (unsigned source = 0; source < 2; ++source)
         if (util_lut2_invert_source((util_lut2)lut, source) !=
             truth_permute((uint8_t)lut, 2, source))
            return 20 + (int)(lut * 2 + source);

   for (unsigned lut = 0; lut < 256; ++lut)
      for (unsigned source = 0; source < 3; ++source)
         if (util_lut3_invert_source((util_lut3)lut, source) !=
             truth_permute((uint8_t)lut, 3, source))
            return 60 + (int)(lut * 3 + source);

   for (unsigned i = 0; i < sizeof(expressions) / sizeof(expressions[0]); ++i)
      printf("%02x%c", expressions[i], i + 1 == sizeof(expressions) / sizeof(expressions[0]) ? '\n' : ' ');
   return 0;
}
