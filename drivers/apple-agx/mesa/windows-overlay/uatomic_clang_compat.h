#ifndef APPLE_AGX_UATOMIC_CLANG_COMPAT_H
#define APPLE_AGX_UATOMIC_CLANG_COMPAT_H

/*
 * The pinned Mesa MSVC atomic branch uses the lowercase spellings emitted by
 * Microsoft's headers. clang-cl exposes the same operations under the
 * documented uppercase intrinsic names. Include the declarations first, then
 * provide aliases only for the exact observed 64-bit spellings.
 */
#include <intrin.h>
#include <stdint.h>
#include <string.h>

#define _interlockedexchange64 _InterlockedExchange64
#define _interlockedexchangeadd64 _InterlockedExchangeAdd64
#define _interlockedincrement64 _InterlockedIncrement64
#define _interlockeddecrement64 _InterlockedDecrement64
/* clang-cl exposes _InterlockedAdd64 only for AArch64. The x64 primitive is
 * ExchangeAdd64; adding the operand reproduces MSVC's updated-value contract.
 */
static __inline__ __int64
apple_agx_interlockedadd64(__int64 volatile *addend, __int64 value)
{
   /*
    * ExchangeAdd returns the pre-update signed value.  Computing the result
    * as signed arithmetic would make the MSVC two's-complement wrap contract
    * undefined at INT64_MAX + 1 and INT64_MIN - 1.  Use uint64_t for the
    * modulo-2^64 addition and copy the resulting representation back to the
    * signed return type without a value-changing conversion.
    */
   __int64 previous = _InterlockedExchangeAdd64(addend, value);
   uint64_t previous_bits, value_bits, updated_bits;
   __int64 updated;

   memcpy(&previous_bits, &previous, sizeof(previous_bits));
   memcpy(&value_bits, &value, sizeof(value_bits));
   updated_bits = previous_bits + value_bits;
   memcpy(&updated, &updated_bits, sizeof(updated));
   return updated;
}

#define _interlockedadd64 apple_agx_interlockedadd64

static __inline__ long
apple_agx_interlockedadd32(long volatile *addend, long value)
{
   long previous = _InterlockedExchangeAdd(addend, value);
   uint32_t updated_bits = (uint32_t)previous + (uint32_t)value;
   long updated;
   memcpy(&updated, &updated_bits, sizeof(updated));
   return updated;
}

#define _interlockedadd apple_agx_interlockedadd32

#endif /* APPLE_AGX_UATOMIC_CLANG_COMPAT_H */
