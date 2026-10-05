#include "apple_agx_gpuva_b1_status.h"
#include <assert.h>

int main(void) {
  const unsigned int busy = 0x80000011u;
  const unsigned int timeout = 0xc00000b5u;
  APPLE_AGX_GPUVA_B1_STATUS result;

  result = AppleAgxGpuvaB1FinalStatus(timeout, 0u, busy);
  assert(result.FirstFailure == timeout);
  assert(result.Terminal == busy);

  result = AppleAgxGpuvaB1FinalStatus(0u, 0u, busy);
  assert(result.FirstFailure == busy);
  assert(result.Terminal == busy);

  result = AppleAgxGpuvaB1FinalStatus(0u, 1u, busy);
  assert(result.FirstFailure == 0u);
  assert(result.Terminal == 0u);
  return 0;
}
