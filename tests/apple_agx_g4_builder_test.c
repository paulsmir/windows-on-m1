#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_g4_builder.h"
#include "apple_agx_render_template_rebase.h"
#include "apple_agx_render_template_vm_slot.h"
#include "apple_agx_exp208_adapter.h"

static unsigned long long get64(const unsigned char *data) {
  unsigned long long value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= (unsigned long long)data[i] << (8u * i);
  return value;
}

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V2 Header;
  APPLE_AGX_G4_NATIVE_HEADER AttachCommand;
  APPLE_AGX_G4_ATTACHMENT Attachment;
  APPLE_AGX_G4_NATIVE_HEADER RenderCommand;
  APPLE_AGX_G4_NATIVE_RENDER Render;
} G4_PACKET;

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V2 Header;
  APPLE_AGX_G4_NATIVE_HEADER AttachCommand;
  APPLE_AGX_G4_ATTACHMENT Attachment[2];
  APPLE_AGX_G4_NATIVE_HEADER RenderCommand;
  APPLE_AGX_G4_NATIVE_RENDER Render;
} G4_PACKET_ZLS;

typedef struct {
  const G4_PACKET *Packet;
  unsigned Calls;
} ACCESS_CONTEXT;

static int graph_access(void *opaque, unsigned long long va,
                        unsigned int bytes, int write) {
  ACCESS_CONTEXT *context = opaque;
  const G4_PACKET *packet = context->Packet;
  ++context->Calls;
  if (!write && va == packet->Header.Base.CommandVa &&
      bytes == packet->Header.Base.CommandBytes) return 1;
  for (unsigned i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i)
    if (write && va == packet->Header.Process[i].Va &&
        bytes == packet->Header.Process[i].Bytes) return 1;
  if (write && va == packet->Attachment.Pointer &&
      bytes == packet->Attachment.Size) return 1;
  if (!write && bytes == 1u) {
    const unsigned long long native[] = {
      packet->Render.VdmCtrlStreamBase, packet->Render.IspScissorBase,
      packet->Render.IspDbiasBase, packet->Render.SamplerHeap,
      packet->Render.FragmentHelper.Data, 0x1100060000ULL,
      0x1100020000ULL, 0x1100030000ULL,
      0x1100040000ULL, 0x1100050000ULL
    };
    for (unsigned i = 0; i < sizeof(native) / sizeof(native[0]); ++i)
      if (va == native[i]) return 1;
  }
  return 0;
}

/* The depth attachment of the EXP1150 case: its range and its base. */
static APPLE_AGX_G4_ATTACHMENT zls_depth;
static int graph_access_zls(void *opaque, unsigned long long va,
                            unsigned int bytes, int write) {
  if (write && zls_depth.Pointer == 0x34000000ULL &&
      ((va == zls_depth.Pointer && bytes == zls_depth.Size) ||
       (va == zls_depth.Pointer && bytes == 1u)))
    return 1;
  return graph_access(opaque, va, bytes, write);
}

