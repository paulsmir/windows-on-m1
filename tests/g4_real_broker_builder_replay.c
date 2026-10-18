#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_g4_builder.h"
#include "apple_agx_render_template_rebase.h"
#include "apple_agx_uat.h"

/* Reuse the real broker test's normal-RAM/slot fixture, then publish this
 * frame through the production m1n1 broker implementation. */
#define main broker_fixture_selftest_main
#include "../m1n1_windows/tests/hv_agx_gpuva_v5_test.c"
#undef main

#define REPLAY_MAX_PAGES 4096u
#define REPLAY_USC_BASE UINT64_C(0x1100000000)
typedef struct {
  uint64_t Va, Ipa;
  int Writable;
} MAPPED_PAGE;
typedef struct {
  struct fixture *Broker;
  MAPPED_PAGE Pages[REPLAY_MAX_PAGES];
  unsigned Count, AccessCalls;
  uint64_t NextIpa;
} FRAME_GRAPH;
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V2 Header;
  APPLE_AGX_G4_NATIVE_HEADER AttachCommand;
  APPLE_AGX_G4_ATTACHMENT Attachment;
  APPLE_AGX_G4_NATIVE_HEADER RenderCommand;
  APPLE_AGX_G4_NATIVE_RENDER Render;
} FRAME_PACKET;

static uint64_t align16k(uint64_t value) {
  return (value + 0x3fffULL) & ~0x3fffULL;
}

static void map_range(FRAME_GRAPH *graph, uint64_t va, uint64_t bytes,
                      int writable) {
  struct fixture *f = graph->Broker;
  uint64_t table = va >= REPLAY_USC_BASE ? Q_L2 : P_L2;
  assert(bytes && !(va & 0x3fffULL));
  for (uint64_t off = 0; off < align16k(bytes); off += 0x4000ULL) {
    uint64_t ipa = graph->NextIpa;
    uint64_t logical[4] = {ipa, ipa + 0x1000ULL,
                           ipa + 0x2000ULL, ipa + 0x3000ULL};
    unsigned index = (unsigned)(((va + off) >> 14) & 0x7ffULL);
    assert(graph->Count < REPLAY_MAX_PAGES);
    assert(hv_agx_gpuva_v5_register_shared_backing(
        &f->broker, 1, 1, 17, ipa) == HV_AGX_GPUVA_V5_OK);
    assert(hv_agx_gpuva_v5_update_leaf(&f->broker, 1, 1, table,
        index, logical, 17, 15, 15,
        writable ? 15 : 0) == HV_AGX_GPUVA_V5_OK);
    graph->Pages[graph->Count++] = (MAPPED_PAGE){va + off, ipa, writable};
    graph->NextIpa += 0x4000ULL;
  }
}

static int broker_access(void *opaque, unsigned long long va,
                         unsigned int bytes, int write) {
  FRAME_GRAPH *graph = opaque;
  uint64_t first, last;
  if (!graph || !bytes || va > UINT64_MAX - bytes) return 0;
  ++graph->AccessCalls;
  first = va & ~0x3fffULL;
  last = (va + bytes - 1ULL) & ~0x3fffULL;
  for (uint64_t page = first; page <= last; page += 0x4000ULL) {
    unsigned index = (unsigned)((page >> 14) & 0x7ffULL);
    uint64_t descriptor = graph->Broker->pages[
        page >= REPLAY_USC_BASE ? 6 : 2][index];
    unsigned found = 0;
    for (unsigned i = 0; i < graph->Count; ++i) {
      const MAPPED_PAGE *entry = &graph->Pages[i];
      unsigned long long encoded = 0;
      if (entry->Va != page || (write && !entry->Writable)) continue;
      assert(AppleAgxUatEncodePageDescriptor(1u, entry->Ipa,
          entry->Writable ? AppleAgxUatGpuSharedReadWrite :
                            AppleAgxUatGpuSharedReadOnly,
          &encoded) == AppleAgxUatResultOk);
      if (descriptor == encoded) found = 1;
      break;
    }
    if (!found) return 0;
  }
  return 1;
}

static uint64_t read64(const unsigned char *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (8u * i);
  return value;
}

