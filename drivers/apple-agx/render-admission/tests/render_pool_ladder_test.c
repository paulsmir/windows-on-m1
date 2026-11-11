#include "render_pool_ladder.h"

#include <assert.h>
#include <stdio.h>

/* Simulates AdmissionPoolAllocSlab's attempt loop: rungs in order until one
 * succeeds (Succeeds[rung]) or the ladder ends; records the rungs tried. */
static unsigned int run_slab(ADMISSION_POOL_LADDER *ladder, int enabled,
                             const int succeeds[3], unsigned int tried[4],
                             unsigned int *plan_out) {
  unsigned int plan = AdmissionPoolLadderBegin(ladder, enabled);
  unsigned int rung, count = 0u;
  int rung256_tried = 0, rung256_ok = 0;
  for (rung = ADMISSION_POOL_RUNG_LARGE; rung != ADMISSION_POOL_RUNG_END;
       rung = AdmissionPoolLadderNextRung(rung, plan)) {
    assert(count < 4u);
    tried[count++] = rung;
    if (rung == ADMISSION_POOL_RUNG_256K) rung256_tried = 1;
    if (succeeds[rung]) {
      if (rung == ADMISSION_POOL_RUNG_256K) rung256_ok = 1;
      break;
    }
  }
  AdmissionPoolLadderEnd(ladder, rung256_tried, rung256_ok);
  if (plan_out) *plan_out = plan;
  return count;
}

int main(void) {
  static const int all_fail[3] = {0, 0, 0};
  static const int only_256k[3] = {0, 0, 1};
  static const int only_large[3] = {1, 0, 0};
  ADMISSION_POOL_LADDER ladder = {0};
  unsigned int tried[4], plan, n, i;

  /* Switch off: exactly {2 MiB, 64 KiB}, and the back-off never moves. */
  ladder.Backoff = 0u;
  n = run_slab(&ladder, 0, all_fail, tried, &plan);
  assert(plan == ADMISSION_POOL_LADDER_OFF);
  assert(n == 2u && tried[0] == ADMISSION_POOL_RUNG_LARGE &&
         tried[1] == ADMISSION_POOL_RUNG_64K);
  assert(ladder.Backoff == 0u);
  ladder.Backoff = 5u; /* off ignores a stale counter and leaves it alone */
  n = run_slab(&ladder, 0, all_fail, tried, &plan);
  assert(plan == ADMISSION_POOL_LADDER_OFF && n == 2u && ladder.Backoff == 5u);

  /* Switch on: {2 MiB, 256 KiB, 64 KiB}. */
  ladder.Backoff = 0u;
  n = run_slab(&ladder, 1, only_256k, tried, &plan);
  assert(plan == ADMISSION_POOL_LADDER_TRY);
  assert(n == 2u && tried[0] == ADMISSION_POOL_RUNG_LARGE &&
         tried[1] == ADMISSION_POOL_RUNG_256K);
  /* A 256 KiB success does not start a back-off. */
  assert(ladder.Backoff == 0u);
  n = run_slab(&ladder, 1, only_256k, tried, &plan);
  assert(plan == ADMISSION_POOL_LADDER_TRY && n == 2u);
  /* A 2 MiB success never reaches the rung and leaves no back-off. */
  n = run_slab(&ladder, 1, only_large, tried, &plan);
  assert(n == 1u && tried[0] == ADMISSION_POOL_RUNG_LARGE && ladder.Backoff == 0u);

  /* A 256 KiB failure: the full ladder ran, then the next 16 allocations
   * skip the rung, and the 17th tries it again. */
  n = run_slab(&ladder, 1, all_fail, tried, &plan);
  assert(plan == ADMISSION_POOL_LADDER_TRY);
  assert(n == 3u && tried[0] == ADMISSION_POOL_RUNG_LARGE &&
         tried[1] == ADMISSION_POOL_RUNG_256K &&
         tried[2] == ADMISSION_POOL_RUNG_64K);
  assert(ladder.Backoff == ADMISSION_POOL_LADDER_BACKOFF_SLABS);
  for (i = 0u; i < ADMISSION_POOL_LADDER_BACKOFF_SLABS; ++i) {
    n = run_slab(&ladder, 1, all_fail, tried, &plan);
    assert(plan == ADMISSION_POOL_LADDER_BACKED_OFF);
    assert(n == 2u && tried[0] == ADMISSION_POOL_RUNG_LARGE &&
           tried[1] == ADMISSION_POOL_RUNG_64K);
  }
  assert(ladder.Backoff == 0u);
  n = run_slab(&ladder, 1, only_256k, tried, &plan);
  assert(plan == ADMISSION_POOL_LADDER_TRY && n == 2u &&
         tried[1] == ADMISSION_POOL_RUNG_256K && ladder.Backoff == 0u);
  assert(ADMISSION_POOL_LADDER_BACKOFF_SLABS == 16u);

  puts("render_pool_ladder_test: ok");
  return 0;
}
