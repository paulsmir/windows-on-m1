#include "apple_agx_g13_codec.h"

#define APPLE_AGX_G13_NULL ((void *)0)

static void AppleAgxG13Zero(void *Buffer, APPLE_AGX_BACKEND_U32 Bytes) {
  unsigned char *buffer = (unsigned char *)Buffer;
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < Bytes; ++index)
    buffer[index] = 0u;
}

static void AppleAgxG13PutU32(unsigned char *Destination,
                              APPLE_AGX_BACKEND_U32 Value) {
  Destination[0] = (unsigned char)(Value & 0xffu);
  Destination[1] = (unsigned char)((Value >> 8) & 0xffu);
  Destination[2] = (unsigned char)((Value >> 16) & 0xffu);
  Destination[3] = (unsigned char)((Value >> 24) & 0xffu);
}

static void AppleAgxG13PutU64(unsigned char *Destination,
                              APPLE_AGX_BACKEND_U64 Value) {
  AppleAgxG13PutU32(Destination, (APPLE_AGX_BACKEND_U32)Value);
  AppleAgxG13PutU32(Destination + 4,
                    (APPLE_AGX_BACKEND_U32)(Value >> 32));
}

static APPLE_AGX_BACKEND_U32 AppleAgxG13GetU32(const unsigned char *Source) {
  return (APPLE_AGX_BACKEND_U32)Source[0] |
         ((APPLE_AGX_BACKEND_U32)Source[1] << 8) |
         ((APPLE_AGX_BACKEND_U32)Source[2] << 16) |
         ((APPLE_AGX_BACKEND_U32)Source[3] << 24);
}

static APPLE_AGX_BACKEND_U64 AppleAgxG13GetU64(const unsigned char *Source) {
  return (APPLE_AGX_BACKEND_U64)AppleAgxG13GetU32(Source) |
         ((APPLE_AGX_BACKEND_U64)AppleAgxG13GetU32(Source + 4) << 32);
}

