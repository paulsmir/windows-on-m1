/* G2: the AGX4 compute envelope parser (AppleAgxG4ParseComputeEx). */
#include "apple_agx_g4_submit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned long long Va, Bytes; int Writable; } MAPPING;
typedef struct {
  MAPPING Map[8]; unsigned Count;
  unsigned Calls, Kinds[8];
} SPACE;

static int access_ex(void *context, unsigned long long va, unsigned int bytes,
                     int write, APPLE_AGX_G4_ACCESS_KIND kind, unsigned int ordinal) {
  SPACE *s = context;
  (void)ordinal;
  if (s->Calls < 8u) s->Kinds[s->Calls] = (unsigned)kind;
  ++s->Calls;
  for (unsigned i = 0; i < s->Count; ++i)
    if (va >= s->Map[i].Va && va + bytes <= s->Map[i].Va + s->Map[i].Bytes &&
        (!write || s->Map[i].Writable)) return 1;
  return 0;
}

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_COMPUTE Header;
  APPLE_AGX_G4_NATIVE_HEADER Native;
  APPLE_AGX_G4_NATIVE_COMPUTE Compute;
} ENVELOPE;

#define DMA_VA 0x100000ULL
#define PREEMPT 0x7fa0u

static void make(ENVELOPE *e, SPACE *s) {
  APPLE_AGX_G4_PROCESS_RANGE preemption = {0x300000ULL, 0x10000u, 0u};
  memset(e, 0, sizeof(*e));
  assert(AppleAgxG4ComposeHeaderCompute(&e->Header, DMA_VA,
      sizeof(e->Native) + sizeof(e->Compute), &preemption));
  e->Native.Type = APPLE_AGX_G4_COMPUTE;
  e->Native.Size = sizeof(e->Compute);
  e->Compute.CdmCtrlStreamBase = 0x200000ULL;
  e->Compute.CdmCtrlStreamEnd = 0x200040ULL;
  memset(s, 0, sizeof(*s));
  s->Map[0] = (MAPPING){DMA_VA, 0x1000, 0};
  s->Map[1] = (MAPPING){0x200000ULL, 0x10000, 0};
  s->Map[2] = (MAPPING){0x300000ULL, 0x10000, 1};
  s->Map[3] = (MAPPING){0x400000ULL, 0x10000, 0};
  s->Count = 4;
}

static APPLE_AGX_G4_PARSE_RESULT parse(ENVELOPE *e, SPACE *s,
                                       APPLE_AGX_G4_COMPUTE_VIEW *view,
                                       APPLE_AGX_G4_FAILURE *failure) {
  s->Calls = 0;
  return AppleAgxG4ParseComputeEx(e, sizeof(*e), sizeof(*e), DMA_VA,
      sizeof(e->Native) + sizeof(e->Compute), PREEMPT, access_ex, s, view, failure);
}