static void check_active_relocations(FRAME_GRAPH *graph,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *objects,
    const FRAME_PACKET *packet) {
  const APPLE_AGX_EXP208_RELOCATION *relocs =
      AppleAgxRenderTemplateRelocations();
  for (unsigned i = 0; i < AppleAgxRenderTemplateRelocationCount(); ++i) {
    const APPLE_AGX_EXP208_RELOCATION *r = &relocs[i];
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *target;
    uint64_t expected;
    int write;
    if (r->SourceObject >= 36u || r->TargetObject < 36u) continue;
    target = &objects[r->TargetObject];
    assert(r->TargetObject != 36u && r->TargetObject != 73u &&
           r->TargetObject != 74u);
    assert(r->Encoding != AppleAgxExp208RelocationGpuVaPage32kU32);
    assert(r->SourceOffset + 8u <= objects[r->SourceObject].Size);
    expected = target->GpuVa ? target->GpuVa + r->TargetOffset |
        r->EncodingBits : 0ULL;
    assert(read64(objects[r->SourceObject].Data + r->SourceOffset) == expected);
    if (!target->GpuVa) {
      assert(r->TargetObject >= 67u && r->TargetObject <= 71u);
      continue;
    }
    write = r->TargetObject == 40u || r->TargetObject == 41u ||
        r->TargetObject == 42u ||
        (r->TargetObject >= 43u && r->TargetObject <= 72u &&
         r->TargetObject != 63u);
    assert(broker_access(graph, target->GpuVa + r->TargetOffset,
        1u, write));
  }
  assert(objects[40].GpuVa == packet->Attachment.Pointer);
  assert(broker_access(graph, packet->Attachment.Pointer,
      (unsigned)packet->Attachment.Size, 1));
}

static void setup_broker(FRAME_GRAPH *graph) {
  struct fixture *f = new_fixture();
  assert(f);
  f->range_backing = true;
  graph->Broker = f;
  graph->NextIpa = P_DATA;
  assert(hv_agx_gpuva_v5_create(&f->broker, 1, 1, P_ROOT, false) ==
      HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_register_table(&f->broker, 1, 1, P_L1, 1) ==
      HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_register_table(&f->broker, 1, 1, P_L2, 2) ==
      HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_register_table(&f->broker, 1, 1, Q_L1, 1) ==
      HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_register_table(&f->broker, 1, 1, Q_L2, 2) ==
      HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_update_parent(&f->broker, 1, 1,
      P_ROOT, 0, P_L1) == HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_update_parent(&f->broker, 1, 1,
      P_ROOT, 1, Q_L1) == HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_update_parent(&f->broker, 1, 1,
      P_L1, (0x10000000u >> 25) & 0x7ffu, P_L2) == HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_update_parent(&f->broker, 1, 1,
      Q_L1, (unsigned)((REPLAY_USC_BASE >> 25) & 0x7ffULL), Q_L2) ==
      HV_AGX_GPUVA_V5_OK);
}

