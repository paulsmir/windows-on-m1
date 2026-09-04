#include "apple_agx_dma_shadow.h"

#define APPLE_AGX_DMA_SHADOW_NULL ((void *)0)
#define APPLE_AGX_DMA_SHADOW_ALIGNMENT 8u

static APPLE_AGX_BOOL AppleAgxDmaShadowRecordBytes(
    APPLE_AGX_U32 DmaBytes, APPLE_AGX_U32 *RecordBytes) {
  APPLE_AGX_U32 raw;

  if (RecordBytes == APPLE_AGX_DMA_SHADOW_NULL || DmaBytes == 0u ||
      DmaBytes > ~0u - (APPLE_AGX_U32)sizeof(APPLE_AGX_DMA_SHADOW_RECORD))
    return APPLE_AGX_FALSE;
  raw = (APPLE_AGX_U32)sizeof(APPLE_AGX_DMA_SHADOW_RECORD) + DmaBytes;
  if (raw > ~0u - (APPLE_AGX_DMA_SHADOW_ALIGNMENT - 1u))
    return APPLE_AGX_FALSE;
  *RecordBytes =
      (raw + APPLE_AGX_DMA_SHADOW_ALIGNMENT - 1u) &
      ~(APPLE_AGX_DMA_SHADOW_ALIGNMENT - 1u);
  return APPLE_AGX_TRUE;
}

static void AppleAgxDmaShadowCopy(unsigned char *Destination,
                                  const unsigned char *Source,
                                  APPLE_AGX_U32 Bytes) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    Destination[index] = Source[index];
}

static void AppleAgxDmaShadowZero(unsigned char *Destination,
                                  APPLE_AGX_U32 Bytes) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    Destination[index] = 0u;
}

