#include "render_present.h"
#include <string.h>

#define PRESENT_LOCATION_MASK 0x00ffffffffffffffULL

static int RectValid(const APPLE_AGX_GDI_RECT *r) {
  return r->Left < r->Right && r->Top < r->Bottom;
}

static int RectContains(const APPLE_AGX_GDI_RECT *outer,
                        const APPLE_AGX_GDI_RECT *inner) {
  return RectValid(inner) && inner->Left >= outer->Left &&
      inner->Top >= outer->Top && inner->Right <= outer->Right &&
      inner->Bottom <= outer->Bottom;
}

static int GeometryValid(const ADMISSION_PRESENT_BLT_COMMAND *c) {
  const ADMISSION_ALLOCATION_DESCRIPTION *s = &c->SourceDescription;
  const ADMISSION_ALLOCATION_DESCRIPTION *d = &c->DestinationDescription;
  return AdmissionAllocationDescriptionValid(s) &&
      AdmissionAllocationDescriptionValid(d) &&
      s->BytesPerPixel == 4u && d->BytesPerPixel == 4u &&
      s->Format == 21u && d->Format == 21u && c->ContextToken != 0ULL &&
      RectValid(&c->SourceRect) && RectValid(&c->DestinationRect) &&
      c->SourceRect.Right <= s->Width && c->SourceRect.Bottom <= s->Height &&
      c->DestinationRect.Right <= d->Width && c->DestinationRect.Bottom <= d->Height &&
      c->SourceRect.Right - c->SourceRect.Left ==
          c->DestinationRect.Right - c->DestinationRect.Left &&
      c->SourceRect.Bottom - c->SourceRect.Top ==
          c->DestinationRect.Bottom - c->DestinationRect.Top;
}

int AdmissionPresentLocationEncode(unsigned int Segment,
    unsigned long long Address, unsigned long long *Location) {
  if (!Location || (Segment != 1u && Segment != 2u) ||
      Address == 0ULL || Address > PRESENT_LOCATION_MASK)
    return 0;
  *Location = ((unsigned long long)Segment << 56u) | Address;
  return 1;
}

int AdmissionPresentLocationDecode(unsigned long long Location,
    unsigned int *Segment, unsigned long long *Address) {
  unsigned int segment = (unsigned int)(Location >> 56u);
  unsigned long long address = Location & PRESENT_LOCATION_MASK;
  if (!Segment || !Address || (segment != 1u && segment != 2u) || !address)
    return 0;
  *Segment = segment;
  *Address = address;
  return 1;
}

int AdmissionPresentBltEncode(const ADMISSION_PRESENT_BLT_INPUT *Input,
    void *Buffer, unsigned int Capacity, unsigned int *BytesUsed,
    unsigned int *NextRect) {
  ADMISSION_PRESENT_BLT_COMMAND c;
  unsigned int count, index;
  if (!Input || !Buffer || !BytesUsed || !NextRect || !Input->Rects ||
      !Input->RectCount || Input->RectCount > 0xffffffffu / sizeof(*Input->Rects) ||
      Input->MultipassOffset >= Input->RectCount ||
      !GeometryValid(&Input->Command) || Capacity < sizeof(c) + sizeof(*Input->Rects))
    return 0;
  for (index = 0; index < Input->RectCount; ++index)
    if (!RectContains(&Input->Command.DestinationRect, &Input->Rects[index]))
      return 0;
  if (Capacity > ADMISSION_PRESENT_BLT_DMA_MAX)
    Capacity = ADMISSION_PRESENT_BLT_DMA_MAX;
  count = (Capacity - (unsigned int)sizeof(c)) / sizeof(*Input->Rects);
  if (count > Input->RectCount - Input->MultipassOffset)
    count = Input->RectCount - Input->MultipassOffset;
  /* No packet may overwrite pixels needed by a later packet. Until a shared
   * operation snapshot exists, reject split shifted overlap before output. */
  if (Input->SameAllocation && Input->RectCount > count &&
      (Input->Command.SourceRect.Left != Input->Command.DestinationRect.Left ||
       Input->Command.SourceRect.Top != Input->Command.DestinationRect.Top) &&
      Input->Command.SourceRect.Left < Input->Command.DestinationRect.Right &&
      Input->Command.DestinationRect.Left < Input->Command.SourceRect.Right &&
      Input->Command.SourceRect.Top < Input->Command.DestinationRect.Bottom &&
      Input->Command.DestinationRect.Top < Input->Command.SourceRect.Bottom)
    return 0;
  c = Input->Command;
  c.Magic = ADMISSION_PRESENT_BLT_MAGIC;
  c.Version = Input->Command.Version == ADMISSION_PRESENT_BLT_GPUVA_VERSION ?
                  ADMISSION_PRESENT_BLT_GPUVA_VERSION :
                  ADMISSION_PRESENT_BLT_VERSION;
  c.RectCount = count;
  c.Bytes = (unsigned int)sizeof(c) + count * (unsigned int)sizeof(*Input->Rects);
  memcpy(Buffer, &c, sizeof(c));
  memcpy((unsigned char *)Buffer + sizeof(c),
          Input->Rects + Input->MultipassOffset, count * sizeof(*Input->Rects));
  *BytesUsed = c.Bytes;
  *NextRect = Input->MultipassOffset + count;
  return 1;
}

