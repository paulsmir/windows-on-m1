#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum probe_size { PROBE_SIZE_16 = 0, PROBE_SIZE_32 = 1, PROBE_SIZE_64 = 2 };
enum probe_type { PROBE_NULL = 0, PROBE_NORMAL = 1, PROBE_IMMEDIATE = 2,
                  PROBE_UNIFORM = 3, PROBE_REGISTER = 4, PROBE_UNDEF = 5 };

typedef struct {
   bool a : 1;
   unsigned b : 3;
   enum probe_type c : 3;
   unsigned d : 11;
} sentinel_before;

#if PROBE_PRAGMA
#pragma ms_struct off
#endif
typedef struct {
   uint32_t value;
   bool kill : 1;
   bool cache : 1;
   bool discard : 1;
   bool abs : 1;
   bool neg : 1;
   bool memory : 1;
   unsigned channels_m1 : 3;
   enum probe_size size : 2;
   enum probe_type type : 3;
   unsigned reg : 11;
   bool has_reg : 1;
   unsigned padding : 6;
} agx_index;
#if PROBE_PRAGMA
#pragma ms_struct reset
#endif

typedef struct {
   bool a : 1;
   unsigned b : 3;
   enum probe_type c : 3;
   unsigned d : 11;
} sentinel_after;

#if PROBE_PRAGMA
_Static_assert(sizeof(agx_index) == 8, "pragma agx_index must be 8 bytes");
_Static_assert(sizeof(sentinel_before) == sizeof(sentinel_after),
               "ms_struct reset leaked into sentinel");
#endif

static int
check_fields(void)
{
   agx_index a = {0};
   a.value = 0xa5a5a5a5u;
   a.kill = true;
   a.cache = true;
   a.discard = true;
   a.abs = true;
   a.neg = true;
   a.memory = true;
   a.channels_m1 = 7;
   a.size = PROBE_SIZE_64;
   a.type = PROBE_UNDEF;
   a.reg = 0x7ff;
   a.has_reg = true;
   a.padding = 0;

   agx_index b = a;
   if (a.value != 0xa5a5a5a5u || !a.kill || !a.cache || !a.discard ||
       !a.abs || !a.neg || !a.memory || a.channels_m1 != 7 ||
       a.size != PROBE_SIZE_64 || a.type != PROBE_UNDEF || a.reg != 0x7ff ||
       !a.has_reg || memcmp(&a, &b, sizeof(a)) != 0)
      return 1;

   b.type = PROBE_REGISTER;
   if (memcmp(&a, &b, sizeof(a)) == 0)
      return 2;
   return 0;
}

int
main(void)
{
   int fields = check_fields();
   printf("agx_size=%zu agx_align=%zu sentinel_before=%zu sentinel_after=%zu fields=%d pragma=%d\n",
          sizeof(agx_index), _Alignof(agx_index), sizeof(sentinel_before),
          sizeof(sentinel_after), fields == 0, PROBE_PRAGMA);
   return fields;
}
