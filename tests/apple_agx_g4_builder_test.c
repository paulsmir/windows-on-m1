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
      packet.Attachment.Size = 1024u;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Attachment.Size = 1280ULL * 720ULL * 4ULL;
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