int AdmissionPresentBltValidate(const void *Buffer, unsigned int Bytes,
    int RequireResidency, ADMISSION_PRESENT_BLT_COMMAND *Command) {
  ADMISSION_PRESENT_BLT_COMMAND c;
  APPLE_AGX_GDI_RECT rect;
  unsigned int index, segment;
  unsigned long long address;
  if (!Buffer || !Command || Bytes < sizeof(c) || Bytes > ADMISSION_PRESENT_BLT_DMA_MAX)
    return 0;
  memcpy(&c, Buffer, sizeof(c));
  if (c.Magic != ADMISSION_PRESENT_BLT_MAGIC ||
      (c.Version != ADMISSION_PRESENT_BLT_VERSION &&
       c.Version != ADMISSION_PRESENT_BLT_GPUVA_VERSION) ||
      c.Bytes != Bytes || !c.RectCount ||
      c.RectCount > (Bytes - sizeof(c)) / sizeof(rect) ||
      Bytes != sizeof(c) + c.RectCount * sizeof(rect) || !GeometryValid(&c))
    return 0;
  if (c.Version == ADMISSION_PRESENT_BLT_GPUVA_VERSION) {
    if (c.SourceLocation < 0x4000ULL ||
        c.DestinationLocation < 0x4000ULL ||
        c.SourceLocation >= (1ULL << 39) ||
        c.DestinationLocation >= (1ULL << 39) ||
        c.SourceDescription.Size > (1ULL << 39) - c.SourceLocation ||
        c.DestinationDescription.Size > (1ULL << 39) - c.DestinationLocation)
      return 0;
  } else {
    if ((RequireResidency || c.SourceLocation) &&
        !AdmissionPresentLocationDecode(c.SourceLocation, &segment, &address))
      return 0;
    if ((RequireResidency || c.DestinationLocation) &&
        (!AdmissionPresentLocationDecode(c.DestinationLocation, &segment, &address) || segment != 2u))
      return 0;
  }
  for (index = 0; index < c.RectCount; ++index) {
    memcpy(&rect, (const unsigned char *)Buffer + sizeof(c) + index * sizeof(rect), sizeof(rect));
    if (!RectContains(&c.DestinationRect, &rect))
      return 0;
  }
  *Command = c;
  return 1;
}

