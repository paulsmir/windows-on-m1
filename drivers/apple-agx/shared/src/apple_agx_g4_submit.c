#include "apple_agx_g4_submit.h"
#include <string.h>

typedef char agx4_header_size[(sizeof(APPLE_AGX_G4_PRIVATE_HEADER) == 24u) ? 1 : -1];
typedef char agx4_native_header_size[(sizeof(APPLE_AGX_G4_NATIVE_HEADER) == 8u) ? 1 : -1];
typedef char agx4_attachment_size[(sizeof(APPLE_AGX_G4_ATTACHMENT) == 24u) ? 1 : -1];
typedef char agx4_render_size[(sizeof(APPLE_AGX_G4_NATIVE_RENDER) == 240u) ? 1 : -1];
typedef char agx4_process_range_size[(sizeof(APPLE_AGX_G4_PROCESS_RANGE) == 16u) ? 1 : -1];
typedef char agx4_private_v2_size[(sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V2) == 168u) ? 1 : -1];

static int valid_va(unsigned long long va, unsigned long long bytes) {
  return va >= 0x10000ULL && va < (1ULL << 39) && bytes &&
         bytes <= (1ULL << 39) - va;
}

static APPLE_AGX_G4_PARSE_RESULT validate_render(
    const unsigned char *data, APPLE_AGX_G4_ACCESS access, void *context) {
  APPLE_AGX_G4_NATIVE_RENDER render;
  struct address { unsigned long long Va; int Write; } addresses[19];
  unsigned int index, count = 0u;
#define AGX4_ADDRESS(value, write) do { \
  addresses[count].Va = (value); addresses[count].Write = (write); ++count; \
} while (0)
  memcpy(&render, data, sizeof(render));
  if ((render.Flags & ~((1u << 0) | (1u << 1) | (1u << 2) |
                        (1u << 18))) != 0u ||
      !render.WidthPx || !render.HeightPx ||
      render.WidthPx > 16384u || render.HeightPx > 16384u ||
      !render.Layers || render.Layers > 2048u ||
      !((render.UtileWidthPx == 32u && render.UtileHeightPx == 32u) ||
        (render.UtileWidthPx == 32u && render.UtileHeightPx == 16u) ||
        (render.UtileWidthPx == 16u && render.UtileHeightPx == 16u)) ||
      (render.Samples != 1u && render.Samples != 2u &&
       render.Samples != 4u) || !render.SampleSizeBytes)
    return AppleAgxG4ParseInvalid;
  if (render.TimestampsVertex.StartHandle ||
      render.TimestampsVertex.EndHandle ||
      render.TimestampsFragment.StartHandle ||
      render.TimestampsFragment.EndHandle)
    return AppleAgxG4ParseUnsupported;
  if (render.TimestampsVertex.StartOffset ||
      render.TimestampsVertex.EndOffset ||
      render.TimestampsFragment.StartOffset ||
      render.TimestampsFragment.EndOffset)
    return AppleAgxG4ParseInvalid;
  AGX4_ADDRESS(render.VdmCtrlStreamBase, 0);
  AGX4_ADDRESS(render.VertexHelper.Binary, 0);
  AGX4_ADDRESS(render.VertexHelper.Data, 0);
  AGX4_ADDRESS(render.FragmentHelper.Binary, 0);
  AGX4_ADDRESS(render.FragmentHelper.Data, 0);
  AGX4_ADDRESS(render.IspScissorBase, 0);
  AGX4_ADDRESS(render.IspDbiasBase, 0);
  AGX4_ADDRESS(render.IspOclQryBase, 1);
  AGX4_ADDRESS(render.Depth.Base, 1);
  AGX4_ADDRESS(render.Depth.CompBase, 1);
  AGX4_ADDRESS(render.Stencil.Base, 1);
  AGX4_ADDRESS(render.Stencil.CompBase, 1);
  AGX4_ADDRESS(render.SamplerHeap, 0);
  AGX4_ADDRESS(render.Bg.Usc, 0);
  AGX4_ADDRESS(render.Eot.Usc, 0);
  AGX4_ADDRESS(render.PartialBg.Usc, 0);
  AGX4_ADDRESS(render.PartialEot.Usc, 0);
#undef AGX4_ADDRESS
  for (index = 0u; index < count; ++index) {
    if (!addresses[index].Va) {
      if (index == 0u) return AppleAgxG4ParseInvalid;
      continue;
    }
    if (!valid_va(addresses[index].Va, 1u))
      return AppleAgxG4ParseInvalid;
    if (!access(context, addresses[index].Va, 1u,
                addresses[index].Write))
      return AppleAgxG4ParseUnmapped;
  }
  return AppleAgxG4ParseOk;
}

APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseSubmit(
    const void *private_data, unsigned int private_capacity,
    unsigned int umd_private_bytes, unsigned long long dma_va,
    unsigned int dma_bytes, APPLE_AGX_G4_ACCESS access, void *access_context,
    APPLE_AGX_G4_SUBMIT_VIEW *view) {
  APPLE_AGX_G4_PRIVATE_HEADER header;
  APPLE_AGX_G4_PRIVATE_HEADER_V2 header_v2;
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
      (header.Version != APPLE_AGX_G4_PRIVATE_VERSION &&
       header.Version != APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA) ||
      header.HeaderBytes != (header.Version ==
          APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA ?
          sizeof(header_v2) : sizeof(header)) ||
      header.Reserved != 0u ||
      header.CommandVa != dma_va || header.CommandBytes != dma_bytes ||
      umd_private_bytes != (unsigned int)header.HeaderBytes + dma_bytes)
    return AppleAgxG4ParseInvalid;
  if (header.Version == APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA) {
    if (umd_private_bytes < sizeof(header_v2))
      return AppleAgxG4ParseInvalid;
    memcpy(&header_v2, bytes, sizeof(header_v2));
    for (index = 0u; index < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++index) {
      const APPLE_AGX_G4_PROCESS_RANGE *range = &header_v2.Process[index];
      if (range->Reserved || !range->Bytes ||
          (range->Va & 0xffffULL) || (range->Bytes & 0xffffu) ||
          !valid_va(range->Va, range->Bytes))
        return AppleAgxG4ParseInvalid;
      if (range->Va < dma_va + dma_bytes &&
          dma_va < range->Va + range->Bytes)
        return AppleAgxG4ParseInvalid;
      for (unsigned int earlier = 0u; earlier < index; ++earlier) {
        const APPLE_AGX_G4_PROCESS_RANGE *other =
            &header_v2.Process[earlier];
        if (range->Va < other->Va + other->Bytes &&
            other->Va < range->Va + range->Bytes)
          return AppleAgxG4ParseInvalid;
      }
      if (!access(access_context, range->Va, range->Bytes, 1))
        return AppleAgxG4ParseUnmapped;
    }
  }
  if (!access(access_context, dma_va, dma_bytes, 0))
    return AppleAgxG4ParseUnmapped;
  bytes += header.HeaderBytes;
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
  if (native_header.Size != sizeof(APPLE_AGX_G4_NATIVE_RENDER) ||
      native_header.VdmBarrier != 0u ||
      native_header.CdmBarrier != 0u ||
      dma_bytes - position != sizeof(native_header) + native_header.Size)
    return AppleAgxG4ParseInvalid;
  {
    APPLE_AGX_G4_PARSE_RESULT render_result = validate_render(
        bytes + position + sizeof(native_header), access, access_context);
    if (render_result != AppleAgxG4ParseOk) return render_result;
  }
  if (header.Version == APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA) {
    APPLE_AGX_G4_NATIVE_RENDER render;
    unsigned long long tiles_x, tiles_y, blocks, minimum_heap;
    memcpy(&render, bytes + position + sizeof(native_header), sizeof(render));
    /* Asahi buffer.rs uses 128 KiB blocks and at least eight blocks per
     * scene; render.rs derives the minimum from the 32x32 tile grid.  This
     * checks a capacity contract, not one particular captured frame size. */
    tiles_x = ((unsigned long long)render.WidthPx + 31ULL) / 32ULL;
    tiles_y = ((unsigned long long)render.HeightPx + 31ULL) / 32ULL;
    blocks = ((tiles_x * tiles_y + 127ULL) / 128ULL + 7ULL) & ~7ULL;
    minimum_heap = blocks * 0x20000ULL;
    if (header_v2.Process[2].Bytes < minimum_heap)
      return AppleAgxG4ParseInvalid;
  }
  view->Native = bytes;
  view->Render = bytes + position + sizeof(native_header);
  view->Attachments = attachment_base;
  view->CommandBytes = dma_bytes;
  view->RenderBytes = native_header.Size;
  view->AttachmentCount = attachments;
  view->CommandVa = dma_va;
  if (header.Version == APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA)
    memcpy(view->Process, header_v2.Process, sizeof(view->Process));
  return AppleAgxG4ParseOk;
}
