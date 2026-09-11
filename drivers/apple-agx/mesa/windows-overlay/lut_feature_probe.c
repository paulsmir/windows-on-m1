#include <stdint.h>

typedef uint8_t util_lut2;
typedef uint8_t util_lut3;

#define PROBE_LUT3(expr_involving_a_b_c) \
   ({ \
      const uint8_t a = 0xAA, b = 0xCC, c = 0xF0; \
      (util_lut3)(expr_involving_a_b_c); \
   })
#define PROBE_LUT2(expr_involving_a_b) \
   (util_lut2) PROBE_LUT3((expr_involving_a_b) & ~c)

static uint8_t
reference_lut2(unsigned op)
{
   uint8_t result = 0;
   for (unsigned a = 0; a < 2; ++a) {
      for (unsigned b = 0; b < 2; ++b) {
         unsigned bit = a | (b << 1);
         unsigned out;
         switch (op) {
         case 0: out = !(a | b); break;
         case 1: out = a & !b; break;
         case 2: out = !a & b; break;
         case 3: out = a ^ b; break;
         case 4: out = !(a & b); break;
         case 5: out = a & b; break;
         case 6: out = !a ^ b; break;
         case 7: out = a | !b; break;
         case 8: out = !a | b; break;
         case 9: out = a | b; break;
         case 10: out = a; break;
         case 11: out = !a; break;
         case 12: out = a ^ b; break;
         default: out = 0; break;
         }
         if (out)
            result |= (uint8_t)(1u << bit);
      }
   }
   return result;
}

static uint8_t
probe_invert(util_lut3 l, unsigned s)
{
   uint8_t masks[] = {PROBE_LUT3(a), PROBE_LUT3(b), PROBE_LUT3(c)};
   uint8_t mask = masks[s];
   uint8_t shift = __builtin_ctz(mask);
   uint8_t true_bits = l & mask;
   uint8_t false_bits = l & ~mask;
   return (uint8_t)((false_bits << shift) | (true_bits >> shift));
}

static uint8_t
reference_invert(util_lut3 l, unsigned s)
{
   uint8_t masks[] = {0xAA, 0xCC, 0xF0};
   uint8_t mask = masks[s];
   uint8_t shift = (s == 0) ? 1 : ((s == 1) ? 2 : 4);
   uint8_t true_bits = l & mask;
   uint8_t false_bits = l & (uint8_t)~mask;
   return (uint8_t)((false_bits << shift) | (true_bits >> shift));
}

int
main(void)
{
   const uint8_t values[] = {
      PROBE_LUT2(~(a | b)), PROBE_LUT2(a & ~b), PROBE_LUT2(~a & b),
      PROBE_LUT2(a ^ b), PROBE_LUT2(~(a & b)), PROBE_LUT2(a & b),
      PROBE_LUT2(~a ^ b), PROBE_LUT2(a | ~b), PROBE_LUT2(~a | b),
      PROBE_LUT2(a | b), PROBE_LUT2(a), PROBE_LUT2(~a),
      PROBE_LUT2(a ^ b),
   };
   for (unsigned i = 0; i < sizeof(values); ++i) {
      if (values[i] != reference_lut2(i))
         return (int)(i + 1);
   }
   for (unsigned s = 0; s < 3; ++s) {
      util_lut3 l = PROBE_LUT3((a & b) | (~a & c));
      if (probe_invert(l, s) != reference_invert(l, s))
         return 40 + (int)s;
   }
   return 0;
}
