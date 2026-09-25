#include "apple_agx_g4_submit.h"
#include <string.h>

typedef char agx4_header_size[(sizeof(APPLE_AGX_G4_PRIVATE_HEADER) == 24u) ? 1 : -1];
typedef char agx4_native_header_size[(sizeof(APPLE_AGX_G4_NATIVE_HEADER) == 8u) ? 1 : -1];
typedef char agx4_attachment_size[(sizeof(APPLE_AGX_G4_ATTACHMENT) == 24u) ? 1 : -1];

static int valid_va(unsigned long long va, unsigned long long bytes) {
  return va >= 0x10000ULL && va < (1ULL << 39) && bytes &&
         bytes <= (1ULL << 39) - va;
}

APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseSubmit(
    const void *private_data, unsigned int private_capacity,
    unsigned int umd_private_bytes, unsigned long long dma_va,
    unsigned int dma_bytes, APPLE_AGX_G4_ACCESS access, void *access_context,
    APPLE_AGX_G4_SUBMIT_VIEW *view) {
  APPLE_AGX_G4_PRIVATE_HEADER header;
  APPLE_AGX_G4_NATIVE_HEADER native_header;
  const unsigned char *bytes = (const unsigned char *)private_data;
  unsigned int position = 0u, index, attachments = 0u;
  const APPLE_AGX_G4_ATTACHMENT *attachment_base = 0;
  if (view != 0) memset(view, 0, sizeof(*view));
  if (!private_data || !view || !access ||
      private_capacity < sizeof(header) ||
      umd_private_bytes < sizeof(header) ||
      umd_private_bytes > private_capacity ||
      dma_bytes == 0u || dma_bytes > APPLE_AGX_G4_NATIVE_MAX_BYTES ||
      !valid_va(dma_va, dma_bytes)) return AppleAgxG4ParseInvalid;
  memcpy(&header, bytes, sizeof(header));
  if (header.Magic != APPLE_AGX_G4_PRIVATE_MAGIC ||
      header.Version != APPLE_AGX_G4_PRIVATE_VERSION ||
      header.HeaderBytes != sizeof(header) || header.Reserved != 0u ||
      header.CommandVa != dma_va || header.CommandBytes != dma_bytes ||
      umd_private_bytes != sizeof(header) + dma_bytes)
    return AppleAgxG4ParseInvalid;
  if (!access(access_context, dma_va, dma_bytes, 0))
    return AppleAgxG4ParseUnmapped;
  bytes += sizeof(header);
  if (dma_bytes < sizeof(native_header)) return AppleAgxG4ParseInvalid;
  memcpy(&native_header, bytes, sizeof(native_header));
  if (native_header.Type == APPLE_AGX_G4_FRAGMENT_ATTACHMENTS) {
    if (native_header.VdmBarrier != 0xffffu ||
        native_header.CdmBarrier != 0xffffu ||
        native_header.Size % sizeof(APPLE_AGX_G4_ATTACHMENT) != 0u ||
        native_header.Size > dma_bytes - sizeof(native_header))
      return AppleAgxG4ParseInvalid;
    attachments = native_header.Size / sizeof(APPLE_AGX_G4_ATTACHMENT);
    attachment_base = (const APPLE_AGX_G4_ATTACHMENT *)(bytes + sizeof(native_header));
    for (index = 0u; index < attachments; ++index) {
      APPLE_AGX_G4_ATTACHMENT attachment;
      memcpy(&attachment, (const unsigned char *)attachment_base +
              index * sizeof(attachment), sizeof(attachment));
      if (attachment.Pad || attachment.Flags ||
          attachment.Size > 0xffffffffULL ||
          !valid_va(attachment.Pointer, attachment.Size))
        return AppleAgxG4ParseInvalid;
      if (!access(access_context, attachment.Pointer,
                  (unsigned int)attachment.Size, 1))
        return AppleAgxG4ParseUnmapped;
    }
    position = sizeof(native_header) + native_header.Size;
    if (dma_bytes - position < sizeof(native_header))
      return AppleAgxG4ParseInvalid;
    memcpy(&native_header, bytes + position, sizeof(native_header));
  }
  if (native_header.Type != APPLE_AGX_G4_RENDER)
    return AppleAgxG4ParseUnsupported;
  if (native_header.Size != 240u || native_header.VdmBarrier != 0u ||
      native_header.CdmBarrier != 0u ||
      dma_bytes - position != sizeof(native_header) + native_header.Size)
    return AppleAgxG4ParseInvalid;
  view->Native = bytes;
  view->Render = bytes + position + sizeof(native_header);
  view->Attachments = attachment_base;
  view->CommandBytes = dma_bytes;
  view->RenderBytes = native_header.Size;
  view->AttachmentCount = attachments;
  view->CommandVa = dma_va;
  return AppleAgxG4ParseOk;
}