int main(void) {
  ENVELOPE e; SPACE s; APPLE_AGX_G4_COMPUTE_VIEW v; APPLE_AGX_G4_FAILURE f;
  /* valid */
  make(&e, &s);
  assert(AppleAgxG4IsCompute(&e, sizeof(e)));
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseOk);
  assert(v.Compute.CdmCtrlStreamBase == 0x200000ULL &&
         v.Compute.CdmCtrlStreamEnd == 0x200040ULL &&
         v.Preemption.Va == 0x300000ULL && v.CommandVa == DMA_VA &&
         v.CommandBytes == sizeof(e.Native) + sizeof(e.Compute) &&
         v.Native == (const unsigned char *)&e.Native);
  assert(s.Calls == 3u && s.Kinds[0] == AppleAgxG4AccessCpuEnvelope &&
         s.Kinds[1] == AppleAgxG4AccessComputeStream &&
         s.Kinds[2] == AppleAgxG4AccessComputePreemption);
  /* samplers need a mapped heap */
  e.Compute.SamplerCount = 4u;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  e.Compute.SamplerHeap = 0x400000ULL;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseOk && s.Calls == 4u &&
         s.Kinds[2] == AppleAgxG4AccessSamplerHeap);
  e.Compute.SamplerHeap = 0x500000ULL;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseUnmapped &&
         f.Kind == AppleAgxG4AccessSamplerHeap && f.Va == 0x500000ULL);
  e.Compute.SamplerCount = APPLE_AGX_G4_MAX_SAMPLERS + 1u;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  /* header */
  make(&e, &s); e.Header.Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA;
  assert(!AppleAgxG4IsCompute(&e, sizeof(e)) && parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Header.Base.Magic ^= 1u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Header.Base.HeaderBytes = 24u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Header.Base.Reserved = 1u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Header.Base.CommandVa += 0x1000ULL; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Header.Base.CommandBytes = 8u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  /* native framing */
  make(&e, &s); e.Native.Type = APPLE_AGX_G4_RENDER; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseUnsupported);
  make(&e, &s); e.Native.Size = 48u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Native.CdmBarrier = 0xffffu; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Native.VdmBarrier = 1u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  /* payload */
  make(&e, &s); e.Compute.Flags = 1u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  /* EXP1339: a helper (compute scratch) is a mapped USC program with its
   * mapped argument; its configuration only with a program, and may be 0. */
  make(&e, &s); e.Compute.Helper.Binary = 0x100u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.Helper.Data = 0x400000ULL; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.Helper.Cfg = 1u << 16; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.Helper.Binary = 0x100u; e.Compute.Helper.Data = 0x400000ULL;
  s.Map[4] = (MAPPING){APPLE_AGX_G4_USC_EXECUTION_BASE, 0x1000, 0}; s.Count = 5;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseOk && s.Calls == 5u &&
         s.Kinds[2] == AppleAgxG4AccessComputeStream && s.Kinds[3] == AppleAgxG4AccessComputeStream &&
         v.Compute.Helper.Binary == 0x100u && v.Compute.Helper.Data == 0x400000ULL);
  e.Compute.Helper.Cfg = 2u << 16; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseOk);
  e.Compute.Helper.Data = 0x500000ULL;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseUnmapped && f.Kind == AppleAgxG4AccessComputeStream &&
         f.Va == 0x500000ULL);
  e.Compute.Helper.Data = 0x400000ULL; s.Count = 4;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseUnmapped &&
         f.Va == APPLE_AGX_G4_USC_EXECUTION_BASE + 0x100u);
  /* GPU timestamps: a request per end (handle 1, offsets 0). */
  make(&e, &s); e.Compute.Timestamps.StartHandle = APPLE_AGX_G4_TIMESTAMP_REQUEST;
  e.Compute.Timestamps.EndHandle = APPLE_AGX_G4_TIMESTAMP_REQUEST;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseOk &&
         v.Compute.Timestamps.StartHandle == 1u && v.Compute.Timestamps.EndHandle == 1u);
  make(&e, &s); e.Compute.Timestamps.EndHandle = APPLE_AGX_G4_TIMESTAMP_REQUEST;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseOk);
  make(&e, &s); e.Compute.Timestamps.StartHandle = 2u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.Timestamps.StartHandle = 1u; e.Compute.Timestamps.StartOffset = 8u;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.Timestamps.EndOffset = 8u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.CdmCtrlStreamEnd = e.Compute.CdmCtrlStreamBase; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.CdmCtrlStreamBase += 2ULL; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.CdmCtrlStreamEnd = e.Compute.CdmCtrlStreamBase + APPLE_AGX_G4_CDM_STREAM_MAX_BYTES + 4ULL;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Compute.CdmCtrlStreamBase = 0x600000ULL; e.Compute.CdmCtrlStreamEnd = 0x600010ULL;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseUnmapped && f.Kind == AppleAgxG4AccessComputeStream &&
         f.Va == 0x600000ULL && f.Bytes == 0x10u && !f.Write);
  /* preemption buffer */
  make(&e, &s);
  assert(AppleAgxG4ParseComputeEx(&e, sizeof(e), sizeof(e), DMA_VA,
      sizeof(e.Native) + sizeof(e.Compute), 0x20000u, access_ex, &s, &v, &f) == AppleAgxG4ParseInvalid);
  assert(AppleAgxG4ParseComputeEx(&e, sizeof(e), sizeof(e), DMA_VA,
      sizeof(e.Native) + sizeof(e.Compute), 0u, access_ex, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Header.Preemption.Va = 0x200000ULL; /* the stream */
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Header.Preemption.Va = 0x300800ULL; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); e.Header.Preemption.Reserved = 1u; assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  make(&e, &s); s.Map[2].Writable = 0;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseUnmapped && f.Kind == AppleAgxG4AccessComputePreemption && f.Write);
  /* composer refuses an overlapping or unaligned buffer */
  {
    APPLE_AGX_G4_PRIVATE_HEADER_COMPUTE h;
    APPLE_AGX_G4_PROCESS_RANGE r = {DMA_VA, 0x10000u, 0u};
    assert(!AppleAgxG4ComposeHeaderCompute(&h, DMA_VA, 72u, &r));
    r.Va = 0x308000ULL; assert(!AppleAgxG4ComposeHeaderCompute(&h, DMA_VA, 72u, &r));
  }
  puts("agx4 compute envelope: PASS");
  return 0;
}
