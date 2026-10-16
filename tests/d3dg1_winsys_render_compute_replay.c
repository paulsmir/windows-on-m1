/* D3D-G1: a render batch whose draws launched compute work, through the
 * production batch finish, GPUVA submit and KMD parser. Host boundaries only;
 * no external Mesa implementation is copied into this replay. */
#include "apple_agx_g4_submit.h"
#include "apple_agx_g13_compute_work.h"
#include "agx_win32_gpuva.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned APPLE_AGX_U32;

#define PIPE_MAX_COLOR_BUFS 8
#define PIPE_FORMAT_NONE 0
#define PIPE_FORMAT_B8G8R8A8_UNORM 1
#define PIPE_FORMAT_B8G8R8A8_SRGB 2
#define PIPE_FORMAT_B8G8R8X8_UNORM 3
#define PIPE_FORMAT_B8G8R8X8_SRGB 4
#define PIPE_FORMAT_R8G8B8A8_UNORM 5
#define PIPE_FORMAT_R16G16B16A16_FLOAT 6
#define PIPE_FORMAT_R10G10B10A2_UNORM 7
#define PIPE_FORMAT_R8_UNORM 8
#define PIPE_FORMAT_A8_UNORM 9
#define PIPE_FORMAT_R8G8_UNORM 10
/* D3D-G10a: the colour class follows the block size. */
static unsigned util_format_get_blocksize(unsigned format) {
  return format == PIPE_FORMAT_R8_UNORM || format == PIPE_FORMAT_A8_UNORM ? 1u :
      format == PIPE_FORMAT_R8G8_UNORM ? 2u :
      format == PIPE_FORMAT_R16G16B16A16_FLOAT ? 8u : 4u;
}
#define DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS 3u
#define DRM_ASAHI_BARRIER_NONE 0xffffu
#define AGX_G4_WRITTEN_LIMIT 16u
#define AGX_WIN32_ENCODER_TAIL 0x100u
#define drm_asahi_cmd_render _APPLE_AGX_G4_NATIVE_RENDER_HOST
struct drm_asahi_cmd_render { APPLE_AGX_G4_NATIVE_RENDER native; };
/* The production function accesses these lowercase UAPI fields only. */
#define samples native.Samples
#define sample_size_B native.SampleSizeBytes
#define utile_width_px native.UtileWidthPx
#define utile_height_px native.UtileHeightPx
/* asahi_drm.h subset, laid out as APPLE_AGX_G4_NATIVE_COMPUTE. */
struct drm_asahi_cmd_compute {
  uint32_t flags, sampler_count;
  uint64_t cdm_ctrl_stream_base, cdm_ctrl_stream_end, sampler_heap;
  struct { uint32_t binary, cfg; uint64_t data; } helper;
  struct { struct { uint32_t handle, offset; } start, end; } ts;
};
_Static_assert(sizeof(struct drm_asahi_cmd_compute) ==
               sizeof(APPLE_AGX_G4_NATIVE_COMPUTE), "compute layout");
struct drm_asahi_attachment { uint64_t pointer, size; uint32_t pad, flags; };
struct drm_asahi_cmd_header { uint16_t cmd_type, size, vdm_barrier, cdm_barrier; };
struct agx_va { uint64_t addr; };
struct agx_bo {
  struct agx_va *va;
  AGX_WIN32_GPUVA_BO view;
  unsigned handle, flags, refs, resident, uploaded, published;
  unsigned char cpu[512];
  size_t size;
};
struct agx_resource {
  struct agx_bo *bo;
  struct { uint64_t size_B, level_offsets_B[1]; } layout;
  struct agx_resource *separate_stencil;
};
struct util_dynarray { void *data; unsigned size; };
#define util_dynarray_num_elements(a, t) ((a)->size / sizeof(t))
#define util_dynarray_foreach(a, t, i) \
  for (t *i = (t *)(a)->data; i < (t *)((char *)(a)->data + (a)->size); ++i)
