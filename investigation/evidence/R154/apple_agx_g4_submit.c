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

/* The firmware's 32-bit USC fields are offsets from the process USC
 * execution base. Low six bits carry pipeline flags, not address bits. */
static unsigned long long usc_va(unsigned int packed) {
  return packed ? APPLE_AGX_G4_USC_EXECUTION_BASE + (packed & ~63u) : 0ULL;
}

typedef struct {
  APPLE_AGX_G4_ACCESS Old;
  APPLE_AGX_G4_ACCESS_EX Typed;
  void *Context;
  APPLE_AGX_G4_FAILURE *Failure;
  unsigned int Ordinal;
} AGX4_ACCESS_STATE;

static int check_access(AGX4_ACCESS_STATE *state, unsigned long long va,
    unsigned int bytes, int write, APPLE_AGX_G4_ACCESS_KIND kind) {
  unsigned int ordinal = state->Ordinal++;
  int accepted = state->Typed ? state->Typed(state->Context, va, bytes,
      write, kind, ordinal) : state->Old(state->Context, va, bytes, write);
  if (!accepted && state->Failure) {
    state->Failure->Subsite = AppleAgxG4FailureAccess;
    state->Failure->Kind = kind;
    state->Failure->Ordinal = ordinal;
    state->Failure->Va = va;
    state->Failure->Bytes = bytes;
    state->Failure->Write = write != 0;
  }
  return accepted;
}

static APPLE_AGX_G4_PARSE_RESULT validate_render(
    const unsigned char *data, AGX4_ACCESS_STATE *access) {
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
  AGX4_ADDRESS(usc_va(render.VertexHelper.Binary), 0);
  AGX4_ADDRESS(render.VertexHelper.Data, 0);
  AGX4_ADDRESS(usc_va(render.FragmentHelper.Binary), 0);
  AGX4_ADDRESS(render.FragmentHelper.Data, 0);
  AGX4_ADDRESS(render.IspScissorBase, 0);
  AGX4_ADDRESS(render.IspDbiasBase, 0);
  AGX4_ADDRESS(render.IspOclQryBase, 1);
  AGX4_ADDRESS(render.Depth.Base, 1);
  AGX4_ADDRESS(render.Depth.CompBase, 1);
  AGX4_ADDRESS(render.Stencil.Base, 1);
  AGX4_ADDRESS(render.Stencil.CompBase, 1);
  AGX4_ADDRESS(render.SamplerHeap, 0);
  AGX4_ADDRESS(usc_va(render.Bg.Usc), 0);
  AGX4_ADDRESS(usc_va(render.Eot.Usc), 0);
  AGX4_ADDRESS(usc_va(render.PartialBg.Usc), 0);
  AGX4_ADDRESS(usc_va(render.PartialEot.Usc), 0);
#undef AGX4_ADDRESS
  for (index = 0u; index < count; ++index) {
    if (!addresses[index].Va) {
      if (index == 0u) return AppleAgxG4ParseInvalid;
      continue;
    }
    if (!valid_va(addresses[index].Va, 1u))
      return AppleAgxG4ParseInvalid;
    if (!check_access(access, addresses[index].Va, 1u,
                addresses[index].Write, AppleAgxG4AccessRender))
      return AppleAgxG4ParseUnmapped;
  }
  return AppleAgxG4ParseOk;
}

