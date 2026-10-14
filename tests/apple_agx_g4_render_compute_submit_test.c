/* D3D-G1: the AGX4 render envelope whose submission starts with a compute
 * command (Asahi agx_batch_submit: COMPUTE {vdm 0, cdm 0}, attachments,
 * RENDER {vdm NONE, cdm 1}), parsed by AppleAgxG4ParseSubmitComputeEx. */
#include "apple_agx_g4_submit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned long long Va, Bytes; int Writable; } MAPPING;
typedef struct { MAPPING Map[16]; unsigned Count, Calls; } SPACE;

static int access_ex(void *context, unsigned long long va, unsigned int bytes,
                     int write, APPLE_AGX_G4_ACCESS_KIND kind, unsigned int ordinal) {
  SPACE *s = context;
  (void)kind; (void)ordinal;
  ++s->Calls;
  for (unsigned i = 0; i < s->Count; ++i)
    if (va >= s->Map[i].Va && va + bytes <= s->Map[i].Va + s->Map[i].Bytes &&
        (!write || s->Map[i].Writable)) return 1;
  return 0;
}

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V5 Header;
  APPLE_AGX_G4_NATIVE_HEADER ComputeHeader;
  APPLE_AGX_G4_NATIVE_COMPUTE Compute;
  APPLE_AGX_G4_NATIVE_HEADER AttachmentHeader;
  APPLE_AGX_G4_ATTACHMENT Attachment;
  APPLE_AGX_G4_NATIVE_HEADER RenderHeader;
  APPLE_AGX_G4_NATIVE_RENDER Render;
} PACKET;

#define DMA_VA 0x50000ULL
#define PREEMPT 0x7fa0u
#define DMA_BYTES ((unsigned)(sizeof(PACKET) - sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V5)))

static void make(PACKET *p, SPACE *s) {
  APPLE_AGX_G4_PRIVATE_HEADER_V3 v3;
  APPLE_AGX_G4_PROCESS_RANGE preemption = {0x3000000ULL, 0x10000u, 0u};
  memset(p, 0, sizeof(*p));
  memset(&v3, 0, sizeof(v3));
  v3.V2.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  v3.V2.Base.Reserved = APPLE_AGX_G4_COLOR_BGRA8;
  for (unsigned i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    v3.V2.Process[i].Va = 0x100000ULL + i * 0x400000ULL;
    v3.V2.Process[i].Bytes = 0x10000u;
  }
  v3.V2.Process[2].Bytes = 0x400000u;
  v3.V2.Process[3].Bytes = 0x20000u;
  v3.Lease.ManagerId = 1u; v3.Lease.ManagerGeneration = 2u;
  v3.Lease.SceneId = 3u; v3.Lease.SceneGeneration = 4u;
  assert(AppleAgxG4ComposeHeaderV5(&p->Header, &v3, DMA_VA, DMA_BYTES, &preemption));
  p->ComputeHeader.Type = APPLE_AGX_G4_COMPUTE;
  p->ComputeHeader.Size = sizeof(p->Compute);
  p->Compute.CdmCtrlStreamBase = 0x4000000ULL;
  p->Compute.CdmCtrlStreamEnd = 0x4000040ULL;
  p->AttachmentHeader.Type = APPLE_AGX_G4_FRAGMENT_ATTACHMENTS;
  p->AttachmentHeader.Size = sizeof(p->Attachment);
  p->AttachmentHeader.VdmBarrier = 0xffffu;
  p->AttachmentHeader.CdmBarrier = 0xffffu;
  p->Attachment.Pointer = 0x30000ULL;
  p->Attachment.Size = 0x4000ULL;
  p->RenderHeader.Type = APPLE_AGX_G4_RENDER;
  p->RenderHeader.Size = sizeof(p->Render);
  p->RenderHeader.VdmBarrier = 0xffffu;
  p->RenderHeader.CdmBarrier = 1u;
  p->Render.VdmCtrlStreamBase = 0x40000ULL;
  p->Render.WidthPx = 32u; p->Render.HeightPx = 32u; p->Render.Layers = 1u;
  p->Render.UtileWidthPx = 32u; p->Render.UtileHeightPx = 32u;
  p->Render.Samples = 1u; p->Render.SampleSizeBytes = 4u;
  memset(s, 0, sizeof(*s));
  s->Map[s->Count++] = (MAPPING){DMA_VA, 0x10000, 0};          /* envelope */
  s->Map[s->Count++] = (MAPPING){0x30000ULL, 0x4000, 1};      /* attachment */
  s->Map[s->Count++] = (MAPPING){0x40000ULL, 0x1000, 0};      /* VDM stream */
  s->Map[s->Count++] = (MAPPING){0x100000ULL, 0x2400000, 1};  /* process ranges */
  s->Map[s->Count++] = (MAPPING){0x3000000ULL, 0x10000, 1};   /* preemption */
  s->Map[s->Count++] = (MAPPING){0x4000000ULL, 0x10000, 0};   /* CDM stream */
}

static APPLE_AGX_G4_PARSE_RESULT parse(PACKET *p, SPACE *s, APPLE_AGX_G4_SUBMIT_VIEW *v) {
  APPLE_AGX_G4_FAILURE f;
  s->Calls = 0;
  return AppleAgxG4ParseSubmitComputeEx(p, sizeof(*p), sizeof(*p), DMA_VA, DMA_BYTES,
                                        PREEMPT, access_ex, s, v, &f);
}

