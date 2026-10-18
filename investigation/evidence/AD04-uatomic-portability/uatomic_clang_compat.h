#ifndef APPLE_AGX_UATOMIC_CLANG_COMPAT_H
#define APPLE_AGX_UATOMIC_CLANG_COMPAT_H

/*
 * The pinned Mesa MSVC atomic branch uses the lowercase spellings emitted by
 * Microsoft's headers. clang-cl exposes the same operations under the
 * documented uppercase intrinsic names. Include the declarations first, then
 * provide aliases only for the exact observed 64-bit spellings.
 */
#include <intrin.h>

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
   return _InterlockedExchangeAdd64(addend, value) + value;
}

#define _interlockedadd64 apple_agx_interlockedadd64

#endif /* APPLE_AGX_UATOMIC_CLANG_COMPAT_H */
