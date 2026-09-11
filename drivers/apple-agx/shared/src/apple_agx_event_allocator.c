#include "apple_agx_event_allocator.h"

#define APPLE_AGX_EVENT_NULL ((void *)0)

static void AppleAgxEventZero(void *Address, APPLE_AGX_U32 Bytes) {
  unsigned char *destination = (unsigned char *)Address;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    destination[index] = 0u;
}

static APPLE_AGX_BOOL AppleAgxEventReserved(
    const APPLE_AGX_EVENT_ALLOCATOR *Allocator, APPLE_AGX_U32 Event) {
  return (Allocator->Reserved[Event / 32u] & (1u << (Event % 32u))) != 0u
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static void AppleAgxEventSetReserved(APPLE_AGX_EVENT_ALLOCATOR *Allocator,
                                     APPLE_AGX_U32 Event,
                                     APPLE_AGX_BOOL Reserved) {
  APPLE_AGX_U32 mask = 1u << (Event % 32u);
  if (Reserved)
    Allocator->Reserved[Event / 32u] |= mask;
  else
    Allocator->Reserved[Event / 32u] &= ~mask;
}

APPLE_AGX_BOOL AppleAgxEventAllocatorInitialize(
    APPLE_AGX_EVENT_ALLOCATOR *Allocator) {
  if (Allocator == APPLE_AGX_EVENT_NULL)
    return APPLE_AGX_FALSE;
  AppleAgxEventZero(Allocator, (APPLE_AGX_U32)sizeof(*Allocator));
  Allocator->NextLease = 1u;
  Allocator->Initialized = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxEventAllocatorReservePair(
    APPLE_AGX_EVENT_ALLOCATOR *Allocator, APPLE_AGX_EVENT_PAIR *Pair) {
  APPLE_AGX_EVENT_PAIR candidate;
  APPLE_AGX_U32 first;
  APPLE_AGX_U32 second;
  if (Allocator == APPLE_AGX_EVENT_NULL || Pair == APPLE_AGX_EVENT_NULL ||
      !Allocator->Initialized)
    return APPLE_AGX_FALSE;

  first = APPLE_AGX_EVENT_COUNT;
  second = APPLE_AGX_EVENT_COUNT;
  for (candidate.Ta = 0u; candidate.Ta < APPLE_AGX_EVENT_COUNT;
       ++candidate.Ta) {
    if (AppleAgxEventReserved(Allocator, candidate.Ta))
      continue;
    if (first == APPLE_AGX_EVENT_COUNT)
      first = candidate.Ta;
    else {
      second = candidate.Ta;
      break;
    }
  }
  if (second == APPLE_AGX_EVENT_COUNT)
    return APPLE_AGX_FALSE;

  if (Allocator->NextLease == 0u)
    Allocator->NextLease = 1u;
  candidate.Ta = first;
  candidate.D3 = second;
  candidate.Lease = Allocator->NextLease++;
  AppleAgxEventSetReserved(Allocator, first, APPLE_AGX_TRUE);
  AppleAgxEventSetReserved(Allocator, second, APPLE_AGX_TRUE);
  Allocator->Leases[first] = candidate.Lease;
  Allocator->Leases[second] = candidate.Lease;
  *Pair = candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxEventAllocatorReleasePair(
    APPLE_AGX_EVENT_ALLOCATOR *Allocator, const APPLE_AGX_EVENT_PAIR *Pair) {
  if (Allocator == APPLE_AGX_EVENT_NULL || Pair == APPLE_AGX_EVENT_NULL ||
      !Allocator->Initialized || Pair->Ta >= APPLE_AGX_EVENT_COUNT ||
      Pair->D3 >= APPLE_AGX_EVENT_COUNT || Pair->Ta == Pair->D3 ||
      Pair->Lease == 0u || !AppleAgxEventReserved(Allocator, Pair->Ta) ||
      !AppleAgxEventReserved(Allocator, Pair->D3) ||
      Allocator->Leases[Pair->Ta] != Pair->Lease ||
      Allocator->Leases[Pair->D3] != Pair->Lease)
    return APPLE_AGX_FALSE;
  AppleAgxEventSetReserved(Allocator, Pair->Ta, APPLE_AGX_FALSE);
  AppleAgxEventSetReserved(Allocator, Pair->D3, APPLE_AGX_FALSE);
  Allocator->Leases[Pair->Ta] = 0u;
  Allocator->Leases[Pair->D3] = 0u;
  return APPLE_AGX_TRUE;
}

#undef APPLE_AGX_EVENT_NULL