int main(void) {
  PACKET p, bad; SPACE s; APPLE_AGX_G4_SUBMIT_VIEW v; APPLE_AGX_G4_FAILURE f;
  make(&p, &s);
  assert(!AppleAgxG4IsCompute(&p, sizeof(p)));
  assert(parse(&p, &s, &v) == AppleAgxG4ParseOk);
  assert(v.HasCompute && v.Compute.CdmCtrlStreamBase == 0x4000000ULL &&
         v.Compute.CdmCtrlStreamEnd == 0x4000040ULL &&
         v.Preemption.Va == 0x3000000ULL && v.Preemption.Bytes == 0x10000u);
  assert(v.Render == (const unsigned char *)&p.Render && v.AttachmentCount == 1u &&
         v.CommandBytes == DMA_BYTES && v.Lease.SceneId == 3u &&
         v.Process[0].Va == 0x100000ULL);
  /* Without attachments the render follows the compute directly. */
  {
    struct {
      APPLE_AGX_G4_PRIVATE_HEADER_V5 Header;
      APPLE_AGX_G4_NATIVE_HEADER ComputeHeader;
      APPLE_AGX_G4_NATIVE_COMPUTE Compute;
      APPLE_AGX_G4_NATIVE_HEADER RenderHeader;
      APPLE_AGX_G4_NATIVE_RENDER Render;
    } q;
    unsigned bytes = (unsigned)(sizeof(q) - sizeof(q.Header));
    memset(&q, 0, sizeof(q));
    q.Header = p.Header;
    q.Header.V3.V2.Base.CommandBytes = bytes;
    q.ComputeHeader = p.ComputeHeader; q.Compute = p.Compute;
    q.RenderHeader = p.RenderHeader; q.Render = p.Render;
    s.Calls = 0;
    assert(AppleAgxG4ParseSubmitComputeEx(&q, sizeof(q), sizeof(q), DMA_VA, bytes,
        PREEMPT, access_ex, &s, &v, &f) == AppleAgxG4ParseOk && v.HasCompute &&
        v.AttachmentCount == 0u);
  }
  /* The render must wait on the compute command (cdm 1, vdm NONE). */
  bad = p; bad.RenderHeader.CdmBarrier = 0u;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  bad = p; bad.RenderHeader.VdmBarrier = 0u;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  bad = p; bad.RenderHeader.CdmBarrier = 2u;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  /* The compute barriers on the previous submission (0, 0). */
  bad = p; bad.ComputeHeader.VdmBarrier = 0xffffu;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  bad = p; bad.ComputeHeader.Size++;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  /* A v5 envelope must start with its compute command. */
  bad = p; bad.ComputeHeader.Type = APPLE_AGX_G4_RENDER;
  assert(parse(&bad, &s, &v) != AppleAgxG4ParseOk);
  /* A second compute in place of the render is not a render. */
  bad = p; bad.RenderHeader.Type = APPLE_AGX_G4_COMPUTE;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseUnsupported);
  /* Compute validation as for a standalone compute envelope. */
  bad = p; bad.Compute.Helper.Binary = 1u;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseUnsupported);
  bad = p; bad.Compute.Flags = 1u;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  bad = p; bad.Compute.CdmCtrlStreamEnd = bad.Compute.CdmCtrlStreamBase;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  bad = p; bad.Compute.CdmCtrlStreamBase = 0x5000000ULL;
  bad.Compute.CdmCtrlStreamEnd = 0x5000040ULL;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseUnmapped);
  bad = p; bad.Header.Preemption.Bytes = 0u;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  s.Map[4].Writable = 0;
  assert(parse(&p, &s, &v) == AppleAgxG4ParseUnmapped);
  s.Map[4].Writable = 1;
  /* The render keeps every v3 rule. */
  bad = p; bad.Header.V3.Lease.SceneId = 0u;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  bad = p; bad.Render.WidthPx = 0u;
  assert(parse(&bad, &s, &v) == AppleAgxG4ParseInvalid);
  /* The plain v3 entry point does not admit a v5 envelope. */
  assert(AppleAgxG4ParseSubmitEx(&p, sizeof(p), sizeof(p), DMA_VA, DMA_BYTES,
      access_ex, &s, &v, &f) == AppleAgxG4ParseUnsupported);
  /* The v5 entry point needs the KMD's preemption requirement. */
  assert(AppleAgxG4ParseSubmitComputeEx(&p, sizeof(p), sizeof(p), DMA_VA, DMA_BYTES,
      0u, access_ex, &s, &v, &f) == AppleAgxG4ParseInvalid);
  /* A v3 render through the v5 entry point: no compute. */
  {
    struct {
      APPLE_AGX_G4_PRIVATE_HEADER_V3 Header;
      APPLE_AGX_G4_NATIVE_HEADER RenderHeader;
      APPLE_AGX_G4_NATIVE_RENDER Render;
    } r;
    unsigned bytes = (unsigned)(sizeof(r) - sizeof(r.Header));
    memset(&r, 0, sizeof(r));
    r.Header = p.Header.V3;
    r.Header.V2.Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA;
    r.Header.V2.Base.HeaderBytes = sizeof(r.Header);
    r.Header.V2.Base.CommandBytes = bytes;
    r.RenderHeader = p.RenderHeader;
    r.RenderHeader.VdmBarrier = 0u; r.RenderHeader.CdmBarrier = 0u;
    r.Render = p.Render;
    s.Calls = 0;
    assert(AppleAgxG4ParseSubmitComputeEx(&r, sizeof(r), sizeof(r), DMA_VA, bytes,
        PREEMPT, access_ex, &s, &v, &f) == AppleAgxG4ParseOk && !v.HasCompute);
  }
  puts("agx4 render+compute envelope: PASS");
  return 0;
}
