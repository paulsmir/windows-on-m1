#include "apple_agx_g4_builder.h"
#include "apple_agx_relocation.h"
#include "apple_agx_render_template_vm_slot.h"
#include <string.h>

#define G4_HEAP_BLOCK 0x20000ULL
#define G4_PREEMPT_CLUSTER_UPPER_BOUND 9ULL

static APPLE_AGX_BOOL g4_range_valid(
    const APPLE_AGX_G4_PROCESS_RANGE *range, APPLE_AGX_U32 required) {
  return range->Reserved == 0u && range->Va >= 0x10000ULL &&
      range->Va < (1ULL << 39) && (range->Va & 0xffffULL) == 0ULL &&
      (range->Bytes & 0xffffu) == 0u && range->Bytes >= required &&
      range->Bytes <= (1ULL << 39) - range->Va;
}

APPLE_AGX_BOOL AppleAgxG4BindProcessObjects(
    const APPLE_AGX_G4_NATIVE_RENDER *Render,
    const APPLE_AGX_G4_PROCESS_RANGE
        Process[APPLE_AGX_G4_PROCESS_RANGE_COUNT],
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount) {
  APPLE_AGX_U32 required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  APPLE_AGX_U32 i, j;
  if (Render == 0 || Process == 0 || Objects == 0 ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      !AppleAgxG4ProcessRequiredBytes(Render, required) ||
      Render->Layers != 1u || Render->Samples != 1u ||
      (Render->Flags & (1u << 2)) == 0u)
    return APPLE_AGX_FALSE;
  for (i = 0u; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    if (!g4_range_valid(&Process[i], required[i]))
      return APPLE_AGX_FALSE;
    for (j = 0u; j < i; ++j)
      if (Process[i].Va < Process[j].Va + Process[j].Bytes &&
          Process[j].Va < Process[i].Va + Process[i].Bytes)
        return APPLE_AGX_FALSE;
  }
  /* The EXP208 arena has 16 TVB block descriptors. A larger heap needs a
   * fresh buffer-manager layout; this binding cannot silently alias blocks. */
  if (Process[2].Bytes < 16u * G4_HEAP_BLOCK ||
      required[2] > 16u * G4_HEAP_BLOCK ||
      Process[7].Bytes < G4_PREEMPT_CLUSTER_UPPER_BOUND *
          (0x540ULL + 0x280ULL + 0x20ULL))
    return APPLE_AGX_FALSE;
  Objects[41].GpuVa = Process[0].Va;
  Objects[42].GpuVa = Process[1].Va;
  for (i = 43u; i <= 58u; ++i)
    Objects[i].GpuVa = Process[2].Va + (i - 43u) * G4_HEAP_BLOCK;
  Objects[59].GpuVa = Process[8].Va;
  Objects[60].GpuVa = Process[7].Va;
  Objects[61].GpuVa = Process[7].Va +
      G4_PREEMPT_CLUSTER_UPPER_BOUND * 0x540ULL;
  Objects[62].GpuVa = Process[7].Va +
      G4_PREEMPT_CLUSTER_UPPER_BOUND * (0x540ULL + 0x280ULL);
  Objects[63].GpuVa = 0ULL;
  Objects[64].GpuVa = Process[6].Va;
  Objects[65].GpuVa = Process[4].Va;
  Objects[66].GpuVa = Process[5].Va;
  for (i = 67u; i <= 71u; ++i)
    Objects[i].GpuVa = 0ULL;
  Objects[72].GpuVa = Process[3].Va;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxG4ApplySceneRelocations(
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount) {
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      candidate[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  const APPLE_AGX_EXP208_RELOCATION *relocations;
  APPLE_AGX_U32 count, i;
  if (Objects == 0 ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)
    return APPLE_AGX_FALSE;
  relocations = AppleAgxRenderTemplateRelocations();
  count = AppleAgxRenderTemplateRelocationCount();
  if (relocations == 0 || count != APPLE_AGX_RENDER_TEMPLATE_RELOCATION_COUNT)
    return APPLE_AGX_FALSE;
  for (i = 0u; i < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT; ++i) {
    candidate[i] = Objects[i];
    if (candidate[i].GpuVa == 0ULL)
      candidate[i].GpuVa = 0x10000ULL;
  }
  /* The generic relocator validates the complete batch before writing any
   * firmware bytes. A temporary nonzero address admits optional pointers;
   * these fields are set to null before the image can be submitted. */
  if (!AppleAgxApplyRelocations(candidate,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      relocations, count)) return APPLE_AGX_FALSE;
  for (i = 0u; i < count; ++i) {
    const APPLE_AGX_EXP208_RELOCATION *r = &relocations[i];
    APPLE_AGX_U32 j;
    if (Objects[r->TargetObject].GpuVa != 0ULL) continue;
    if (r->Encoding == AppleAgxExp208RelocationGpuVaPage32kU32 ||
        r->SourceOffset > Objects[r->SourceObject].Size ||
        Objects[r->SourceObject].Size - r->SourceOffset < 8u)
      return APPLE_AGX_FALSE;
    for (j = 0u; j < 8u; ++j)
      Objects[r->SourceObject].Data[r->SourceOffset + j] = 0u;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxG4BindNativeObjects(
    const APPLE_AGX_G4_SUBMIT_VIEW *View,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount) {
  APPLE_AGX_G4_NATIVE_RENDER render;
  APPLE_AGX_G4_ATTACHMENT color;
  APPLE_AGX_U64 minimum;
  if (View == 0 || Objects == 0 || View->Render == 0 ||
      View->RenderBytes != sizeof(render) || View->Attachments == 0 ||
      View->AttachmentCount != 1u ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)
    return APPLE_AGX_FALSE;
  memcpy(&render, View->Render, sizeof(render));
  memcpy(&color, View->Attachments, sizeof(color));
  minimum = (APPLE_AGX_U64)render.WidthPx * render.HeightPx * 4ULL;
  if (!render.WidthPx || !render.HeightPx || !minimum ||
      render.Layers != 1u || render.Samples != 1u ||
      (render.SampleSizeBytes != 8u && render.SampleSizeBytes != 16u) ||
      (render.Flags & (1u << 2)) == 0u ||
      (render.Flags & ~((1u << 1) | (1u << 2) | (1u << 18))) != 0u ||
      !render.VdmCtrlStreamBase || !render.IspScissorBase ||
      !render.IspDbiasBase || render.Depth.Base || render.Depth.CompBase ||
      render.Stencil.Base || render.Stencil.CompBase || render.ZlsCtrl ||
      render.IspZlsPixels || color.Pad || color.Flags ||
      color.Pointer < 0x10000ULL || color.Size < minimum ||
      color.Size > 0xffffffffULL)
    return APPLE_AGX_FALSE;
  Objects[37].GpuVa = render.VdmCtrlStreamBase;
  Objects[38].GpuVa = render.IspScissorBase;
  Objects[39].GpuVa = render.IspDbiasBase;
  Objects[40].GpuVa = color.Pointer;
  Objects[63].GpuVa = render.SamplerHeap;
  return APPLE_AGX_TRUE;
}

static void g4_put16(unsigned char *p, APPLE_AGX_U32 value) {
  p[0] = (unsigned char)value;
  p[1] = (unsigned char)(value >> 8);
}
static void g4_put32(unsigned char *p, APPLE_AGX_U32 value) {
  g4_put16(p, value);
  g4_put16(p + 2u, value >> 16);
}
static void g4_put64(unsigned char *p, APPLE_AGX_U64 value) {
  g4_put32(p, (APPLE_AGX_U32)value);
  g4_put32(p + 4u, (APPLE_AGX_U32)(value >> 32));
}
static APPLE_AGX_U32 g4_get32(const unsigned char *p) {
  return (APPLE_AGX_U32)p[0] | ((APPLE_AGX_U32)p[1] << 8) |
      ((APPLE_AGX_U32)p[2] << 16) | ((APPLE_AGX_U32)p[3] << 24);
}
static APPLE_AGX_U32 g4_align4(APPLE_AGX_U32 value) {
  return (value + 3u) & ~3u;
}

APPLE_AGX_BOOL AppleAgxG4PatchRenderScalars(
    const APPLE_AGX_G4_NATIVE_RENDER *Render,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount) {
  unsigned char *work, *ta;
  APPLE_AGX_U32 tiles_x, tiles_y, mtile_x, mtile_y, utiles_x, utiles_y;
  APPLE_AGX_U32 utile, blocks, tile_config, tile_counts, rgn_size, tpc_stride;
  APPLE_AGX_U32 mtile1, mtile2;
  if (Render == 0 || Objects == 0 ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      Objects[18].Data == 0 || Objects[18].Size < 0x900u ||
      Objects[19].Data == 0 || Objects[19].Size < 0x550u ||
      !Render->WidthPx || !Render->HeightPx || Render->Layers != 1u ||
      Render->Samples != 1u || !Render->UtileWidthPx ||
      !Render->UtileHeightPx ||
      (Render->Flags & (1u << 2)) == 0u)
    return APPLE_AGX_FALSE;
  work = Objects[18].Data;
  ta = Objects[19].Data;
  tiles_x = ((APPLE_AGX_U32)Render->WidthPx + 31u) / 32u;
  tiles_y = ((APPLE_AGX_U32)Render->HeightPx + 31u) / 32u;
  mtile_x = g4_align4((tiles_x + 3u) / 4u);
  mtile_y = g4_align4((tiles_y + 3u) / 4u);
  utiles_x = mtile_x * (32u / Render->UtileWidthPx);
  utiles_y = mtile_y * (32u / Render->UtileHeightPx);
  utile = ((Render->UtileWidthPx / 16u) << 12) |
      ((Render->UtileHeightPx / 16u) << 14);
  blocks = ((APPLE_AGX_U32)Render->SampleSizeBytes *
      Render->UtileWidthPx * Render->UtileHeightPx + 2047u) / 2048u;
  tile_config = 0x280u | ((Render->Flags & (1u << 1)) ? 0x10000u : 0u);
  tile_counts = ((tiles_y - 1u) << 12) | (tiles_x - 1u);
  rgn_size = g4_align4(5u * mtile_x * mtile_y *
      (32u / Render->UtileWidthPx) *
      (32u / Render->UtileHeightPx)) / 4u;
  tpc_stride = 2u * utiles_x * utiles_y;
  mtile1 = 3u * mtile_x | ((2u * mtile_x) << 9) | (mtile_x << 18);
  mtile2 = 3u * mtile_y | ((2u * mtile_y) << 9) | (mtile_y << 18);
  /* G13/V13_5 RunFragment/RunVertex embedded parameters; offsets follow
   * Asahi fw/{fragment,vertex}.rs and m1n1 cmdqueue.py. */
  g4_put32(work + 0x80u, utile);
  g4_put32(ta + 0x88u, utile);
  g4_put64(work + 0x88u, Render->Bg.ResourceSpec);
  g4_put64(work + 0x90u, Render->Bg.Usc);
  g4_put32(work + 0x3c8u, Render->Eot.ResourceSpec);
  g4_put32(work + 0x3ccu, Render->Eot.Usc);
  g4_put64(work + 0x610u, Render->PartialBg.ResourceSpec);
  g4_put64(work + 0x618u, Render->PartialBg.Usc);
  g4_put64(work + 0x640u, Render->PartialBg.ResourceSpec);
  g4_put64(work + 0x648u, Render->PartialBg.Usc);
  g4_put32(work + 0x70cu, Render->PartialEot.ResourceSpec);
  g4_put32(work + 0x714u, Render->PartialEot.Usc);
  g4_put32(work + 0x72cu, Render->PartialEot.ResourceSpec);
  g4_put32(work + 0x734u, Render->PartialEot.Usc);
  g4_put64(work + 0xa0u, Render->IspScissorBase);
  g4_put64(work + 0x4c8u, Render->IspScissorBase);
  g4_put64(work + 0xa8u, Render->IspDbiasBase);
  g4_put64(work + 0x4b8u, Render->IspDbiasBase);
  g4_put64(work + 0x48u, Render->PppMultisampleCtrl);
  g4_put64(work + 0x98u, Render->PppMultisampleCtrl);
  g4_put64(ta + 0x90u, Render->PppMultisampleCtrl);
  g4_put32(work + 0x50u, Render->Samples);
  g4_put16(work + 0x54u, mtile_y);
  g4_put16(work + 0x56u, mtile_x);
  g4_put32(work + 0x60u, Render->IspMergeUpperX);
  g4_put32(work + 0x64u, Render->IspMergeUpperY);
  g4_put64(work + 0x70u, tiles_x * tiles_y);
  g4_put16(work + 0x3e0u, utiles_y);
  g4_put16(work + 0x3e2u, utiles_x);
  g4_put32(work + 0x3d8u, Render->IspMergeUpperX);
  g4_put32(work + 0x3dcu, Render->IspMergeUpperY);
  g4_put32(work + 0x3f0u, tile_counts);
  g4_put32(work + 0x3f4u, blocks);
  g4_put32(work + 0x6d0u, blocks);
  g4_put32(work + 0x748u, Render->SampleSizeBytes);
  g4_put64(work + 0x180u, tile_config);
  g4_put64(work + 0x6f0u, tile_config);
  g4_put32(work + 0x3f8u, Render->IspBgObjDepth);
  g4_put32(work + 0x3fcu, Render->IspBgObjVals | 0x400u);
  g4_put32(work + 0x740u, Render->IspBgObjDepth);
  g4_put32(work + 0x744u, Render->IspBgObjVals);
  g4_put32(work + 0xb0u, 0xc000u |
      ((Render->Flags & (1u << 18)) ? 0x40000u : 0u));
  g4_put32(work + 0x6d8u, 0xc000u |
      ((Render->Flags & (1u << 18)) ? 0x40000u : 0u));
  g4_put32(ta + 0x3c4u, rgn_size);
  g4_put32(ta + 0x3c8u, 0x88u);
  g4_put32(ta + 0x3ccu, Render->PppCtrl);
  g4_put16(ta + 0x3d0u, Render->WidthPx - 1u);
  g4_put16(ta + 0x3d2u, Render->HeightPx - 1u);
  g4_put32(ta + 0x3d4u, tile_counts);
  g4_put32(ta + 0x3d8u, mtile1);
  g4_put32(ta + 0x3dcu, mtile2);
  g4_put32(ta + 0x3e0u, utiles_y | (utiles_x << 16));
  g4_put32(ta + 0x3e4u, tpc_stride);
  g4_put32(ta + 0x3e8u, 0x100u);
  g4_put32(ta + 0x3ecu, 0x8000u);
  g4_put32(ta + 0x3f0u, Render->VertexHelper.Cfg);
  g4_put32(ta + 0xe8u, g4_get32(ta + 0xe8u) | 1u);
  g4_put32(ta + 0x140u, Render->VertexHelper.Binary);
  g4_put64(ta + 0x148u, Render->VertexHelper.Data);
  g4_put32(work + 0x1c8u, Render->FragmentHelper.Binary);
  g4_put64(work + 0x1d0u, Render->FragmentHelper.Data);
  g4_put32(work + 0x8b4u, Render->SamplerCount);
  g4_put32(work + 0x8b8u, (APPLE_AGX_U32)Render->SamplerCount + 1u);
  g4_put32(ta + 0x550u, Render->SamplerCount);
  g4_put32(ta + 0x554u, (APPLE_AGX_U32)Render->SamplerCount + 1u);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxG4BuildTa3d(
    const APPLE_AGX_G4_SUBMIT_VIEW *View,
    void *Arena, APPLE_AGX_U32 ArenaBytes,
    APPLE_AGX_U32 VmSlot,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount) {
  APPLE_AGX_G4_NATIVE_RENDER render;
  if (View == 0 || View->Render == 0 ||
      View->RenderBytes != sizeof(render) || Arena == 0 ||
      Objects == 0 || VmSlot == 0u || VmSlot >= 63u) return APPLE_AGX_FALSE;
  memcpy(&render, View->Render, sizeof(render));
  if (!AppleAgxG4BindProcessObjects(&render, View->Process,
          Objects, ObjectCount) ||
      !AppleAgxG4BindNativeObjects(View, Objects, ObjectCount) ||
      !AppleAgxRenderTemplateSelectVmSlot(Arena, ArenaBytes, VmSlot) ||
      !AppleAgxG4ApplySceneRelocations(Objects, ObjectCount) ||
      !AppleAgxG4PatchRenderScalars(&render, Objects, ObjectCount))
    return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}