struct agx_pool { struct util_dynarray bos; struct agx_bo *transient_bo; unsigned transient_offset; };
typedef struct {
  AGX_WIN32_GPUVA_SPACE Gpuva;
  void *Native;
  unsigned GpuvaReady, Failed;
  int ZlsAttachments;
  uint64_t NewestFence;
} AGX_WIN32_ASAHI_BACKEND;
struct agx_screen { struct agx_bo *rodata; };
struct agx_device { struct agx_bo *zero_bo, *scratch_bo; };
#define AGX_MAX_BATCHES 1
struct agx_batch;
struct agx_context { struct { struct agx_screen *screen; } base;
  struct { struct agx_batch *slots; } batches; };
struct agx_batch {
  struct agx_context *ctx;
  void *windows_batch;
  struct agx_encoder { struct agx_bo *bo; unsigned char *current; } vdm, cdm;
  unsigned draws, clear;
  struct { unsigned bit_count, count; unsigned handles[8]; } bo_list;
  struct agx_pool pool, pipeline_pool;
  struct { struct agx_bo *bo; } sampler_heap;
  int compute;
  struct {
    unsigned nr_cbufs;
    struct { struct agx_resource *texture; unsigned format; }
      cbufs[PIPE_MAX_COLOR_BUFS], zsbuf;
  } key;
};
#define AGX_BATCH_FOREACH_BO_HANDLE(b, h) \
  for (unsigned hi = 0; hi < (b)->bo_list.count && ((h) = (int)(b)->bo_list.handles[hi], 1); ++hi)
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V3 Header;
  unsigned char Native[APPLE_AGX_G4_NATIVE_MAX_BYTES];
} AGX_G4_PRIVATE;
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V5 Header;
  unsigned char Native[APPLE_AGX_G4_NATIVE_MAX_BYTES];
} AGX_G4_PRIVATE_V5;
typedef struct {
  struct agx_bo *Command;
  struct agx_bo *Preemption;
  APPLE_AGX_G4_PRIVATE_LEASE Lease;
  uint64_t Fence;
  unsigned Entered, ComputeEntered, Submitted, Retired, Rejected;
} AGX_G4_BATCH;

