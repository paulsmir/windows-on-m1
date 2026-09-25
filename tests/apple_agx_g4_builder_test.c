#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_g4_builder.h"
#include "apple_agx_render_template_rebase.h"
#include "apple_agx_render_template_vm_slot.h"

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
  render.Bg.Usc = 0x20004u;
  render.Bg.ResourceSpec = 0x123u;
  render.Eot.Usc = 0x30004u;
  render.Eot.ResourceSpec = 0x456u;
  render.PartialBg.Usc = 0x40004u;
  render.PartialEot.Usc = 0x50004u;
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
  /* The template's object63 has no native Asahi owner. */
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
      packet.Attachment.Size = 1024u;
      assert(!AppleAgxG4BindNativeObjects(&view, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      packet.Attachment.Size = 1280ULL * 720ULL * 4ULL;
      assert(AppleAgxG4BuildTa3d(&view, arena,
          AppleAgxRenderTemplateBytes(), 1u, objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
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
    assert(get64(objects[18].Data + 0xa0u) == render.IspScissorBase);
    assert(get64(objects[18].Data + 0x88u) == render.Bg.ResourceSpec);
    assert(get64(objects[18].Data + 0x90u) == render.Bg.Usc);
    free(arena);
  }
  ranges[7].Bytes = 0x1000;
  assert(!AppleAgxG4BindProcessObjects(&render, ranges, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  puts("apple_agx_g4_builder_test: PASS");
  return 0;
}
