#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum agx_size_probe { AGX_SIZE_16 = 0, AGX_SIZE_32 = 1, AGX_SIZE_64 = 2 };
enum agx_type_probe { AGX_INDEX_NULL = 0, AGX_INDEX_NORMAL = 1,
                      AGX_INDEX_IMMEDIATE = 2, AGX_INDEX_UNIFORM = 3,
                      AGX_INDEX_REGISTER = 4, AGX_INDEX_UNDEF = 5 };

#if PROBE_CANDIDATE
typedef struct {
   uint32_t value;
   unsigned kill : 1;
   unsigned cache : 1;
   unsigned discard : 1;
   unsigned abs : 1;
   unsigned neg : 1;
   unsigned memory : 1;
   unsigned channels_m1 : 3;
   unsigned size : 2;
   unsigned type : 3;
   unsigned reg : 11;
   unsigned has_reg : 1;
   unsigned padding : 6;
} agx_index_probe;
#else
typedef struct {
   uint32_t value;
   bool kill : 1;
   bool cache : 1;
   bool discard : 1;
   bool abs : 1;
   bool neg : 1;
   bool memory : 1;
   unsigned channels_m1 : 3;
   enum agx_size_probe size : 2;
   enum agx_type_probe type : 3;
   unsigned reg : 11;
   bool has_reg : 1;
   unsigned padding : 6;
} agx_index_probe;
#endif

_Static_assert(sizeof(agx_index_probe) == 8, "agx_index corpus record must be 8 bytes");
_Static_assert(_Alignof(agx_index_probe) == 4, "agx_index corpus alignment must be 4");

static void
fill(agx_index_probe *idx, unsigned n)
{
   static const unsigned regs[] = {0, 1, 2, 31, 127, 511, 1022, 2047};
   memset(idx, 0, sizeof(*idx));
   idx->value = 0x9e3779b9u * (n + 1u) ^ (n << 16);
   idx->kill = (n >> 0) & 1u;
   idx->cache = (n >> 1) & 1u;
   idx->discard = (n >> 2) & 1u;
   idx->abs = (n >> 3) & 1u;
   idx->neg = (n >> 4) & 1u;
   idx->memory = (n >> 5) & 1u;
   idx->channels_m1 = n & 7u;
   idx->size = n % 3u;
   idx->type = n % 6u;
   idx->reg = regs[n % (sizeof(regs) / sizeof(regs[0]))];
   idx->has_reg = (n >> 6) & 1u;
   idx->padding = 0;
}

static void
print_case(unsigned n, const agx_index_probe *idx)
{
   const unsigned char *raw = (const unsigned char *)idx;
   printf("%03u %08x %u%u%u%u%u%u %u %u %u %u %u ", n, idx->value,
          (unsigned)idx->kill, (unsigned)idx->cache, (unsigned)idx->discard,
          (unsigned)idx->abs, (unsigned)idx->neg, (unsigned)idx->memory,
          (unsigned)idx->channels_m1, (unsigned)idx->size, (unsigned)idx->type,
          (unsigned)idx->reg, (unsigned)idx->has_reg);
   for (unsigned i = 0; i < sizeof(*idx); ++i)
      printf("%02x", raw[i]);
   putchar('\n');
}

int
main(void)
{
   for (unsigned n = 0; n < 256; ++n) {
      agx_index_probe a, b;
      fill(&a, n);
      fill(&b, n);
      if (memcmp(&a, &b, sizeof(a)) != 0)
         return 10;
      if (n == 255)
         b.type = AGX_INDEX_REGISTER;
      if (n == 255 && memcmp(&a, &b, sizeof(a)) == 0)
         return 11;
      print_case(n, &a);
   }
   return 0;
}