static APPLE_AGX_G4_PARSE_RESULT parse_submit(
    const void *private_data, unsigned int private_capacity,
    unsigned int umd_private_bytes, unsigned long long dma_va,
    unsigned int dma_bytes, AGX4_ACCESS_STATE *access,
    APPLE_AGX_G4_SUBMIT_VIEW *view) {
  APPLE_AGX_G4_PRIVATE_HEADER header;
  APPLE_AGX_G4_PRIVATE_HEADER_V2 header_v2 = {0};
  APPLE_AGX_G4_PRIVATE_HEADER_V3 header_v3 = {0};
  APPLE_AGX_G4_NATIVE_HEADER native_header;
  const unsigned char *bytes = (const unsigned char *)private_data;
  unsigned int position = 0u, index, attachments = 0u;
  const APPLE_AGX_G4_ATTACHMENT *attachment_base = 0;
  if (view != 0) memset(view, 0, sizeof(*view));
  if (access && access->Failure) memset(access->Failure, 0, sizeof(*access->Failure));
  if (!private_data || !view || !access || (!access->Old && !access->Typed) ||
      private_capacity < sizeof(header) ||
      umd_private_bytes < sizeof(header) ||
      umd_private_bytes > private_capacity ||
      dma_bytes == 0u || dma_bytes > APPLE_AGX_G4_NATIVE_MAX_BYTES ||
      !valid_va(dma_va, dma_bytes)) return AppleAgxG4ParseInvalid;
  memcpy(&header, bytes, sizeof(header));
  if (header.Magic != APPLE_AGX_G4_PRIVATE_MAGIC ||
      (header.Version != APPLE_AGX_G4_PRIVATE_VERSION &&
       header.Version != APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA &&
       header.Version != APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA) ||
      header.HeaderBytes != (header.Version == APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA ?
          sizeof(header_v3) : header.Version ==
          APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA ?
          sizeof(header_v2) : sizeof(header)) ||
      (header.Version == APPLE_AGX_G4_PRIVATE_VERSION &&
       header.Reserved != 0u) ||
      header.CommandVa != dma_va || header.CommandBytes != dma_bytes ||
      umd_private_bytes != (unsigned int)header.HeaderBytes + dma_bytes)
    return AppleAgxG4ParseInvalid;
  if (header.Version >= APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA) {
    if (header.Reserved != APPLE_AGX_G4_COLOR_BGRA8)
      return AppleAgxG4ParseUnsupported;
    if (umd_private_bytes < sizeof(header_v2))
      return AppleAgxG4ParseInvalid;
    memcpy(&header_v2, bytes, sizeof(header_v2));
    if (header.Version == APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA) {
      if (umd_private_bytes < sizeof(header_v3)) return AppleAgxG4ParseInvalid;
      memcpy(&header_v3,bytes,sizeof(header_v3));
      if (!header_v3.Lease.ManagerId || !header_v3.Lease.ManagerGeneration ||
          !header_v3.Lease.SceneId || !header_v3.Lease.SceneGeneration)
        return AppleAgxG4ParseInvalid;
    }
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
      if (!check_access(access, range->Va, range->Bytes, 1,
              AppleAgxG4AccessProcess))
        return AppleAgxG4ParseUnmapped;
    }
  }
  if (!check_access(access, dma_va, dma_bytes, 0,
          AppleAgxG4AccessCpuEnvelope))
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
      if (!check_access(access, attachment.Pointer,
                  (unsigned int)attachment.Size, 1, AppleAgxG4AccessAttachment))
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
        bytes + position + sizeof(native_header), access);
    if (render_result != AppleAgxG4ParseOk) return render_result;
  }
  if (header.Version >= APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA) {
    APPLE_AGX_G4_NATIVE_RENDER render;
    unsigned int required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
    memcpy(&render, bytes + position + sizeof(native_header), sizeof(render));
    if (!AppleAgxG4ProcessRequiredBytes(&render, required))
      return AppleAgxG4ParseInvalid;
    for (index = 0u; index < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++index)
      if (header_v2.Process[index].Bytes < required[index])
        return AppleAgxG4ParseInvalid;
  }
  view->Lease = header_v3.Lease;
  view->Native = bytes;
  view->Render = bytes + position + sizeof(native_header);
  view->Attachments = attachment_base;
  view->CommandBytes = dma_bytes;
  view->RenderBytes = native_header.Size;
  view->AttachmentCount = attachments;
  view->CommandVa = dma_va;
  view->ColorFormat = header.Version >=
      APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA ? header.Reserved : 0u;
  if (header.Version >= APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA)
    memcpy(view->Process, header_v2.Process, sizeof(view->Process));
  return AppleAgxG4ParseOk;
}

APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseSubmit(
    const void *private_data, unsigned int private_capacity,
    unsigned int umd_private_bytes, unsigned long long dma_va,
    unsigned int dma_bytes, APPLE_AGX_G4_ACCESS access, void *context,
    APPLE_AGX_G4_SUBMIT_VIEW *view) {
  AGX4_ACCESS_STATE state = {0};
  state.Old = access;
  state.Context = context;
  return parse_submit(private_data, private_capacity, umd_private_bytes,
      dma_va, dma_bytes, &state, view);
}

APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseSubmitEx(
    const void *private_data, unsigned int private_capacity,
    unsigned int umd_private_bytes, unsigned long long dma_va,
    unsigned int dma_bytes, APPLE_AGX_G4_ACCESS_EX access, void *context,
    APPLE_AGX_G4_SUBMIT_VIEW *view, APPLE_AGX_G4_FAILURE *failure) {
  AGX4_ACCESS_STATE state = {0};
  state.Typed = access;
  state.Context = context;
  state.Failure = failure;
  return parse_submit(private_data, private_capacity, umd_private_bytes,
      dma_va, dma_bytes, &state, view);
}