int AdmissionPresentBltScratchBytes(const ADMISSION_PRESENT_BLT_COMMAND *Command,
    unsigned int *Bytes) {
  unsigned long long size;
  if (!Command || !Bytes || !GeometryValid(Command))
    return 0;
  size = (unsigned long long)(Command->SourceRect.Right - Command->SourceRect.Left) *
      (Command->SourceRect.Bottom - Command->SourceRect.Top) * 4ULL;
  if (!size || size > 0xffffffffULL)
    return 0;
  *Bytes = (unsigned int)size;
  return 1;
}

int AdmissionPresentBltExecute(const void *Buffer, unsigned int Bytes,
    ADMISSION_PRESENT_COPY_IO ReadSource, ADMISSION_PRESENT_COPY_IO WriteDestination,
    void *IoContext, void *Scratch, unsigned int ScratchBytes,
    unsigned long long *BytesCopied) {
  ADMISSION_PRESENT_BLT_COMMAND c;
  APPLE_AGX_GDI_RECT rect;
  unsigned int needed, row, width, index, row_bytes;
  unsigned long long source_offset, destination_offset, scratch_offset;
  if (!ReadSource || !WriteDestination || !Scratch || !BytesCopied ||
      !AdmissionPresentBltValidate(Buffer, Bytes, 1, &c) ||
      !AdmissionPresentBltScratchBytes(&c, &needed) || ScratchBytes < needed)
    return 0;
  *BytesCopied = 0;
  width = c.SourceRect.Right - c.SourceRect.Left;
  row_bytes = width * 4u;
  /* Snapshot before writing: screen-to-screen overlap and multiple clipped
   * rectangles cannot overwrite source pixels still required later. */
  for (row = 0; row < c.SourceRect.Bottom - c.SourceRect.Top; ++row) {
    source_offset = (unsigned long long)(c.SourceRect.Top + row) * c.SourceDescription.Pitch +
        (unsigned long long)c.SourceRect.Left * 4ULL;
    if (!ReadSource(IoContext, source_offset,
          (unsigned char *)Scratch + (unsigned long long)row * row_bytes, row_bytes))
      return 0;
  }
  for (index = 0; index < c.RectCount; ++index) {
    memcpy(&rect, (const unsigned char *)Buffer + sizeof(c) + index * sizeof(rect), sizeof(rect));
    for (row = rect.Top; row < rect.Bottom; ++row) {
      scratch_offset = ((unsigned long long)(row - c.DestinationRect.Top) * width +
          rect.Left - c.DestinationRect.Left) * 4ULL;
      destination_offset = (unsigned long long)row * c.DestinationDescription.Pitch +
          (unsigned long long)rect.Left * 4ULL;
      row_bytes = (rect.Right - rect.Left) * 4u;
      if (!WriteDestination(IoContext, destination_offset,
            (unsigned char *)Scratch + scratch_offset, row_bytes))
        return 0;
      *BytesCopied += row_bytes;
    }
  }
  return 1;
}

typedef struct {
  ADMISSION_PRESENT_GPUVA_TRANSLATE SourceTranslate;
  ADMISSION_PRESENT_GPUVA_TRANSLATE DestinationTranslate;
  ADMISSION_PRESENT_GPUVA_READ Read;
  ADMISSION_PRESENT_GPUVA_WRITE Write;
  void *Context;
  unsigned long long SourceVa, DestinationVa, SourceBytes, DestinationBytes;
} ADMISSION_PRESENT_GPUVA_COPY;

static int PresentGpuvaRead(void *Opaque, unsigned long long Offset,
    void *Bytes, unsigned int ByteCount) {
  ADMISSION_PRESENT_GPUVA_COPY *copy = Opaque;
  unsigned long long va, ipa;
  unsigned int chunk;
  if (Offset > copy->SourceBytes || ByteCount > copy->SourceBytes - Offset)
    return 0;
  while (ByteCount != 0u) {
    va = copy->SourceVa + Offset;
    chunk = 0x1000u - (unsigned int)(va & 0xfffULL);
    if (chunk > ByteCount) chunk = ByteCount;
    if (!copy->SourceTranslate(copy->Context, va, &ipa) ||
        !copy->Read(copy->Context, ipa, Bytes, chunk))
      return 0;
    Offset += chunk;
    Bytes = (unsigned char *)Bytes + chunk;
    ByteCount -= chunk;
  }
  return 1;
}

