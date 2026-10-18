#include "util/u_atomic.h"

int main(void)
{
   volatile __int64 value = 3;

   if (p_atomic_xchg(&value, 9) != 3 || value != 9)
      return 1;
   if (p_atomic_fetch_add(&value, 4) != 9 || value != 13)
      return 2;
   if (p_atomic_inc_return(&value) != 14 || value != 14)
      return 3;
   if (p_atomic_dec_return(&value) != 13 || value != 13)
      return 4;
   if (p_atomic_add_return(&value, -5) != 8 || value != 8)
      return 5;

   return 0;
}
