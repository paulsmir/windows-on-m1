/* SPDX-License-Identifier: MIT
 * Original Windows query reduction. Sources contain completed batch end
 * timestamps in nanoseconds; the immutable seed was captured at query End.
 */
#include "compiler/libcl/libcl.h"
#include "agx_win32_query_timestamp.h"

/* Dispatch exactly one invocation after every source producer. The caller
 * retains the address array, source BOs and destination through completion.
 * As with native libagx_copy_timestamp, volatile avoids folding timestamp
 * loads; queue ordering and publication visibility are the caller's contract.
 */
KERNEL(1)
libagx_win32_query_timestamp(uint64_t addresses_array_gpu, uint32_t count,
                           uint64_t seed, uint64_t destination_gpu)
{
   if (count > AGX_WIN32_QUERY_TIMESTAMP_MAX_SOURCES)
      return;

   const global uint64_t *addresses =
      (const global uint64_t *)(uintptr_t)addresses_array_gpu;
   uint64_t value = seed;
   for (uint32_t i = 0; i < count; ++i) {
      const volatile global uint64_t *source =
         (const volatile global uint64_t *)(uintptr_t)addresses[i];
      uint64_t end = *source;
      value = end > value ? end : value;
   }

   *(global uint64_t *)(uintptr_t)destination_gpu = value;
}
