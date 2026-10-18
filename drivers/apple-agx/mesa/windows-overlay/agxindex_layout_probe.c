#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum probe_size { PROBE_SIZE_16 = 0, PROBE_SIZE_32 = 1, PROBE_SIZE_64 = 2 };
enum probe_type { PROBE_NULL = 0, PROBE_NORMAL = 1, PROBE_IMMEDIATE = 2,
                  PROBE_UNIFORM = 3, PROBE_REGISTER = 4, PROBE_UNDEF = 5 };

#if PROBE_GCC_STRUCT
#define PROBE_RECORD __attribute__((gcc_struct))
#else
#define PROBE_RECORD
#endif

typedef struct PROBE_RECORD {
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
} probe_index;

#if PROBE_GCC_STRUCT
_Static_assert(sizeof(probe_index) == 8, "gcc_struct layout must be 8 bytes");
#endif
#if PROBE_EXPECT_SIZE
_Static_assert(sizeof(probe_index) == PROBE_EXPECT_SIZE,
               "probe target size mismatch");
#endif

static int
check_fields(void)
{
   probe_index value = {0};
   value.value = 0xa5a5a5a5u;
   value.kill = true;
   value.cache = true;
   value.discard = true;
   value.abs = true;
   value.neg = true;
   value.memory = true;
   value.channels_m1 = 5;
   value.size = PROBE_SIZE_64;
   value.type = PROBE_REGISTER;
   value.reg = 0x321;
   value.has_reg = true;
   if (value.value != 0xa5a5a5a5u || !value.kill || !value.cache ||
       !value.discard || !value.abs || !value.neg || !value.memory ||
       value.channels_m1 != 5 || value.size != PROBE_SIZE_64 ||
       value.type != PROBE_REGISTER || value.reg != 0x321 || !value.has_reg)
      return 1;

   probe_index same = value;
#if PROBE_GCC_STRUCT
   if (sizeof(value) != 8 || sizeof(same) != 8)
      return 2;
#endif
   if (__builtin_memcmp(&value, &same, sizeof(value)) != 0)
      return 3;
   same.reg ^= 1u;
   if (__builtin_memcmp(&value, &same, sizeof(value)) == 0)
      return 4;
   return 0;
}

int
main(void)
{
   printf("sizeof=%zu alignof=%zu fields=%d gcc_struct=%d\n",
          sizeof(probe_index), _Alignof(probe_index), check_fields() == 0,
          PROBE_GCC_STRUCT);
   return check_fields();
}