int main(void) {
  APPLE_AGX_G4_NATIVE_RENDER render = {0};
  APPLE_AGX_G4_PROCESS_RANGE ranges[APPLE_AGX_G4_PROCESS_RANGE_COUNT] = {{0}};
  APPLE_AGX_EXP208_RELOCATION_OBJECT objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT] = {{0}};
  unsigned char dummy[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT] = {0};
  APPLE_AGX_U32 required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  unsigned i;
  render.WidthPx = 1280;
  render.HeightPx = 720;
  render.Layers = 1;
  render.UtileWidthPx = render.UtileHeightPx = 32;
  render.Samples = 1;
  render.SampleSizeBytes = 8;
  render.Flags = 1u << 2; /* Asahi no vertex clustering. */
  render.VdmCtrlStreamBase = 0x30000000ULL;
  render.IspScissorBase = 0x31000000ULL;
  render.IspDbiasBase = 0x32000000ULL;
  render.SamplerHeap = 0x34000000ULL;
  render.PppMultisampleCtrl = 0x88ULL;
  render.PppCtrl = 0x202u;
  /* Real scene geometry must replace the 16x16 recorded template. */
  {
    float merge_x = 1.732051f / render.WidthPx;
    float merge_y = 1.732051f / render.HeightPx;
    memcpy(&render.IspMergeUpperX, &merge_x, sizeof(merge_x));
    memcpy(&render.IspMergeUpperY, &merge_y, sizeof(merge_y));
  }
  render.Bg.Usc = 0x20004u;
  render.Bg.ResourceSpec = 0x123u;
  render.Eot.Usc = 0x30004u;
  render.Eot.ResourceSpec = 0x456u;
  render.PartialBg.Usc = 0x40004u;
  render.PartialEot.Usc = 0x50004u;
  render.FragmentHelper.Binary = 0x60004u;
  render.FragmentHelper.Data = 0x35000000ULL;
  render.FragmentHelper.Cfg = 0x1234u;
  assert(AppleAgxG4ProcessRequiredBytes(&render, required));
  for (i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    ranges[i].Va = 0x10000000ULL + (unsigned long long)i * 0x1000000ULL;
    ranges[i].Bytes = required[i];
    objects[i].Data = &dummy[i];
  }
  for (i = 0; i < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT; ++i) {
    objects[i].Data = &dummy[i];
    objects[i].Size = 1;
  }
  assert(AppleAgxG4BindProcessObjects(&render, ranges, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  assert(objects[41].GpuVa == ranges[0].Va);
  assert(objects[42].GpuVa == ranges[1].Va);
  assert(objects[43].GpuVa == ranges[2].Va);
  assert(objects[58].GpuVa == ranges[2].Va + 15ULL * 0x20000ULL);
  assert(objects[59].GpuVa == ranges[8].Va);
  assert(objects[60].GpuVa == ranges[7].Va);
  assert(objects[61].GpuVa == ranges[7].Va + 9ULL * 0x540ULL);
  assert(objects[62].GpuVa == ranges[7].Va + 9ULL * (0x540ULL + 0x280ULL));
  assert(objects[64].GpuVa == ranges[6].Va);
  assert(objects[65].GpuVa == ranges[4].Va);
  assert(objects[66].GpuVa == ranges[5].Va);
  assert(objects[72].GpuVa == ranges[3].Va);
  /* The sampler heap comes from Mesa once the render is bound. */
  assert(objects[63].GpuVa == 0ULL);
  {
    unsigned char *arena = malloc(AppleAgxRenderTemplateBytes());
    APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
    assert(arena);
    assert(AppleAgxRenderTemplateMaterialize(
        arena, AppleAgxRenderTemplateBytes(), &roots));
    assert(AppleAgxRenderTemplateBuildRelocationObjectsRebased(
        arena, AppleAgxRenderTemplateBytes(), 0x800000000ULL,
        0x1503800000ULL, 0x1503800000ULL,
        AppleAgxRenderTemplateBytes(), objects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &roots));
    {
      G4_PACKET packet = {0};
      ACCESS_CONTEXT graph = {0};
      APPLE_AGX_G4_SUBMIT_VIEW view = {0};
      packet.Header.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
      packet.Header.Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA;
      packet.Header.Base.HeaderBytes = sizeof(packet.Header);
      packet.Header.Base.CommandBytes = sizeof(packet) - sizeof(packet.Header);
      packet.Header.Base.CommandVa = 0x20000ULL;
      packet.Header.Base.Reserved = APPLE_AGX_G4_COLOR_BGRA8;
      memcpy(packet.Header.Process, ranges, sizeof(ranges));
      packet.AttachCommand.Type = APPLE_AGX_G4_FRAGMENT_ATTACHMENTS;
      packet.AttachCommand.Size = sizeof(packet.Attachment);
      packet.AttachCommand.VdmBarrier = 0xffffu;
      packet.AttachCommand.CdmBarrier = 0xffffu;
      packet.Attachment.Pointer = 0x33000000ULL;
      packet.Attachment.Size = 1280ULL * 720ULL * 4ULL;
      packet.RenderCommand.Type = APPLE_AGX_G4_RENDER;
      packet.RenderCommand.Size = sizeof(render);
      packet.Render = render;
      graph.Packet = &packet;
      assert(AppleAgxG4ParseSubmit(&packet, sizeof(packet), sizeof(packet),
          packet.Header.Base.CommandVa, packet.Header.Base.CommandBytes,
          graph_access, &graph, &view) == AppleAgxG4ParseOk);
      assert(graph.Calls >= 15u);
      /* EXP1150: a GL window job lists colour then depth (Mesa
       * append_attachments); the KMD rejected any count but one. Parse
       * proves both attachments; bind uses attachment 0. */
      {
        G4_PACKET_ZLS zls = {0};
        G4_PACKET shadow = packet;
        ACCESS_CONTEXT zgraph = {0};
        APPLE_AGX_G4_SUBMIT_VIEW zview = {0};
        zls.Header = packet.Header;
        zls.Header.Base.CommandBytes = sizeof(zls) - sizeof(zls.Header);
        zls.AttachCommand = packet.AttachCommand;
        zls.AttachCommand.Size = sizeof(zls.Attachment);
        zls.Attachment[0] = packet.Attachment;
        zls.Attachment[1].Pointer = 0x34000000ULL;
        zls.Attachment[1].Size = 1280ULL * 720ULL * 4ULL;
        zls.RenderCommand = packet.RenderCommand;
        zls.Render = packet.Render;
        zls.Render.Depth.Base = 0x34000000ULL;
        zls.Render.ZlsCtrl = 0x80080ULL;
        zls.Render.IspZlsPixels = (1280u - 1u) | ((720u - 1u) << 15);
        shadow.Header = zls.Header;
        shadow.Render = zls.Render;
        zgraph.Packet = &shadow;
        zls_depth = zls.Attachment[1];
        assert(AppleAgxG4ParseSubmit(&zls, sizeof(zls), sizeof(zls),
            zls.Header.Base.CommandVa, zls.Header.Base.CommandBytes,
            graph_access_zls, &zgraph, &zview) == AppleAgxG4ParseOk);
        assert(zview.AttachmentCount == 2u);
        assert(AppleAgxG4BindNativeObjects(&zview, objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        assert(objects[40].GpuVa == zls.Attachment[0].Pointer);
        zview.AttachmentCount = APPLE_AGX_G4_MAX_ATTACHMENTS + 1u;
        assert(!AppleAgxG4BindNativeObjects(&zview, objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        /* An unmapped depth range is refused at parse time. */
        zls_depth.Pointer = 0x37000000ULL;
        memset(&zview, 0, sizeof(zview));
        assert(AppleAgxG4ParseSubmit(&zls, sizeof(zls), sizeof(zls),
            zls.Header.Base.CommandVa, zls.Header.Base.CommandBytes,
            graph_access_zls, &zgraph, &zview) == AppleAgxG4ParseUnmapped);
      }
      packet.Render.Samples = 4u;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Render.Samples = 1u;
      packet.Render.SampleSizeBytes = 3u;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Render.SampleSizeBytes = 8u;
      packet.Render.IspOclQryBase = 0x36000000ULL;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Render.IspOclQryBase = 0ULL;
      /* CS 1.6 ICD: a GL window always carries depth. A consistent ZLS
       * description (base -> control and ZLS pixels; compression only with
       * a base; strides only with a base) binds; anything else refuses. */
      packet.Render.Depth.Base = 0x34000000ULL;
      packet.Render.Depth.Stride = 0x1400u;
      packet.Render.ZlsCtrl = 0x80000ULL | 0x80ULL;
      packet.Render.IspZlsPixels = (1280u - 1u) | ((720u - 1u) << 15);
      assert(AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Render.Stencil.Base = 0x35000000ULL;
      assert(AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Render.ZlsCtrl = 0ULL;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Render.ZlsCtrl = 0x80080ULL;
      packet.Render.IspZlsPixels = 0u;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Render.IspZlsPixels = (1280u - 1u) | ((720u - 1u) << 15);
      packet.Render.Depth.Base = 0ULL;
      packet.Render.Depth.CompBase = 0x36000000ULL;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Render.Depth.CompBase = 0ULL;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)); /* stride w/o base */
      packet.Render.Depth.Stride = 0u;
      packet.Render.Stencil.Base = 0ULL;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)); /* control w/o base */
      packet.Render.ZlsCtrl = 0ULL;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)); /* pixels w/o base */
      packet.Render.IspZlsPixels = 0u;
      packet.Attachment.Size = 1024u;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Attachment.Size = 1280ULL * 720ULL * 4ULL;
      /* EXP1035: R8/A8 upload blits render 1-byte targets; the binding
       * minimum follows the colour class carried in the private header. */
      {
        G4_PACKET narrow = packet;
        ACCESS_CONTEXT narrow_graph = {0};
        APPLE_AGX_G4_SUBMIT_VIEW narrow_view = {0};
        narrow.Header.Base.Reserved = APPLE_AGX_G4_COLOR_1BYTE;
        narrow.Attachment.Size = 1280ULL * 720ULL;
        narrow_graph.Packet = &narrow;
        assert(AppleAgxG4ParseSubmit(&narrow, sizeof(narrow), sizeof(narrow),
            narrow.Header.Base.CommandVa, narrow.Header.Base.CommandBytes,
            graph_access, &narrow_graph, &narrow_view) == AppleAgxG4ParseOk);
        assert(narrow_view.ColorFormat == APPLE_AGX_G4_COLOR_1BYTE);
        assert(AppleAgxG4BindNativeObjects(&narrow_view, objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        narrow_view.ColorFormat = APPLE_AGX_G4_COLOR_2BYTE;
        assert(!AppleAgxG4BindNativeObjects(&narrow_view, objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        narrow.Attachment.Size = 1280ULL * 720ULL * 2ULL;
        assert(AppleAgxG4BindNativeObjects(&narrow_view, objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        narrow_view.ColorFormat = APPLE_AGX_G4_COLOR_BGRA8;
        assert(!AppleAgxG4BindNativeObjects(&narrow_view, objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        narrow_view.ColorFormat = 7u;
        narrow.Attachment.Size = 1280ULL * 720ULL * 4ULL;
        assert(!AppleAgxG4BindNativeObjects(&narrow_view, objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        narrow.Header.Base.Reserved = 7u;
        memset(&narrow_graph, 0, sizeof(narrow_graph));
        narrow_graph.Packet = &narrow;
        assert(AppleAgxG4ParseSubmit(&narrow, sizeof(narrow), sizeof(narrow),
            narrow.Header.Base.CommandVa, narrow.Header.Base.CommandBytes,
            graph_access, &narrow_graph, &narrow_view) ==
            AppleAgxG4ParseUnsupported);
      }
      assert(AppleAgxG4BuildTa3d(&view, arena,
          AppleAgxRenderTemplateBytes(), 1u, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      assert(get64(objects[1].Data + 0x24u) ==
          ((32u * 16u) | ((unsigned long long)(32u * 4u) << 32)));
      assert(get64(objects[1].Data + 0x2cu) ==
          (32u | ((unsigned long long)32u << 32)));
      assert(get64(objects[20].Data) ==
          (32u | ((unsigned long long)32u << 32)));
      assert(objects[37].GpuVa == render.VdmCtrlStreamBase);
      assert(objects[38].GpuVa == render.IspScissorBase);
      assert(objects[39].GpuVa == render.IspDbiasBase);
      assert(objects[40].GpuVa == packet.Attachment.Pointer);
      assert(objects[63].GpuVa == render.SamplerHeap);
      /* Dynamic TVB (Asahi buffer.rs ensure_blocks): the KMD backs the first
       * HeapBlocks of the 32-block window and the buffer manager, InitBM and
       * BlockControl describe exactly those blocks. A 1280x720 render needs
       * min_tvb_blocks = align(ceil(40 * 23 / 128), 8) = 8. */
      {
        static const unsigned int bad[] = {7u, 33u};
        unsigned int i;
        for (i = 0u; i < 2u; ++i) {
          assert(AppleAgxRenderTemplateMaterialize(
              arena, AppleAgxRenderTemplateBytes(), &roots));
          assert(AppleAgxRenderTemplateBuildRelocationObjectsRebased(
              arena, AppleAgxRenderTemplateBytes(), 0x800000000ULL,
              0x1503800000ULL, 0x1503800000ULL,
              AppleAgxRenderTemplateBytes(), objects,
              APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &roots));
          view.HeapBlocks = bad[i];
          assert(!AppleAgxG4BuildTa3d(&view, arena,
              AppleAgxRenderTemplateBytes(), 1u, objects,
              APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        }
        assert(AppleAgxRenderTemplateMaterialize(
            arena, AppleAgxRenderTemplateBytes(), &roots));
        assert(AppleAgxRenderTemplateBuildRelocationObjectsRebased(
            arena, AppleAgxRenderTemplateBytes(), 0x800000000ULL,
            0x1503800000ULL, 0x1503800000ULL,
            AppleAgxRenderTemplateBytes(), objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &roots));
        view.HeapBlocks = 8u;
        assert(AppleAgxG4BuildTa3d(&view, arena,
            AppleAgxRenderTemplateBytes(), 1u, objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
        assert(get64(objects[1].Data + 0x24u) ==
            ((8u * 16u) | ((unsigned long long)(8u * 4u) << 32)));
        assert(get64(objects[1].Data + 0x2cu) ==
            (8u | ((unsigned long long)8u << 32)));
        assert((get64(objects[1].Data + 0x48u) & 0xffffffffULL) == 8u * 4u - 1u);
        assert(get64(objects[1].Data + 0x84u) ==
            ((8u * 4u) | ((unsigned long long)(8u * 4u) << 32)));
        assert((get64(objects[16].Data + 0x10u) & 0xffffffffULL) == 8u);
        assert(get64(objects[20].Data) ==
            (8u | ((unsigned long long)8u << 32)));
        /* The page list and blocks keep the full reserved window. */
        assert(objects[43].GpuVa == ranges[2].Va);
        view.HeapBlocks = 0u;
      }
    }
    assert(get64(objects[13].Data + 24u) == ranges[3].Va);
    assert(get64(objects[18].Data + 2220u) == render.SamplerHeap);
    assert(get64(objects[19].Data + 1352u) == render.SamplerHeap);
    assert(get64(objects[19].Data + 80u) == ranges[4].Va);
    assert(get64(objects[19].Data + 208u) == render.VdmCtrlStreamBase);
    /* Source-backed G13/V13_5 RunFragment header (Asahi fragment.rs and
     * m1n1 WorkCommand3D): unknown U64 at 0x60, merge pair at 0x68,
     * unknown U64 at 0x70, tile_count at 0x78. The old writes were eight
     * bytes early, corrupting unknown fields and retaining 16x16 scalars. */
    assert(get64(objects[18].Data + 0x68u) ==
        ((unsigned long long)render.IspMergeUpperX |
         ((unsigned long long)render.IspMergeUpperY << 32)));
    assert(get64(objects[18].Data + 0x78u) == 40ULL * 23ULL);
    assert(get64(objects[18].Data + 0x60u) == 0ULL);
    assert(get64(objects[18].Data + 0x70u) == 0ULL);
    /* Asahi JobParameters2 / m1n1 Start3DStruct1 ISP_MTILE_SIZE.
     * 1280x720 uses 12x8 utiles per macro tile, not template4x4.
     * The unknown U64 at0x3e0 must not receive this packed pair. */
    assert((get64(objects[18].Data + 0x3e8u) & 0xffffffffULL) ==
        (8ULL | (12ULL << 16)));
    assert(get64(objects[18].Data + 0x3e0u) == 0ULL);
    {
      APPLE_AGX_G4_NATIVE_RENDER full = render;
      full.WidthPx = 2560;
      full.HeightPx = 1600;
      assert(AppleAgxG4PatchRenderScalars(&full, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      assert((get64(objects[18].Data + 0x3e8u) & 0xffffffffULL) ==
          (16ULL | (20ULL << 16)));
      assert(get64(objects[18].Data + 0x3e0u) == 0ULL);
      assert(AppleAgxG4PatchRenderScalars(&render, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
    }
    /* Without ZLS the depth/stencil fields keep the template's bytes (the
     * validated colour-only path is unchanged). */
    {
      static const unsigned zls_offsets[] = {
        0xc8u, 0xd8u, 0xe0u, 0xe8u, 0xf0u, 0xf8u, 0x100u, 0x108u, 0x110u,
        0x118u, 0x120u, 0x128u, 0x130u, 0x138u, 0x140u, 0x148u, 0x150u,
        0x158u, 0x650u, 0x660u, 0x668u, 0x670u, 0x678u, 0x680u, 0x688u,
        0x690u, 0x698u, 0x6a0u, 0x6a8u, 0x6b0u, 0x6b8u, 0x768u};
      unsigned long long before[sizeof(zls_offsets) / sizeof(zls_offsets[0])];
      unsigned i;
      APPLE_AGX_G4_NATIVE_RENDER zls = render;
      for (i = 0; i < sizeof(zls_offsets) / sizeof(zls_offsets[0]); ++i)
        before[i] = get64(objects[18].Data + zls_offsets[i]);
      assert(AppleAgxG4PatchRenderScalars(&render, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      for (i = 0; i < sizeof(zls_offsets) / sizeof(zls_offsets[0]); ++i)
        assert(get64(objects[18].Data + zls_offsets[i]) == before[i]);
      /* G13/V13_5 (Asahi fw/fragment.rs + queue/render.rs, m1n1
       * Start3DStruct2/3): JobParameters1 at work+0x80, JobParameters3 at
       * work+0x4b8; load/store/partial all use the attachment base. */
      zls.ZlsCtrl = 0x80080ULL;
      zls.IspZlsPixels = (1280u - 1u) | ((720u - 1u) << 15);
      zls.Depth.Base = 0x34000000ULL; zls.Depth.Stride = 0x1400u;
      zls.Depth.CompBase = 0x34400000ULL; zls.Depth.CompStride = 0x80u;
      zls.Stencil.Base = 0x35000000ULL; zls.Stencil.Stride = 0x500u;
      zls.Stencil.CompBase = 0x35400000ULL; zls.Stencil.CompStride = 0x40u;
      assert(AppleAgxG4PatchRenderScalars(&zls, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      assert(get64(objects[18].Data + 0xc8u) == zls.IspZlsPixels);
      assert(get64(objects[18].Data + 0xd8u) == zls.ZlsCtrl);
      assert(get64(objects[18].Data + 0xe0u) == zls.Depth.Base);
      assert(get64(objects[18].Data + 0xe8u) == zls.Depth.Base);
      assert(get64(objects[18].Data + 0xf0u) == zls.Stencil.Base);
      assert(get64(objects[18].Data + 0xf8u) == zls.Stencil.Base);
      assert(get64(objects[18].Data + 0x100u) == 0x1400u);
      assert(get64(objects[18].Data + 0x108u) == 0x1400u);
      assert(get64(objects[18].Data + 0x110u) == 0x500u);
      assert(get64(objects[18].Data + 0x118u) == 0x500u);
      assert(get64(objects[18].Data + 0x120u) == zls.Depth.CompBase);
      assert(get64(objects[18].Data + 0x128u) == 0x80u);
      assert(get64(objects[18].Data + 0x130u) == zls.Depth.CompBase);
      assert(get64(objects[18].Data + 0x138u) == 0x80u);
      assert(get64(objects[18].Data + 0x140u) == zls.Stencil.CompBase);
      assert(get64(objects[18].Data + 0x148u) == 0x40u);
      assert(get64(objects[18].Data + 0x150u) == zls.Stencil.CompBase);
      assert(get64(objects[18].Data + 0x158u) == 0x40u);
      assert(get64(objects[18].Data + 0x650u) == zls.ZlsCtrl);
      assert(get64(objects[18].Data + 0x660u) == zls.Depth.Base);
      assert(get64(objects[18].Data + 0x668u) == 0x1400u);
      assert(get64(objects[18].Data + 0x670u) == 0x80u);
      assert(get64(objects[18].Data + 0x678u) == zls.Depth.Base);
      assert(get64(objects[18].Data + 0x680u) == zls.Depth.Base);
      assert(get64(objects[18].Data + 0x688u) == zls.Depth.CompBase);
      assert(get64(objects[18].Data + 0x690u) == zls.Stencil.Base);
      assert(get64(objects[18].Data + 0x698u) == 0x500u);
      assert(get64(objects[18].Data + 0x6a0u) == 0x40u);
      assert(get64(objects[18].Data + 0x6a8u) == zls.Stencil.Base);
      assert(get64(objects[18].Data + 0x6b0u) == zls.Stencil.Base);
      assert(get64(objects[18].Data + 0x6b8u) == zls.Stencil.CompBase);
      assert(get64(objects[18].Data + 0x768u) == zls.IspZlsPixels);
      /* Neighbouring source-backed fields are not disturbed. */
      assert(get64(objects[18].Data + 0xa8u) == render.IspDbiasBase);
      assert(get64(objects[18].Data + 0x170u) == (480ULL << 24));
      assert(get64(objects[18].Data + 0x640u) == render.PartialBg.ResourceSpec);
      assert((get64(objects[18].Data + 0x740u) & 0xffffffffULL) == render.IspBgObjDepth);
      /* Restore the colour-only state for the checks below. */
      for (i = 0; i < sizeof(zls_offsets) / sizeof(zls_offsets[0]); ++i)
        memcpy(objects[18].Data + zls_offsets[i], &before[i], 8u);
    }
    assert(get64(objects[18].Data + 0xa0u) == render.IspScissorBase);
    assert(get64(objects[18].Data + 0x88u) == render.Bg.ResourceSpec);
    assert(get64(objects[18].Data + 0x90u) == render.Bg.Usc);
    assert(get64(objects[18].Data + 0x170u) == (480ULL << 24));
    assert(get64(objects[18].Data + 0x1c0u) == 0x1100000000ULL);
    assert(get64(objects[19].Data + 0x120u) == 0x1100000000ULL);
    /* R163 (EXP883): TilingParameters size2/size3 are the macro-tile stride
     * and twice it (m1n1 render.py, EXP208 full-screen template), not a
     * packed x/y pair. 1280x720: 12x8 tiles per macro tile. */
    assert((get64(objects[19].Data + 0x3e0u) & 0xffffffffULL) == 96ULL);
    assert((get64(objects[19].Data + 0x3e4u) & 0xffffffffULL) == 192ULL);
    assert((get64(objects[18].Data + 0x1d0u) & 0xffffffffULL) ==
        render.FragmentHelper.Binary);
    assert(get64(objects[18].Data + 0x1d8u) ==
        render.FragmentHelper.Data);
    assert((get64(objects[18].Data + 0x408u) & 0xffffffffULL) ==
        render.FragmentHelper.Cfg);
    {
      APPLE_AGX_EXP208_JOB_PARAMETERS parameters = {0};
      APPLE_AGX_BACKEND_JOB_IMAGE job = {0};
      parameters.ArenaGpuAddress = 0x1503800000ULL;
      parameters.ArenaBytes = AppleAgxRenderTemplateBytes();
      parameters.TaEvent = 1u;
      parameters.D3Event = 2u;
      parameters.TaExpectedStamp = 3u;
      parameters.D3ExpectedStamp = 4u;
      parameters.TaExpectedDonePointer = 5u;
      parameters.D3ExpectedDonePointer = 6u;
      assert(AppleAgxG4StageJob(&parameters, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &job));
      assert(job.TaWorkAddresses[1] == objects[19].GpuVa);
      assert(job.D3WorkAddresses[1] == objects[18].GpuVa);
      assert(get64(objects[19].Data + 88u) == 0ULL);
    }
    free(arena);
  }
  ranges[7].Bytes = 0x1000;
  assert(!AppleAgxG4BindProcessObjects(&render, ranges, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  puts("apple_agx_g4_builder_test: PASS");
  return 0;
}
