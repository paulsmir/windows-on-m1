#include <assert.h>
#include <string.h>

#include "apple_agx_physical_paging.h"

int main(void) {
  APPLE_AGX_PHYSICAL_PAGING_PLAN plan;
  unsigned char local[32];
  unsigned char system[32];
  unsigned char before[32];
  unsigned int index;

  memset(local, 0, sizeof(local));
  for (index = 0; index < sizeof(system); ++index)
    system[index] = (unsigned char)(index + 1u);

  assert(AppleAgxPhysicalPagingPlanTransfer(
             0u, 0u, 2u, 0x1008u, 0x1000u, sizeof(local), 2u, 3u, 7u,
             &plan) == AppleAgxPhysicalPagingOk);
  assert(plan.Kind == AppleAgxPhysicalPagingUpload);
  assert(plan.LocalOffset == 10u && plan.SystemOffset == 3u);
  assert(AppleAgxPhysicalPagingExecute(&plan, local, sizeof(local), system,
                                       sizeof(system)) ==
         AppleAgxPhysicalPagingOk);
  assert(memcmp(local + 10, system + 3, 7) == 0);

  assert(AppleAgxPhysicalPagingPlanTransfer(
             2u, 0x1010u, 0u, 0u, 0x1000u, sizeof(local), 1u, 5u, 4u,
             &plan) == AppleAgxPhysicalPagingOk);
  assert(plan.Kind == AppleAgxPhysicalPagingDownload);
  assert(AppleAgxPhysicalPagingExecute(&plan, local, sizeof(local), system,
                                       sizeof(system)) ==
         AppleAgxPhysicalPagingOk);
  assert(memcmp(system + 5, local + 17, 4) == 0);

  assert(AppleAgxPhysicalPagingPlanFill(2u, 0x1001u, 0x1000u,
                                        sizeof(local), 7u, 0x44332211u,
                                        &plan) == AppleAgxPhysicalPagingOk);
  assert(AppleAgxPhysicalPagingExecute(&plan, local, sizeof(local), 0, 0) ==
         AppleAgxPhysicalPagingOk);
  assert(local[1] == 0x11u && local[2] == 0x22u && local[3] == 0x33u &&
         local[4] == 0x44u && local[5] == 0x11u && local[6] == 0x22u &&
         local[7] == 0x33u);

  memcpy(before, local, sizeof(local));
  assert(AppleAgxPhysicalPagingPlanTransfer(
             1u, 0u, 2u, 0x1000u, 0x1000u, sizeof(local), 0u, 0u, 4u,
             &plan) == AppleAgxPhysicalPagingUnsupportedEndpoint);
  assert(memcmp(before, local, sizeof(local)) == 0);
  assert(AppleAgxPhysicalPagingPlanFill(2u, ~0ULL - 1u, 0x1000u,
                                        sizeof(local), 8u, 0u, &plan) ==
         AppleAgxPhysicalPagingOutOfRange);
  assert(AppleAgxPhysicalPagingPlanDiscard(2u, 0x1004u, 0x1000u,
                                           sizeof(local), &plan) ==
         AppleAgxPhysicalPagingOk);
  assert(plan.Kind == AppleAgxPhysicalPagingDiscard);
  assert(AppleAgxPhysicalPagingExecute(&plan, local, sizeof(local), 0, 0) ==
         AppleAgxPhysicalPagingOk);
  assert(memcmp(before, local, sizeof(local)) == 0);
  return 0;
}
