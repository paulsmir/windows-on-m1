#include "apple_agx_event_allocator.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_pair_lifetime_is_unique_and_reusable(void) {
  APPLE_AGX_EVENT_ALLOCATOR allocator;
  APPLE_AGX_EVENT_PAIR first;
  APPLE_AGX_EVENT_PAIR second;

  memset(&allocator, 0xa5, sizeof(allocator));
  assert(AppleAgxEventAllocatorInitialize(&allocator));
  assert(AppleAgxEventAllocatorReservePair(&allocator, &first));
  assert(first.Ta == 0u && first.D3 == 1u);
  assert(first.Ta != first.D3);
  assert(AppleAgxEventAllocatorReservePair(&allocator, &second));
  assert(second.Ta == 2u && second.D3 == 3u);
  {
    APPLE_AGX_EVENT_PAIR forged = first;
    forged.D3 = second.D3;
    assert(!AppleAgxEventAllocatorReleasePair(&allocator, &forged));
  }
  assert(AppleAgxEventAllocatorReleasePair(&allocator, &first));
  assert(AppleAgxEventAllocatorReservePair(&allocator, &first));
  assert(first.Ta == 0u && first.D3 == 1u);
}

static void test_exhaustion_never_returns_partial_pair(void) {
  APPLE_AGX_EVENT_ALLOCATOR allocator;
  APPLE_AGX_EVENT_PAIR pairs[APPLE_AGX_EVENT_COUNT / 2u];
  APPLE_AGX_EVENT_PAIR rejected = {0x55u, 0xaau, 0x1234u};
  unsigned int index;

  assert(AppleAgxEventAllocatorInitialize(&allocator));
  for (index = 0u; index < APPLE_AGX_EVENT_COUNT / 2u; ++index)
    assert(AppleAgxEventAllocatorReservePair(&allocator, &pairs[index]));
  assert(!AppleAgxEventAllocatorReservePair(&allocator, &rejected));
  assert(rejected.Ta == 0x55u && rejected.D3 == 0xaau &&
         rejected.Lease == 0x1234u);
  assert(AppleAgxEventAllocatorReleasePair(&allocator, &pairs[17]));
  assert(AppleAgxEventAllocatorReservePair(&allocator, &rejected));
  assert(rejected.Ta == 34u && rejected.D3 == 35u);
}

int main(void) {
  test_pair_lifetime_is_unique_and_reusable();
  test_exhaustion_never_returns_partial_pair();
  puts("apple_agx_event_allocator_test: ok");
  return 0;
}
