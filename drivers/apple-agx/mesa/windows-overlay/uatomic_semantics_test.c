#include <intrin.h>
#include <stdint.h>
#include <limits.h>

int main(void)
{
   volatile __int64 value = 3;

   if (_interlockedexchange64(&value, 9) != 3 || value != 9)
      return 1;
   if (_interlockedexchangeadd64(&value, 4) != 9 || value != 13)
      return 2;
   if (_interlockedincrement64(&value) != 14 || value != 14)
      return 3;
   if (_interlockeddecrement64(&value) != 13 || value != 13)
      return 4;
   if (_interlockedadd64(&value, -5) != 8 || value != 8)
      return 5;

   value = INT64_MAX;
   if (_interlockedadd64(&value, 1) != INT64_MIN || value != INT64_MIN)
      return 6;
   if (_interlockedadd64(&value, -1) != INT64_MAX || value != INT64_MAX)
      return 7;

   return 0;
}