static AGX_WIN32_ASAHI_BACKEND owner;
static struct agx_bo bos[16];
static struct agx_va vas[16];
static struct agx_batch *writer_of[16];
static unsigned resident_count, submit_count, release_count, creates;
static APPLE_AGX_G4_FAILURE failure;
static struct {
  unsigned char packet[sizeof(AGX_G4_PRIVATE_V5)];
  uint32_t packet_bytes, bytes;
  uint64_t va, written[16];
  unsigned written_count;
  int v5_result, v3_result;
  APPLE_AGX_G4_SUBMIT_VIEW view;
} sub;
static int AgxWin32AsahiShadowPublish(struct agx_bo *bo, size_t bytes) {
  (void)bytes; assert(bo); ++bo->published; return 1;
}
static AGX_WIN32_ASAHI_BACKEND *backend(struct agx_batch *b) { (void)b; return &owner; }
static AGX_G4_BATCH *capsule(struct agx_batch *b) { return b->windows_batch; }
static struct agx_screen *agx_screen(struct agx_screen *s) { return s; }
static struct agx_device native_device;
static struct agx_device *agx_device(struct agx_screen *s) { (void)s; return &native_device; }
static struct agx_resource *agx_resource(struct agx_resource *r) { return r; }
static uint64_t agx_map_gpu(struct agx_resource *r) { return r->bo->va->addr; }
static int agx_batch_is_compute(const struct agx_batch *b) { return b->compute; }
static struct agx_batch *agx_writer_get(struct agx_context *c, unsigned h) {
  (void)c; return h < 16u ? writer_of[h] : NULL;
}
static const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(
    AGX_WIN32_ASAHI_BACKEND *b, struct agx_bo *bo) {
  (void)b; return bo && bo->refs ? &bo->view : NULL;
}
static struct agx_bo *AgxWin32AsahiLookupBo(void *native, unsigned h) {
  (void)native; return h < 16 && bos[h].refs ? &bos[h] : NULL;
}
/* The command BO is bos[1]; the per-job context-store buffer is bos[12]. */
static struct agx_bo *agx_bo_create(void *native, unsigned bytes, unsigned align,
                                   unsigned flags, const char *label) {
  (void)native; (void)align; (void)flags;
  struct agx_bo *bo = strcmp(label, "Compute preemption") == 0 ? &bos[12] : &bos[1];
  assert(bo != &bos[1] || bytes <= sizeof(bo->cpu));
  assert(!bo->refs);
  ++creates; bo->size = bytes; ++bo->refs; return bo;
}
static unsigned char *agx_bo_map(struct agx_bo *bo) { return bo->cpu; }
static void agx_bo_unreference(void *native, struct agx_bo *bo) {
  (void)native; assert(bo->refs && !bo->resident); --bo->refs;
}
static struct drm_asahi_cmd_header agx_cmd_header(bool compute, unsigned v, unsigned c) {
  return (struct drm_asahi_cmd_header){compute ? APPLE_AGX_G4_COMPUTE : APPLE_AGX_G4_RENDER,
      compute ? sizeof(APPLE_AGX_G4_NATIVE_COMPUTE) : sizeof(APPLE_AGX_G4_NATIVE_RENDER),
      (uint16_t)v, (uint16_t)c};
}
static int private_escape(void *ctx, APPLE_AGX_G3_PRIVATE_REQUEST *r) {
  (void)ctx;
  if (r->Operation == APPLE_AGX_G3_PRIVATE_RELEASE) { ++release_count; return 1; }
  APPLE_AGX_G4_NATIVE_RENDER render = {.WidthPx = r->Width, .HeightPx = r->Height,
    .Layers = r->Layers, .Samples = r->Samples, .UtileWidthPx = r->UtileWidth,
    .UtileHeightPx = r->UtileHeight};
  unsigned sizes[9]; assert(AppleAgxG4ProcessRequiredBytes(&render, sizes));
  uint64_t va = 0x20000000;
  for (unsigned i = 0; i < 9; ++i) {
    r->Ranges[i] = (APPLE_AGX_G4_PROCESS_RANGE){va, sizes[i], 0}; va += sizes[i];
  }
  r->ManagerId = r->ManagerGeneration = r->SceneId = r->SceneGeneration = 1;
  return 1;
}
static int make_resident(void *ctx, const uint64_t *tokens, unsigned count, uint64_t *fence) {
  (void)ctx; resident_count = count;
  for (unsigned i = 0; i < count; ++i) { assert(tokens[i] < 16); bos[tokens[i]].resident = 1; }
  *fence = 10; return 2;
}
static int wait_paging(void *ctx, uint64_t fence) { (void)ctx; assert(fence == 10); return 1; }
/* KMD side: the process graph maps exactly the uploaded residency set. */
static int mapped(void *ctx, unsigned long long va, unsigned bytes, int write,
    APPLE_AGX_G4_ACCESS_KIND kind, unsigned ordinal) {
  (void)ctx; (void)write; (void)ordinal;
  if (kind == AppleAgxG4AccessProcess) return 1;
  for (unsigned i = 1; i < 16; ++i)
    if (bos[i].resident && bos[i].uploaded && va >= bos[i].view.Va &&
        va - bos[i].view.Va < bos[i].view.Bytes &&
        bytes <= bos[i].view.Bytes - (va - bos[i].view.Va)) return 1;
  return 0;
}
static int submit(void *ctx, const uint64_t *written, unsigned written_count,
    uint64_t va, uint32_t bytes, const void *packet, uint32_t packet_bytes, uint64_t *fence) {
  (void)ctx; ++submit_count;
  for (unsigned i = 1; i < 16; ++i) if (bos[i].resident) bos[i].uploaded = 1;
  assert(packet_bytes <= sizeof(sub.packet) && written_count <= 16u);
  memcpy(sub.packet, packet, packet_bytes);
  sub.packet_bytes = packet_bytes; sub.va = va; sub.bytes = bytes;
  sub.written_count = written_count;
  memcpy(sub.written, written, written_count * sizeof(written[0]));
  sub.v5_result = AppleAgxG4ParseSubmitComputeEx(packet, packet_bytes, packet_bytes, va,
      bytes, APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES, mapped, NULL, &sub.view, &failure);
  {
    APPLE_AGX_G4_SUBMIT_VIEW v3_view;
    sub.v3_result = AppleAgxG4ParseSubmitEx(packet, packet_bytes, packet_bytes, va, bytes,
        mapped, NULL, &v3_view, NULL);
  }
  if (sub.v5_result != AppleAgxG4ParseOk) {
    fprintf(stderr, "D3D-G1 parse=%d kind=%u ordinal=%u VA=%llx\n", sub.v5_result,
        failure.Kind, failure.Ordinal, failure.Va);
    return 0;
  }
  *fence = 20; return 1;
}
static int wait_render(void *ctx, uint64_t fence) { (void)ctx; assert(fence == 20); return 1; }
static int evict(void *ctx, const uint64_t *tokens, unsigned count) {
  (void)ctx;
  for (unsigned i = 0; i < count; ++i) { assert(bos[tokens[i]].resident); bos[tokens[i]].resident = 0; }
  return 1;
}
int AgxWin32AsahiBatchPoll(struct agx_batch *batch, unsigned timeout);
/* D3D-G10a: the screen renders the colour formats defined above and no
 * others. */