static int PresentGpuvaWrite(void *Opaque, unsigned long long Offset,
    void *Bytes, unsigned int ByteCount) {
  ADMISSION_PRESENT_GPUVA_COPY *copy = Opaque;
  unsigned long long va, ipa;
  unsigned int chunk;
  if (Offset > copy->DestinationBytes ||
      ByteCount > copy->DestinationBytes - Offset)
    return 0;
  while (ByteCount != 0u) {
    va = copy->DestinationVa + Offset;
    chunk = 0x4000u - (unsigned int)(va & 0x3fffULL);
    if (chunk > ByteCount) chunk = ByteCount;
    if (!copy->DestinationTranslate(copy->Context, va, &ipa) ||
        !copy->Write(copy->Context, ipa, Bytes, chunk, 1))
      return 0;
    Offset += chunk;
    Bytes = (unsigned char *)Bytes + chunk;
    ByteCount -= chunk;
  }
  return 1;
}

int AdmissionPresentBltExecuteGpuvaSeparate(const void *Buffer,
    unsigned int Bytes, ADMISSION_PRESENT_GPUVA_TRANSLATE SourceTranslate,
    ADMISSION_PRESENT_GPUVA_TRANSLATE DestinationTranslate,
    ADMISSION_PRESENT_GPUVA_READ Read,
    ADMISSION_PRESENT_GPUVA_WRITE Write, void *Context, void *Scratch,
    unsigned int ScratchBytes, unsigned long long *BytesCopied) {
  ADMISSION_PRESENT_BLT_COMMAND command;
  ADMISSION_PRESENT_GPUVA_COPY copy;
  unsigned long long offset, va, ipa;
  unsigned int chunk;
  if (!SourceTranslate || !DestinationTranslate || !Read || !Write ||
      !Context || !Scratch || !BytesCopied ||
      !AdmissionPresentBltValidate(Buffer, Bytes, 1, &command) ||
      command.Version != ADMISSION_PRESENT_BLT_GPUVA_VERSION)
    return 0;
  copy.SourceTranslate = SourceTranslate;
  copy.DestinationTranslate = DestinationTranslate;
  copy.Read = Read;
  copy.Write = Write;
  copy.Context = Context;
  copy.SourceVa = command.SourceLocation;
  copy.DestinationVa = command.DestinationLocation;
  copy.SourceBytes = command.SourceDescription.Size;
  copy.DestinationBytes = command.DestinationDescription.Size;
  for (offset = 0ULL; offset < copy.DestinationBytes; offset += chunk) {
    va = copy.DestinationVa + offset;
    chunk = 0x4000u - (unsigned int)(va & 0x3fffULL);
    if (chunk > copy.DestinationBytes - offset)
      chunk = (unsigned int)(copy.DestinationBytes - offset);
    if (!DestinationTranslate(Context, va, &ipa) ||
        !Write(Context, ipa, NULL, chunk, 0))
      return 0;
  }
  return AdmissionPresentBltExecute(Buffer, Bytes, PresentGpuvaRead,
      PresentGpuvaWrite, &copy, Scratch, ScratchBytes, BytesCopied);
}

int AdmissionPresentBltExecuteGpuva(const void *Buffer, unsigned int Bytes,
    ADMISSION_PRESENT_GPUVA_TRANSLATE Translate,
    ADMISSION_PRESENT_GPUVA_READ Read,
    ADMISSION_PRESENT_GPUVA_WRITE Write, void *Context, void *Scratch,
    unsigned int ScratchBytes, unsigned long long *BytesCopied) {
  return AdmissionPresentBltExecuteGpuvaSeparate(Buffer, Bytes,
      Translate, Translate, Read, Write, Context, Scratch, ScratchBytes,
      BytesCopied);
}