int main(void) {
  FRAME_GRAPH *graph = calloc(1, sizeof(*graph));
  FRAME_PACKET packet = {0};
  APPLE_AGX_G4_SUBMIT_VIEW view = {0};
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT] = {{0}};
  unsigned required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  unsigned char *arena;
  uint64_t next_va = 0x10000000ULL, token = 0;
  assert(graph);
  setup_broker(graph);
  packet.Render.Flags = 1u << 2;
  packet.Render.WidthPx = 2560;
  packet.Render.HeightPx = 1600;
  packet.Render.Layers = 1;
  packet.Render.UtileWidthPx = packet.Render.UtileHeightPx = 32;
  packet.Render.Samples = 1;
  packet.Render.SampleSizeBytes = 8;
  packet.Render.PppCtrl = 0x202u;
  packet.Render.PppMultisampleCtrl = 0x88u;
  packet.Render.Bg.Usc = 0x20004u;
  packet.Render.Eot.Usc = 0x30004u;
  packet.Render.PartialBg.Usc = 0x40004u;
  packet.Render.PartialEot.Usc = 0x50004u;
  packet.Header.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  packet.Header.Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA;
  packet.Header.Base.HeaderBytes = sizeof(packet.Header);
  packet.Header.Base.CommandBytes = sizeof(packet) - sizeof(packet.Header);
  packet.Header.Base.CommandVa = next_va;
  packet.Header.Base.Reserved = APPLE_AGX_G4_COLOR_BGRA8;
  map_range(graph, next_va, 0x10000ULL, 0);
  next_va += 0x10000ULL;
  assert(AppleAgxG4ProcessRequiredBytes(&packet.Render, required));
  assert(required[2] / 0x20000u == 32u);
  /* Legacy v2 builder coverage. R137's separate combined replay exercises
   * the production UMD escape and exclusive private KMD graph. */
  for (unsigned i=0;i<9;++i) {
    packet.Header.Process[i]=(APPLE_AGX_G4_PROCESS_RANGE){next_va,required[i],0};
    map_range(graph,next_va,required[i],1);next_va+=required[i];
  }
  packet.Render.VdmCtrlStreamBase = next_va;
  map_range(graph, next_va, 0x10000ULL, 0); next_va += 0x10000ULL;
  packet.Render.IspScissorBase = next_va;
  map_range(graph, next_va, 0x10000ULL, 0); next_va += 0x10000ULL;
  packet.Render.IspDbiasBase = next_va;
  map_range(graph, next_va, 0x10000ULL, 0); next_va += 0x10000ULL;
  packet.Render.SamplerHeap = next_va;
  map_range(graph, next_va, 0x10000ULL, 0); next_va += 0x10000ULL;
  packet.Attachment.Pointer = next_va;
  packet.Attachment.Size = 2560ULL * 1600ULL * 4ULL;
  map_range(graph, next_va, packet.Attachment.Size, 1);
  next_va += align16k(packet.Attachment.Size);
  assert(next_va < 0x12000000ULL);
  for (unsigned i = 2u; i <= 5u; ++i)
    map_range(graph, REPLAY_USC_BASE + i * 0x10000ULL, 0x10000ULL, 0);
  packet.AttachCommand.Type = APPLE_AGX_G4_FRAGMENT_ATTACHMENTS;
  packet.AttachCommand.Size = sizeof(packet.Attachment);
  packet.AttachCommand.VdmBarrier = 0xffffu;
  packet.AttachCommand.CdmBarrier = 0xffffu;
  packet.RenderCommand.Type = APPLE_AGX_G4_RENDER;
  packet.RenderCommand.Size = sizeof(packet.Render);
  packet.Header.Base.Reserved = 0u;
  assert(AppleAgxG4ParseSubmit(&packet, sizeof(packet), sizeof(packet),
      packet.Header.Base.CommandVa, packet.Header.Base.CommandBytes,
      broker_access, graph, &view) == AppleAgxG4ParseUnsupported);
  packet.Header.Base.Reserved = APPLE_AGX_G4_COLOR_BGRA8;
  packet.Render.Samples = 4u;
  assert(AppleAgxG4ParseSubmit(&packet, sizeof(packet), sizeof(packet),
      packet.Header.Base.CommandVa, packet.Header.Base.CommandBytes,
      broker_access, graph, &view) == AppleAgxG4ParseOk);
  assert(!AppleAgxG4BindNativeObjects(&view, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  packet.Render.Samples = 1u;
  assert(AppleAgxG4ParseSubmit(&packet, sizeof(packet), sizeof(packet),
      packet.Header.Base.CommandVa, packet.Header.Base.CommandBytes,
      broker_access, graph, &view) == AppleAgxG4ParseOk);
  assert(graph->AccessCalls >= 15u);
  arena = malloc(AppleAgxRenderTemplateBytes());
  assert(arena && AppleAgxRenderTemplateMaterialize(
      arena, AppleAgxRenderTemplateBytes(), &roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjectsRebased(
      arena, AppleAgxRenderTemplateBytes(), 0x800000000ULL,
      0x1503800000ULL, 0x1503800000ULL,
      AppleAgxRenderTemplateBytes(), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &roots));
  assert(AppleAgxG4BuildTa3d(&view, arena, AppleAgxRenderTemplateBytes(),
      1u, objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  check_active_relocations(graph, objects, &packet);
  for (unsigned i = 37u; i <= 72u; ++i) {
    if (i >= 67u && i <= 71u) {
      assert(objects[i].GpuVa == 0ULL);
    } else if (i != 40u && i != 41u && i != 42u &&
               (i < 43u || i > 58u)) {
      assert(objects[i].GpuVa == 0ULL ||
          broker_access(graph, objects[i].GpuVa, 1u, 0));
    }
  }
  assert(broker_access(graph, packet.Attachment.Pointer,
      (unsigned)packet.Attachment.Size, 1));
  assert(graph->Broker->slots[0][1] == 0x90000001ULL);
  assert(hv_agx_gpuva_v5_lease(&graph->Broker->broker, 1, 1, 1,
      &token) == HV_AGX_GPUVA_V5_OK);
  assert(token && hv_agx_gpuva_v5_job_begin(&graph->Broker->broker,
      1, token) == HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_release(&graph->Broker->broker,
      1, token) == HV_AGX_GPUVA_V5_BUSY);
  assert(hv_agx_gpuva_v5_job_end(&graph->Broker->broker,
      1, token) == HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_release(&graph->Broker->broker,
      1, token) == HV_AGX_GPUVA_V5_OK);
  assert(graph->Broker->broker.slots[1].occupied == false);
  assert(graph->Broker->slots[0][1] == 0x90000001ULL);
  free(arena);
  free(graph->Broker);
  free(graph);
  puts("g4_real_broker_builder_replay: PASS");
  return 0;
}
