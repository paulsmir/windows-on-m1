#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <math.h>

static uint64_t
bits(double value)
{
   uint64_t value_bits;
   memcpy(&value_bits, &value, sizeof(value_bits));
   return value_bits;
}

int
main(void)
{
   printf("M_LOG2E=%016" PRIx64 "\n", bits(M_LOG2E));
   printf("M_PI=%016" PRIx64 "\n", bits(M_PI));
   printf("M_1_PI=%016" PRIx64 "\n", bits(M_1_PI));
   printf("_MSC_VER=%d __clang__=%d _WIN32=%d aarch64=%d\n",
#ifdef _MSC_VER
          _MSC_VER,
#else
          0,
#endif
#ifdef __clang__
          1,
#else
          0,
#endif
#ifdef _WIN32
          1,
#else
          0,
#endif
#ifdef __aarch64__
          1
#else
          0
#endif
   );
   return 0;
}