static int AgxWin32AsahiRenderableFormat(const struct agx_batch *batch, unsigned format) {
  (void)batch; return format != PIPE_FORMAT_NONE && format <= PIPE_FORMAT_R8G8_UNORM;
}
#include "d3dg1_winsys_functions.inc"

static int written_has(unsigned token) {
  for (unsigned i = 0; i < sub.written_count; ++i) if (sub.written[i] == token) return 1;
  return 0;
}

int main(void) {
  for (unsigned i = 1; i < 16; ++i) {
    vas[i].addr = 0x100000ULL * i; bos[i].va = &vas[i]; bos[i].handle = i;
    bos[i].view = (AGX_WIN32_GPUVA_BO){i, vas[i].addr, 0x40000, 1};
    bos[i].refs = i == 1 || i == 12 ? 0 : 1;
  }
  bos[12].view.Bytes = 0x10000;
  vas[5].addr = 0x270000; bos[5].view.Va = vas[5].addr;
  bos[6].size = sizeof(bos[6].cpu);
  owner.GpuvaReady = 1;
  owner.Gpuva.Ops = (AGX_WIN32_GPUVA_OPS){.MakeResident = make_resident,
    .WaitPaging = wait_paging, .Submit = submit, .WaitRender = wait_render,
    .Evict = evict, .PrivateEscape = private_escape};
  struct agx_bo *pool_bos[1] = {&bos[5]}, *pipeline_bos[1] = {&bos[8]};
  struct agx_screen screen = {&bos[3]}; struct agx_context ctx = {{&screen}, {NULL}};
  native_device = (struct agx_device){&bos[10], &bos[11]};
  struct agx_resource color = {.bo = &bos[4], .layout = {.size_B = 65536}};
  /* bos[13] is a stream-output buffer the batch's compute work writes;
   * bos[14] the sampler heap; bos[6] the CDM encoder; bos[2] the VDM one. */
  struct agx_batch batch = {.ctx = &ctx, .vdm = {&bos[2], bos[2].cpu}, .draws = 1,
    .bo_list = {.bit_count = 16, .count = 2, .handles = {3, 13}},
    .pool = {.bos = {pool_bos, sizeof(pool_bos)}},
    .pipeline_pool = {.bos = {pipeline_bos, sizeof(pipeline_bos)}},
    .sampler_heap = {&bos[14]},
    .key = {.nr_cbufs = 1, .cbufs = {{&color, PIPE_FORMAT_B8G8R8A8_UNORM}}}};
  writer_of[4] = writer_of[13] = &batch;
  batch.windows_batch = calloc(1, sizeof(AGX_G4_BATCH)); assert(batch.windows_batch);
  AGX_G4_BATCH *g = capsule(&batch);
  APPLE_AGX_G4_NATIVE_RENDER render = {.WidthPx = 31, .HeightPx = 17, .Layers = 1,
    .Samples = 1, .SampleSizeBytes = 8, .UtileWidthPx = 16, .UtileHeightPx = 16,
    .VdmCtrlStreamBase = bos[2].view.Va, .IspScissorBase = bos[5].view.Va + 0xe40};
  struct drm_asahi_cmd_render native; memcpy(&native, &render, sizeof(render));
  struct drm_asahi_cmd_compute compute = {0};
  compute.cdm_ctrl_stream_base = bos[6].view.Va;
  compute.cdm_ctrl_stream_end = bos[6].view.Va + 0x40;
  compute.sampler_heap = bos[14].view.Va; compute.sampler_count = 1;

  /* The draw's CDM scope: refused without a CDM encoder, then entered once
   * (agx_launch_gs_prerast) and finalized inside the encoder. */
  assert(!AgxWin32AsahiBatchComputeEnter(&batch));
  batch.cdm = (struct agx_encoder){&bos[6], bos[6].cpu + 0x40};
  assert(AgxWin32AsahiBatchComputeEnter(&batch) && g->ComputeEntered == 1);
  /* Still inside the scope: refused before anything exists. */
  assert(!AgxWin32AsahiBatchFinish(&batch, &native, &compute) && !creates && !g->Rejected);
  assert(AgxWin32AsahiBatchComputeLeave(&batch));
  assert(AgxWin32AsahiBatchComputeFinalize(&batch, batch.cdm.current));
  /* A CDM encoder without its command, a command without the encoder,
   * flags or timestamps: refused at entry, batch not rejected. (EXP1339: a
   * helper program, compute scratch, is carried by the command.) */
  assert(!AgxWin32AsahiBatchFinish(&batch, &native, NULL) && !creates && !g->Rejected);
  compute.flags = 1u;
  assert(!AgxWin32AsahiBatchFinish(&batch, &native, &compute) && !creates && !g->Rejected);
  compute.flags = 0u; compute.ts.end.handle = 2u;
  assert(!AgxWin32AsahiBatchFinish(&batch, &native, &compute) && !creates && !g->Rejected);
  compute.ts.end.handle = 0u;
  {
    struct agx_encoder cdm = batch.cdm;
    batch.cdm.bo = NULL;
    assert(!AgxWin32AsahiBatchFinish(&batch, &native, &compute) && !creates && !g->Rejected);
    batch.cdm = cdm;
  }

  assert(AgxWin32AsahiBatchFinish(&batch, &native, &compute));
  assert(submit_count == 1 && g->Submitted && g->Fence == 20 && creates == 2);
  /* The KMD admits the v5 envelope; the render-only parser does not. */
  assert(sub.v5_result == AppleAgxG4ParseOk && sub.v3_result != AppleAgxG4ParseOk);
  assert(sub.view.HasCompute &&
         sub.view.Compute.CdmCtrlStreamBase == compute.cdm_ctrl_stream_base &&
         sub.view.Compute.CdmCtrlStreamEnd == compute.cdm_ctrl_stream_end &&
         sub.view.Compute.SamplerHeap == compute.sampler_heap &&
         sub.view.Preemption.Va == bos[12].view.Va &&
         sub.view.Preemption.Bytes >= APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES);
  /* The DMA buffer is the command BO: COMPUTE {0, 0}, attachments, RENDER
   * {NONE, 1}; the private data repeats it behind the v5 header. */
  APPLE_AGX_G4_PRIVATE_HEADER_V5 header; memcpy(&header, sub.packet, sizeof(header));
  assert(header.V3.V2.Base.Version == APPLE_AGX_G4_PRIVATE_VERSION_RENDER_COMPUTE &&
         header.V3.V2.Base.HeaderBytes == sizeof(header) &&
         sub.packet_bytes == sizeof(header) + sub.bytes && sub.va == bos[1].view.Va &&
         memcmp(bos[1].cpu, sub.packet + sizeof(header), sub.bytes) == 0);
  APPLE_AGX_G4_NATIVE_HEADER first, last;
  memcpy(&first, bos[1].cpu, sizeof(first));
  memcpy(&last, bos[1].cpu + sub.bytes - sizeof(APPLE_AGX_G4_NATIVE_RENDER) - sizeof(last),
         sizeof(last));
  assert(first.Type == APPLE_AGX_G4_COMPUTE && first.VdmBarrier == 0u && first.CdmBarrier == 0u);
  assert(memcmp(bos[1].cpu + sizeof(first), &compute, sizeof(compute)) == 0);
  assert(last.Type == APPLE_AGX_G4_RENDER && last.VdmBarrier == 0xffffu && last.CdmBarrier == 1u);
  /* Residency: the CDM stream, the context-store buffer and the sampler
   * heap join the render's set; both encoders are published. */
  assert(resident_count == 12u && bos[6].uploaded && bos[12].uploaded && bos[14].uploaded);
  assert(bos[2].published == 1u && bos[6].published == 1u);
  /* Written: the colour buffer and the buffer the compute work writes. */
  assert(sub.written_count == 2u && written_has(4) && written_has(13));
  /* Retirement returns both job BOs. */
  assert(AgxWin32AsahiBatchPoll(&batch, 1000));
  assert(AgxWin32AsahiBatchRelease(&batch));
  assert(!batch.windows_batch && release_count == 1 && !bos[1].refs && !bos[12].refs);

  /* D3D-G2b: a render batch with two colour targets (no compute work) is one
   * envelope whose attachments name both (the first is the KMD's bound
   * output); both are in the written set; the KMD parser admits it. */
  {
    struct agx_resource second = {.bo = &bos[15], .layout = {.size_B = 65536}};
    struct agx_batch mrt = batch;
    mrt.cdm = (struct agx_encoder){NULL, NULL};
    mrt.key.nr_cbufs = 2;
    mrt.key.cbufs[1].texture = &second;
    mrt.key.cbufs[1].format = PIPE_FORMAT_R8G8B8A8_UNORM;
    mrt.bo_list.count = 1; mrt.bo_list.handles[0] = 3;
    writer_of[13] = NULL;
    mrt.windows_batch = calloc(1, sizeof(AGX_G4_BATCH)); assert(mrt.windows_batch);
    unsigned before = submit_count;
    /* an unsupported format on any bound target refuses the batch */
    mrt.key.cbufs[1].format = 0x7fffu;
    assert(!AgxWin32AsahiBatchFinish(&mrt, &native, NULL) && submit_count == before);
    mrt.key.cbufs[1].format = PIPE_FORMAT_R8G8B8A8_UNORM;
    assert(AgxWin32AsahiBatchFinish(&mrt, &native, NULL));
    assert(submit_count == before + 1 && sub.v5_result == AppleAgxG4ParseOk &&
           !sub.view.HasCompute && sub.view.AttachmentCount == 2u);
    APPLE_AGX_G4_ATTACHMENT a0, a1;
    memcpy(&a0, sub.view.Attachments, sizeof(a0));
    memcpy(&a1, (const unsigned char *)sub.view.Attachments + sizeof(a0), sizeof(a1));
    assert(a0.Pointer == bos[4].view.Va && a1.Pointer == bos[15].view.Va);
    assert(sub.written_count == 2u && written_has(4) && written_has(15));
    assert(AgxWin32AsahiBatchPoll(&mrt, 1000) && AgxWin32AsahiBatchRelease(&mrt));
  }
  puts("D3D-G1 winsys render+compute envelope: PASS");
  return 0;
}
