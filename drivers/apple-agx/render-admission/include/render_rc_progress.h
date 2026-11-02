#ifndef APPLE_AGX_RENDER_RC_PROGRESS_H
#define APPLE_AGX_RENDER_RC_PROGRESS_H

/* Q3: bounded read-only diagnostic. All wire fields are little-endian U32;
 * no pointers, native alignment, CRT, or firmware writes in this record.
 * Words: threshold before, compute FW before, 3D FW, 3D driver, TA FW,
 * TA wptr/done, 3D wptr/done, compute wptr/done, compute FW after,
 * threshold after. Bracketing avoids calling a moving snapshot overlap. */
#define ADMISSION_RC_SAMPLES 128u
#define ADMISSION_RC_BEFORE_BUILD 1u
#define ADMISSION_RC_BUILT 2u
#define ADMISSION_RC_SUBMITTED 3u
#define ADMISSION_RC_POLL 4u
#define ADMISSION_RC_FAILED 5u
#define ADMISSION_RC_REPORTED 6u

typedef struct _ADMISSION_RC_SAMPLE {
  unsigned int Stage, RFence, CFence, ExpectedR, ExpectedC;
  unsigned int EventCount, PreviousThreshold, RenderCount, ComputeCount;
  unsigned int RFlight, CFlight, RSlot, CSet, CFlags; /* 1 published, 2 visible */
  unsigned int WaitLow, WaitHigh, WaitValue, QpcLow, QpcHigh, ProcessId;
  unsigned int TerminalFence, Status, Valid;
  unsigned int Words[13];
} ADMISSION_RC_SAMPLE;
typedef struct _ADMISSION_RC_RECEIPT {
  unsigned int Version, Bytes, Count, Overflow;
  ADMISSION_RC_SAMPLE Samples[ADMISSION_RC_SAMPLES];
} ADMISSION_RC_RECEIPT;

typedef unsigned char (*ADMISSION_RC_READ)(void *, const volatile unsigned int *, unsigned int *);
static __inline void AdmissionRcRead(ADMISSION_RC_SAMPLE *s,
    const volatile unsigned int *const p[11], ADMISSION_RC_READ read, void *ctx) {
  unsigned int i;
  s->Valid=0u;
  for(i=0;i<13u;++i) {
    unsigned int at=i==11u ? 1u : i==12u ? 0u : i;
    s->Words[i]=0u;
    if(p[at] && read(ctx,p[at],&s->Words[i])) s->Valid|=1u<<i;
  }
}
static __inline unsigned int AdmissionRcReached(unsigned int a,unsigned int b) {
  return b && a-b<0x80000000u;
}
/* 1 = firmware reached during staging; 2 = reached after publication while
 * C still pending. Neither proves a transition without an earlier negative
 * sample of THIS pair. Missing/overflowed samples yield no experiment PASS. */
static __inline unsigned int AdmissionRcEvidence(const ADMISSION_RC_SAMPLE *s) {
  if(s->Valid!=0x1fffu || !s->RFence || !s->CFence || !s->ExpectedC ||
     s->Words[0]!=s->Words[12] ||
     !AdmissionRcReached(s->Words[2],s->ExpectedR) ||
     AdmissionRcReached(s->Words[1],s->ExpectedC) ||
     AdmissionRcReached(s->Words[11],s->ExpectedC) ||
     s->EventCount<=s->PreviousThreshold) return 0u;
  if(!s->CFlags && s->Words[0]==s->PreviousThreshold) return 1u;
  return s->CFlags==3u && s->Words[0]==s->EventCount ? 2u : 0u;
}
static __inline void AdmissionRcAppend(ADMISSION_RC_RECEIPT *r,
                                       const ADMISSION_RC_SAMPLE *s) {
  unsigned int i;
  r->Version=1u;r->Bytes=(unsigned int)sizeof(*r);
  if(r->Count && s->Stage==ADMISSION_RC_POLL) {
    const ADMISSION_RC_SAMPLE *last=&r->Samples[r->Count-1u];
    if(last->Stage==s->Stage && last->RFence==s->RFence && last->CFence==s->CFence &&
       last->Valid==s->Valid && last->CFlags==s->CFlags) {
      for(i=0u;i<13u && last->Words[i]==s->Words[i];++i) {}
      if(i==13u) return;
    }
  }
  if(r->Count==ADMISSION_RC_SAMPLES) { if(r->Overflow!=~0u) ++r->Overflow; return; }
  r->Samples[r->Count++]=*s;
}
/* Q3 enables only the discriminating R->C pair. R/R and preludes require
 * the hardware answer and resident-manager lifetime implementation first. */
static __inline unsigned int AdmissionRcPairAllowed(unsigned int enabled,
    unsigned int running, unsigned int next) {
  return !enabled || (running==1u && next==3u);
}
#endif