static APPLE_AGX_BOOL AppleAgxDmaShadowRecordValid(
    const APPLE_AGX_DMA_SHADOW_RECORD *Record, APPLE_AGX_U32 Remaining) {
  APPLE_AGX_U32 expected;

  if (Record == APPLE_AGX_DMA_SHADOW_NULL ||
      Remaining < sizeof(*Record) ||
      Record->Magic != APPLE_AGX_DMA_SHADOW_MAGIC ||
      Record->Version != APPLE_AGX_DMA_SHADOW_VERSION ||
      Record->Reserved != 0u ||
      !AppleAgxDmaShadowRecordBytes(Record->DmaBytes, &expected) ||
      Record->RecordBytes != expected || Record->RecordBytes > Remaining ||
      Record->DmaOffset > ~0u - Record->DmaBytes)
    return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_DMA_SHADOW_HEADER *AppleAgxDmaShadowHeader(
    void *Storage) {
  return (APPLE_AGX_DMA_SHADOW_HEADER *)Storage;
}

static const APPLE_AGX_DMA_SHADOW_HEADER *AppleAgxDmaShadowConstHeader(
    const void *Storage) {
  return (const APPLE_AGX_DMA_SHADOW_HEADER *)Storage;
}

void AppleAgxDmaShadowInitialize(APPLE_AGX_DMA_SHADOW *Shadow,
                                 void *Storage,
                                 APPLE_AGX_U32 Capacity) {
  if (Shadow == (void *)0)
    return;
  Shadow->Storage = (unsigned char *)Storage;
  Shadow->Capacity = Capacity;
  Shadow->BytesUsed = 0u;
  if (Storage != APPLE_AGX_DMA_SHADOW_NULL &&
      Capacity >= sizeof(APPLE_AGX_DMA_SHADOW_HEADER)) {
    APPLE_AGX_DMA_SHADOW_HEADER *header = AppleAgxDmaShadowHeader(Storage);
    AppleAgxDmaShadowZero((unsigned char *)Storage, Capacity);
    header->Magic = APPLE_AGX_DMA_SHADOW_MAGIC;
    header->Version = APPLE_AGX_DMA_SHADOW_VERSION;
    header->BytesUsed = (APPLE_AGX_U32)sizeof(*header);
    header->Capacity = Capacity;
    header->RecordCount = 0u;
    header->State = (APPLE_AGX_U32)AppleAgxDmaShadowWritable;
    header->Fence = 0u;
    Shadow->BytesUsed = header->BytesUsed;
  }
}

APPLE_AGX_BOOL AppleAgxDmaShadowSeal(APPLE_AGX_DMA_SHADOW *Shadow,
                                     APPLE_AGX_U32 Fence) {
  APPLE_AGX_DMA_SHADOW_HEADER *header;

  if (Shadow == APPLE_AGX_DMA_SHADOW_NULL ||
      Shadow->Storage == APPLE_AGX_DMA_SHADOW_NULL ||
      Fence == 0u ||
      !AppleAgxDmaShadowValidate(Shadow->Storage, Shadow->BytesUsed))
    return APPLE_AGX_FALSE;
  header = AppleAgxDmaShadowHeader(Shadow->Storage);
  if (header->State != (APPLE_AGX_U32)AppleAgxDmaShadowWritable)
    return APPLE_AGX_FALSE;
  header->Fence = Fence;
  header->State = (APPLE_AGX_U32)AppleAgxDmaShadowSealed;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowIsSealedForFence(
    const void *Storage, APPLE_AGX_U32 BytesUsed, APPLE_AGX_U32 Fence) {
  const APPLE_AGX_DMA_SHADOW_HEADER *header;

  if (Fence == 0u || !AppleAgxDmaShadowValidate(Storage, BytesUsed))
    return APPLE_AGX_FALSE;
  header = AppleAgxDmaShadowConstHeader(Storage);
  return header->State == (APPLE_AGX_U32)AppleAgxDmaShadowSealed &&
                 header->Fence == Fence
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowIsSealed(const void *Storage,
                                          APPLE_AGX_U32 BytesUsed) {
  const APPLE_AGX_DMA_SHADOW_HEADER *header;

  if (!AppleAgxDmaShadowValidate(Storage, BytesUsed))
    return APPLE_AGX_FALSE;
  header = AppleAgxDmaShadowConstHeader(Storage);
  return header->State == (APPLE_AGX_U32)AppleAgxDmaShadowSealed
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowOpen(APPLE_AGX_DMA_SHADOW *Shadow,
                                     void *Storage,
                                     APPLE_AGX_U32 Capacity) {
  const APPLE_AGX_DMA_SHADOW_HEADER *header;

  if (Shadow == APPLE_AGX_DMA_SHADOW_NULL)
    return APPLE_AGX_FALSE;
  Shadow->Storage = APPLE_AGX_DMA_SHADOW_NULL;
  Shadow->Capacity = 0u;
  Shadow->BytesUsed = 0u;
  if (Storage == APPLE_AGX_DMA_SHADOW_NULL ||
      Capacity < sizeof(APPLE_AGX_DMA_SHADOW_HEADER))
    return APPLE_AGX_FALSE;
  header = AppleAgxDmaShadowConstHeader(Storage);
  if (header->Capacity != Capacity || header->BytesUsed > Capacity ||
      !AppleAgxDmaShadowValidate(Storage, header->BytesUsed))
    return APPLE_AGX_FALSE;
  Shadow->Storage = (unsigned char *)Storage;
  Shadow->Capacity = Capacity;
  Shadow->BytesUsed = header->BytesUsed;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowIsVirgin(const void *Storage,
                                         APPLE_AGX_U32 Capacity) {
  const unsigned char *bytes = (const unsigned char *)Storage;
  APPLE_AGX_U32 index;

  if (bytes == APPLE_AGX_DMA_SHADOW_NULL || Capacity == 0u)
    return APPLE_AGX_FALSE;
  for (index = 0u; index < Capacity; ++index) {
    if (bytes[index] != 0u)
      return APPLE_AGX_FALSE;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowExtent(const void *Storage,
                                       APPLE_AGX_U32 BytesUsed,
                                       APPLE_AGX_U32 *DmaExtent) {
  const unsigned char *bytes = (const unsigned char *)Storage;
  APPLE_AGX_U32 cursor =
      (APPLE_AGX_U32)sizeof(APPLE_AGX_DMA_SHADOW_HEADER);
  APPLE_AGX_U32 extent = 0u;

  if (DmaExtent == APPLE_AGX_DMA_SHADOW_NULL)
    return APPLE_AGX_FALSE;
  *DmaExtent = 0u;
  if (!AppleAgxDmaShadowValidate(Storage, BytesUsed))
    return APPLE_AGX_FALSE;
  while (cursor < BytesUsed) {
    const APPLE_AGX_DMA_SHADOW_RECORD *record =
        (const APPLE_AGX_DMA_SHADOW_RECORD *)(bytes + cursor);
    APPLE_AGX_U32 recordEnd = record->DmaOffset + record->DmaBytes;
    if (recordEnd > extent)
      extent = recordEnd;
    cursor += record->RecordBytes;
  }
  *DmaExtent = extent;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowAppend(APPLE_AGX_DMA_SHADOW *Shadow,
                                       APPLE_AGX_U32 DmaOffset,
                                       const void *Bytes,
                                       APPLE_AGX_U32 ByteCount) {
  APPLE_AGX_DMA_SHADOW_RECORD *record;
  APPLE_AGX_U32 recordBytes;
  APPLE_AGX_U32 cursor;
  APPLE_AGX_U32 dmaEnd;
  APPLE_AGX_DMA_SHADOW_HEADER *header;

  if (Shadow == APPLE_AGX_DMA_SHADOW_NULL ||
      Shadow->Storage == APPLE_AGX_DMA_SHADOW_NULL ||
      Bytes == APPLE_AGX_DMA_SHADOW_NULL ||
      Shadow->BytesUsed > Shadow->Capacity ||
      DmaOffset > ~0u - ByteCount ||
      !AppleAgxDmaShadowRecordBytes(ByteCount, &recordBytes) ||
      recordBytes > Shadow->Capacity - Shadow->BytesUsed)
    return APPLE_AGX_FALSE;
  header = AppleAgxDmaShadowHeader(Shadow->Storage);
  if (!AppleAgxDmaShadowValidate(Shadow->Storage, Shadow->BytesUsed) ||
      header->State != (APPLE_AGX_U32)AppleAgxDmaShadowWritable ||
      header->RecordCount == ~0u)
    return APPLE_AGX_FALSE;
  dmaEnd = DmaOffset + ByteCount;

  cursor = (APPLE_AGX_U32)sizeof(*header);
  while (cursor < Shadow->BytesUsed) {
    const APPLE_AGX_DMA_SHADOW_RECORD *existing =
        (const APPLE_AGX_DMA_SHADOW_RECORD *)(Shadow->Storage + cursor);
    APPLE_AGX_U32 existingEnd;
    if (!AppleAgxDmaShadowRecordValid(existing,
                                      Shadow->BytesUsed - cursor))
      return APPLE_AGX_FALSE;
    existingEnd = existing->DmaOffset + existing->DmaBytes;
    if (DmaOffset < existingEnd && existing->DmaOffset < dmaEnd)
      return APPLE_AGX_FALSE;
    cursor += existing->RecordBytes;
  }

  record = (APPLE_AGX_DMA_SHADOW_RECORD *)(Shadow->Storage +
                                           Shadow->BytesUsed);
  record->Magic = APPLE_AGX_DMA_SHADOW_MAGIC;
  record->Version = APPLE_AGX_DMA_SHADOW_VERSION;
  record->RecordBytes = recordBytes;
  record->DmaOffset = DmaOffset;
  record->DmaBytes = ByteCount;
  record->Reserved = 0u;
  AppleAgxDmaShadowCopy((unsigned char *)(record + 1),
                        (const unsigned char *)Bytes, ByteCount);
  AppleAgxDmaShadowZero((unsigned char *)(record + 1) + ByteCount,
                        recordBytes - (APPLE_AGX_U32)sizeof(*record) -
                            ByteCount);
  Shadow->BytesUsed += recordBytes;
  header->BytesUsed = Shadow->BytesUsed;
  ++header->RecordCount;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowValidate(const void *Storage,
                                         APPLE_AGX_U32 BytesUsed) {
  const unsigned char *bytes = (const unsigned char *)Storage;
  const APPLE_AGX_DMA_SHADOW_HEADER *header;
  APPLE_AGX_U32 cursor;
  APPLE_AGX_U32 records = 0u;

  if (bytes == APPLE_AGX_DMA_SHADOW_NULL ||
      BytesUsed < sizeof(APPLE_AGX_DMA_SHADOW_HEADER))
    return APPLE_AGX_FALSE;
  header = AppleAgxDmaShadowConstHeader(Storage);
  if (header->Magic != APPLE_AGX_DMA_SHADOW_MAGIC ||
      header->Version != APPLE_AGX_DMA_SHADOW_VERSION ||
      (header->State != (APPLE_AGX_U32)AppleAgxDmaShadowWritable &&
       header->State != (APPLE_AGX_U32)AppleAgxDmaShadowSealed) ||
      (header->State == (APPLE_AGX_U32)AppleAgxDmaShadowWritable &&
       header->Fence != 0u) ||
      (header->State == (APPLE_AGX_U32)AppleAgxDmaShadowSealed &&
       header->Fence == 0u) ||
      header->Capacity < BytesUsed ||
      header->BytesUsed != BytesUsed)
    return APPLE_AGX_FALSE;
  cursor = (APPLE_AGX_U32)sizeof(*header);
  while (cursor < BytesUsed) {
    const APPLE_AGX_DMA_SHADOW_RECORD *record =
        (const APPLE_AGX_DMA_SHADOW_RECORD *)(bytes + cursor);
    APPLE_AGX_U32 otherCursor;
    APPLE_AGX_U32 recordEnd;
    if (!AppleAgxDmaShadowRecordValid(record, BytesUsed - cursor))
      return APPLE_AGX_FALSE;
    recordEnd = record->DmaOffset + record->DmaBytes;
    otherCursor = cursor + record->RecordBytes;
    while (otherCursor < BytesUsed) {
      const APPLE_AGX_DMA_SHADOW_RECORD *other =
          (const APPLE_AGX_DMA_SHADOW_RECORD *)(bytes + otherCursor);
      APPLE_AGX_U32 otherEnd;
      if (!AppleAgxDmaShadowRecordValid(other,
                                        BytesUsed - otherCursor))
        return APPLE_AGX_FALSE;
      otherEnd = other->DmaOffset + other->DmaBytes;
      if (record->DmaOffset < otherEnd && other->DmaOffset < recordEnd)
        return APPLE_AGX_FALSE;
      otherCursor += other->RecordBytes;
    }
    cursor += record->RecordBytes;
    ++records;
  }
  return cursor == BytesUsed && records == header->RecordCount
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowFind(const void *Storage,
                                     APPLE_AGX_U32 BytesUsed,
                                     APPLE_AGX_U32 DmaOffset,
                                     APPLE_AGX_U32 DmaBytes,
                                     APPLE_AGX_DMA_SHADOW_VIEW *View) {
  const unsigned char *bytes = (const unsigned char *)Storage;
  APPLE_AGX_U32 cursor =
      (APPLE_AGX_U32)sizeof(APPLE_AGX_DMA_SHADOW_HEADER);

  if (View == APPLE_AGX_DMA_SHADOW_NULL || DmaBytes == 0u ||
      !AppleAgxDmaShadowValidate(Storage, BytesUsed))
    return APPLE_AGX_FALSE;
  View->DmaOffset = 0u;
  View->DmaBytes = 0u;
  View->Bytes = APPLE_AGX_DMA_SHADOW_NULL;
  while (cursor < BytesUsed) {
    const APPLE_AGX_DMA_SHADOW_RECORD *record =
        (const APPLE_AGX_DMA_SHADOW_RECORD *)(bytes + cursor);
    if (record->DmaOffset == DmaOffset && record->DmaBytes == DmaBytes) {
      View->DmaOffset = DmaOffset;
      View->DmaBytes = DmaBytes;
      View->Bytes = (const unsigned char *)(record + 1);
      return APPLE_AGX_TRUE;
    }
    cursor += record->RecordBytes;
  }
  return APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowPatchU64(void *Storage,
                                         APPLE_AGX_U32 BytesUsed,
                                         APPLE_AGX_U32 PatchOffset,
                                         APPLE_AGX_U64 Value) {
  unsigned char *bytes = (unsigned char *)Storage;
  APPLE_AGX_U32 cursor =
      (APPLE_AGX_U32)sizeof(APPLE_AGX_DMA_SHADOW_HEADER);

  if (PatchOffset > ~0u - (APPLE_AGX_U32)sizeof(Value) ||
      !AppleAgxDmaShadowValidate(Storage, BytesUsed))
    return APPLE_AGX_FALSE;
  if (AppleAgxDmaShadowHeader(Storage)->State !=
      (APPLE_AGX_U32)AppleAgxDmaShadowWritable)
    return APPLE_AGX_FALSE;
  while (cursor < BytesUsed) {
    APPLE_AGX_DMA_SHADOW_RECORD *record =
        (APPLE_AGX_DMA_SHADOW_RECORD *)(bytes + cursor);
    APPLE_AGX_U32 patchEnd = PatchOffset + (APPLE_AGX_U32)sizeof(Value);
    APPLE_AGX_U32 recordEnd = record->DmaOffset + record->DmaBytes;
    if (PatchOffset >= record->DmaOffset && patchEnd <= recordEnd) {
      unsigned char encoded[sizeof(Value)];
      APPLE_AGX_U32 index;
      for (index = 0u; index < sizeof(Value); ++index)
        encoded[index] = (unsigned char)(Value >> (index * 8u));
      AppleAgxDmaShadowCopy((unsigned char *)(record + 1) +
                                (PatchOffset - record->DmaOffset),
                            encoded, (APPLE_AGX_U32)sizeof(encoded));
      return APPLE_AGX_TRUE;
    }
    cursor += record->RecordBytes;
  }
  return APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowMatchesU64(
    const void *Storage, APPLE_AGX_U32 BytesUsed,
    APPLE_AGX_U32 PatchOffset, APPLE_AGX_U64 Value) {
  const unsigned char *bytes = (const unsigned char *)Storage;
  APPLE_AGX_U32 cursor =
      (APPLE_AGX_U32)sizeof(APPLE_AGX_DMA_SHADOW_HEADER);

  if (PatchOffset > ~0u - (APPLE_AGX_U32)sizeof(Value) ||
      !AppleAgxDmaShadowIsSealed(Storage, BytesUsed))
    return APPLE_AGX_FALSE;
  while (cursor < BytesUsed) {
    const APPLE_AGX_DMA_SHADOW_RECORD *record =
        (const APPLE_AGX_DMA_SHADOW_RECORD *)(bytes + cursor);
    APPLE_AGX_U32 patchEnd = PatchOffset + (APPLE_AGX_U32)sizeof(Value);
    APPLE_AGX_U32 recordEnd = record->DmaOffset + record->DmaBytes;
    if (PatchOffset >= record->DmaOffset && patchEnd <= recordEnd) {
      const unsigned char *stored = (const unsigned char *)(record + 1) +
                                    (PatchOffset - record->DmaOffset);
      APPLE_AGX_U32 index;
      for (index = 0u; index < sizeof(Value); ++index) {
        if (stored[index] != (unsigned char)(Value >> (index * 8u)))
          return APPLE_AGX_FALSE;
      }
      return APPLE_AGX_TRUE;
    }
    cursor += record->RecordBytes;
  }
  return APPLE_AGX_FALSE;
}

static const APPLE_AGX_DMA_SHADOW_RECORD *AppleAgxDmaShadowRecordAtOffset(
    const unsigned char *Storage, APPLE_AGX_U32 BytesUsed,
    APPLE_AGX_U32 DmaOffset) {
  APPLE_AGX_U32 cursor =
      (APPLE_AGX_U32)sizeof(APPLE_AGX_DMA_SHADOW_HEADER);

  while (cursor < BytesUsed) {
    const APPLE_AGX_DMA_SHADOW_RECORD *record =
        (const APPLE_AGX_DMA_SHADOW_RECORD *)(Storage + cursor);
    if (record->DmaOffset == DmaOffset)
      return record;
    cursor += record->RecordBytes;
  }
  return APPLE_AGX_DMA_SHADOW_NULL;
}

APPLE_AGX_BOOL AppleAgxDmaShadowCoversSubmission(
    const void *Storage, APPLE_AGX_U32 BytesUsed,
    APPLE_AGX_U32 SubmissionStart, APPLE_AGX_U32 SubmissionEnd) {
  const unsigned char *storage = (const unsigned char *)Storage;
  APPLE_AGX_U32 current;

  if (SubmissionStart >= SubmissionEnd ||
      !AppleAgxDmaShadowValidate(Storage, BytesUsed))
    return APPLE_AGX_FALSE;
  current = SubmissionStart;
  while (current < SubmissionEnd) {
    const APPLE_AGX_DMA_SHADOW_RECORD *record =
        AppleAgxDmaShadowRecordAtOffset(storage, BytesUsed, current);
    if (record == APPLE_AGX_DMA_SHADOW_NULL ||
        record->DmaBytes > SubmissionEnd - current)
      return APPLE_AGX_FALSE;
    current += record->DmaBytes;
  }
  return current == SubmissionEnd ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxDmaShadowCopySubmission(
    const void *Storage, APPLE_AGX_U32 BytesUsed,
    APPLE_AGX_U32 SubmissionStart, APPLE_AGX_U32 SubmissionEnd,
    void *Destination, APPLE_AGX_U32 DestinationCapacity,
    APPLE_AGX_U32 *BytesCopied) {
  const unsigned char *storage = (const unsigned char *)Storage;
  unsigned char *destination = (unsigned char *)Destination;
  APPLE_AGX_U32 current;
  APPLE_AGX_U32 outputOffset;
  APPLE_AGX_U32 required;

  if (BytesCopied == APPLE_AGX_DMA_SHADOW_NULL)
    return APPLE_AGX_FALSE;
  *BytesCopied = 0u;
  if (destination == APPLE_AGX_DMA_SHADOW_NULL ||
      SubmissionStart >= SubmissionEnd ||
      !AppleAgxDmaShadowValidate(Storage, BytesUsed))
    return APPLE_AGX_FALSE;
  required = SubmissionEnd - SubmissionStart;
  if (required > DestinationCapacity)
    return APPLE_AGX_FALSE;

  /* Validate the complete interval before modifying Destination. */
  if (!AppleAgxDmaShadowCoversSubmission(
          Storage, BytesUsed, SubmissionStart, SubmissionEnd))
    return APPLE_AGX_FALSE;

  current = SubmissionStart;
  outputOffset = 0u;
  while (current < SubmissionEnd) {
    const APPLE_AGX_DMA_SHADOW_RECORD *record =
        AppleAgxDmaShadowRecordAtOffset(storage, BytesUsed, current);
    AppleAgxDmaShadowCopy(destination + outputOffset,
                          (const unsigned char *)(record + 1),
                          record->DmaBytes);
    outputOffset += record->DmaBytes;
    current += record->DmaBytes;
  }
  *BytesCopied = outputOffset;
  return APPLE_AGX_TRUE;
}