static APPLE_AGX_BACKEND_BOOL
AppleAgxG13RunCommandValid(const APPLE_AGX_G13_RUN_COMMAND *Command) {
  return Command != APPLE_AGX_G13_NULL &&
         Command->QueueType <= (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueCompute &&
         Command->CommandQueueAddress != 0ULL &&
         (Command->CommandQueueAddress & 7ULL) == 0ULL &&
         Command->Head < APPLE_AGX_G13_RING_CAPACITY &&
         Command->EventNumber < APPLE_AGX_G13_EVENT_COUNT &&
         Command->NewQueue <= 1u;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13EncodeRunCommand(
    const APPLE_AGX_G13_RUN_COMMAND *Command,
    unsigned char Message[APPLE_AGX_G13_RUN_MESSAGE_SIZE]) {
  if (!AppleAgxG13RunCommandValid(Command) ||
      Message == APPLE_AGX_G13_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13Zero(Message, APPLE_AGX_G13_RUN_MESSAGE_SIZE);
  AppleAgxG13PutU32(Message, Command->QueueType);
  AppleAgxG13PutU64(Message + 4, Command->CommandQueueAddress);
  AppleAgxG13PutU32(Message + 12, Command->Head);
  AppleAgxG13PutU32(Message + 16, Command->EventNumber);
  AppleAgxG13PutU32(Message + 20, Command->NewQueue);
  AppleAgxG13PutU64(Message + 24, Command->Timestamp);
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13PrepareQueuePublication(
    APPLE_AGX_BACKEND_U64 WorkCommandAddress,
    APPLE_AGX_BACKEND_U32 CurrentWritePointer,
    APPLE_AGX_G13_QUEUE_PUBLICATION *Publication) {
  APPLE_AGX_G13_QUEUE_PUBLICATION candidate;
  if (Publication == APPLE_AGX_G13_NULL || WorkCommandAddress == 0ULL ||
      CurrentWritePointer >= APPLE_AGX_G13_RING_CAPACITY)
    return APPLE_AGX_BACKEND_FALSE;
  candidate.RingIndex = CurrentWritePointer;
  candidate.NextWritePointer =
      (CurrentWritePointer + 1u) % APPLE_AGX_G13_RING_CAPACITY;
  candidate.ExpectedDonePointer = candidate.NextWritePointer;
  AppleAgxG13PutU64(candidate.RingSlot, WorkCommandAddress);
  *Publication = candidate;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13BuildJoinedRun(
    const APPLE_AGX_G13_RUN_COMMAND *D3,
    const APPLE_AGX_G13_RUN_COMMAND *Ta,
    APPLE_AGX_G13_JOINED_RUN *Joined) {
  APPLE_AGX_G13_JOINED_RUN candidate;
  if (Joined == APPLE_AGX_G13_NULL || D3 == APPLE_AGX_G13_NULL ||
      Ta == APPLE_AGX_G13_NULL ||
      D3->QueueType != (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d ||
      Ta->QueueType != (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa ||
      D3->EventNumber == Ta->EventNumber ||
      !AppleAgxG13EncodeRunCommand(D3, candidate.Messages[0]) ||
      !AppleAgxG13EncodeRunCommand(Ta, candidate.Messages[1]))
    return APPLE_AGX_BACKEND_FALSE;
  candidate.QueueOrder[0] = (APPLE_AGX_BACKEND_U32)AppleAgxG13Queue3d;
  candidate.QueueOrder[1] = (APPLE_AGX_BACKEND_U32)AppleAgxG13QueueTa;
  *Joined = candidate;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13DecodeEvent(
    const unsigned char *Message, APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_G13_EVENT *Event) {
  APPLE_AGX_G13_EVENT candidate;
  APPLE_AGX_BACKEND_U32 kind;
  if (Message == APPLE_AGX_G13_NULL || Event == APPLE_AGX_G13_NULL ||
      MessageBytes != APPLE_AGX_G13_EVENT_MESSAGE_SIZE)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxG13Zero(&candidate, (APPLE_AGX_BACKEND_U32)sizeof(candidate));
  kind = AppleAgxG13GetU32(Message);
  candidate.Kind = kind;
  if (kind == (APPLE_AGX_BACKEND_U32)AppleAgxG13EventFlag) {
    candidate.Firing[0] = AppleAgxG13GetU64(Message + 4);
    candidate.Firing[1] = AppleAgxG13GetU64(Message + 12);
  } else if (kind == (APPLE_AGX_BACKEND_U32)AppleAgxG13EventTimeout) {
    APPLE_AGX_BACKEND_U32 rawStampIndex;
    candidate.TerminalFault = APPLE_AGX_BACKEND_TRUE;
    candidate.TimeoutCounter = AppleAgxG13GetU64(Message + 4);
    rawStampIndex = AppleAgxG13GetU32(Message + 12);
    candidate.TimeoutStampIndex =
        rawStampIndex <= 0x7fffffffu
            ? (APPLE_AGX_G13_S32)rawStampIndex
            : -(APPLE_AGX_G13_S32)(0xffffffffu - rawStampIndex) - 1;
  } else if (kind == (APPLE_AGX_BACKEND_U32)AppleAgxG13EventGrowTvb) {
    candidate.RequiresHandler = APPLE_AGX_BACKEND_TRUE;
    candidate.GrowTvbVmId = AppleAgxG13GetU32(Message + 4);
    candidate.GrowTvbBufferManagerId = AppleAgxG13GetU32(Message + 8);
    candidate.GrowTvbCounter = AppleAgxG13GetU32(Message + 12);
  } else if (kind == (APPLE_AGX_BACKEND_U32)AppleAgxG13EventFault ||
             kind == (APPLE_AGX_BACKEND_U32)AppleAgxG13EventChannelError) {
    candidate.TerminalFault = APPLE_AGX_BACKEND_TRUE;
  } else {
    return APPLE_AGX_BACKEND_FALSE;
  }
  *Event = candidate;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13EventHasNumber(
    const APPLE_AGX_G13_EVENT *Event,
    APPLE_AGX_BACKEND_U32 EventNumber) {
  APPLE_AGX_BACKEND_U32 word;
  APPLE_AGX_BACKEND_U32 bit;
  if (Event == APPLE_AGX_G13_NULL ||
      Event->Kind != (APPLE_AGX_BACKEND_U32)AppleAgxG13EventFlag ||
      EventNumber >= APPLE_AGX_G13_EVENT_COUNT)
    return APPLE_AGX_BACKEND_FALSE;
  word = EventNumber / 64u;
  bit = EventNumber % 64u;
  return (Event->Firing[word] & (1ULL << bit)) != 0ULL
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13CompletionSatisfied(
    const APPLE_AGX_G13_EVENT *Event, APPLE_AGX_BACKEND_U32 EventNumber,
    APPLE_AGX_BACKEND_U32 ObservedStamp,
    APPLE_AGX_BACKEND_U32 ExpectedStamp,
    APPLE_AGX_BACKEND_U32 ObservedDonePointer,
    APPLE_AGX_BACKEND_U32 ExpectedDonePointer) {
  if (ExpectedStamp == 0u ||
      !AppleAgxG13EventHasNumber(Event, EventNumber) ||
      ObservedDonePointer >= APPLE_AGX_G13_RING_CAPACITY ||
      ExpectedDonePointer >= APPLE_AGX_G13_RING_CAPACITY ||
      ObservedDonePointer != ExpectedDonePointer)
    return APPLE_AGX_BACKEND_FALSE;
  return (APPLE_AGX_BACKEND_U32)(ObservedStamp - ExpectedStamp) < 0x80000000u
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}
