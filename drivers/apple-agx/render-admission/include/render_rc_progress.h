#ifndef APPLE_AGX_RENDER_RC_PROGRESS_H
#define APPLE_AGX_RENDER_RC_PROGRESS_H

/* Q6 v2: bounded read-only diagnostic. All wire fields are little-endian U32;
 * no pointers, native alignment, CRT, or firmware writes in this record.
 * Words: threshold before, successor FIRST-queue FW before, predecessor
 * LAST-queue FW, predecessor driver, TA FW,
 * TA wptr/done, 3D wptr/done, compute wptr/done, successor FIRST-queue FW after,
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
  /* v2 append: ADMISSION_JOB_WINDOW_KIND/BARRIER values. R/C-named fields
   * above denote predecessor/successor. ExpectedC is the FIRST-queue target,
   * including compute prelude or TA, never a later 3D completion target. */
  unsigned int RunningKind, NextKind, Barrier;
} ADMISSION_RC_SAMPLE;
typedef struct _ADMISSION_RC_RECEIPT {
  unsigned int Version, Bytes, Count, Overflow;
  ADMISSION_RC_SAMPLE Samples[ADMISSION_RC_SAMPLES];
} ADMISSION_RC_RECEIPT;

/* Q4 decisions are observational: 0 allowed, 1..65 existing policy,
 * 66 no pending head, 67 running image not released, 68 prebind not ready
 * (Detail = prebind refusal), 69 backend already has/no longer needs a kick,
 * 70 kick disabled, 71 unknown future reason. Rings retain bounded semantic
 * identities; histogram counts every observation, including evicted identities. */
#define ADMISSION_RC_REASONS 72u
#define ADMISSION_RC_DECISIONS_COUNT 128u
#define ADMISSION_RC_IDLE_COUNT 32u
typedef struct _ADMISSION_RC_DECISION {
  unsigned int Reason, Detail, RFence, CFence, ProcessId, Count;
  unsigned int FirstLow, FirstHigh, LastLow, LastHigh;
  unsigned int RunningKind, NextKind, Barrier;
  /* v4: RFence/CFence are the first observed pair, these are the last.
   * Intermediate pairs are not retained and counts are interval totals. */
  unsigned int LastRFence, LastCFence;
} ADMISSION_RC_DECISION;
typedef struct _ADMISSION_RC_DECISIONS {
  unsigned int Version, Bytes, Count, Next, Overwritten;
  unsigned int Histogram[ADMISSION_RC_REASONS];
  ADMISSION_RC_DECISION Records[ADMISSION_RC_DECISIONS_COUNT];
  /* v3 partition retained in v4: idle polls cannot evict other reasons.
   * v4 coalesces every retained identity regardless of fences or adjacency.
   * Distinct identities can still overflow; overwritten counts those losses. */
  unsigned int IdleCount, IdleNext, IdleOverwritten;
  ADMISSION_RC_DECISION IdleRecords[ADMISSION_RC_IDLE_COUNT];
} ADMISSION_RC_DECISIONS;
/* Storage is zeroed by the owner before initialization. Empty progress is a
 * valid v2 record (Count=0), not an absent registry value. */
static __inline void AdmissionRcInitialize(ADMISSION_RC_RECEIPT *r) {
  r->Version=2u; r->Bytes=(unsigned int)sizeof(*r);
}
static __inline void AdmissionRcDecisionsInitialize(ADMISSION_RC_DECISIONS *r) {
  r->Version=4u; r->Bytes=(unsigned int)sizeof(*r);
}
static __inline void AdmissionRcDecisionTyped(ADMISSION_RC_DECISIONS *r,
    unsigned int reason, unsigned int detail, unsigned int rf,
    unsigned int cf, unsigned int pid, unsigned long long qpc,
    unsigned int running, unsigned int next, unsigned int barrier) {
  ADMISSION_RC_DECISION *s, *records;
  unsigned int *count, *position, *overwritten, capacity, i;
  if(reason>=ADMISSION_RC_REASONS) reason=ADMISSION_RC_REASONS-1u;
  if(r->Histogram[reason]!=~0u) ++r->Histogram[reason];
  records=reason==66u ? r->IdleRecords:r->Records;
  count=reason==66u ? &r->IdleCount:&r->Count;
  position=reason==66u ? &r->IdleNext:&r->Next;
  overwritten=reason==66u ? &r->IdleOverwritten:&r->Overwritten;
  capacity=reason==66u ? ADMISSION_RC_IDLE_COUNT:ADMISSION_RC_DECISIONS_COUNT;
  for(i=0u;i<*count;++i) {
    s=&records[i];
    if(s->Reason==reason && s->Detail==detail &&
       s->ProcessId==pid && s->RunningKind==running &&
       s->NextKind==next && s->Barrier==barrier) {
      if(s->Count!=~0u) ++s->Count;
      s->LastRFence=rf;s->LastCFence=cf;
      s->LastLow=(unsigned int)qpc;s->LastHigh=(unsigned int)(qpc>>32);
      return;
    }
  }
  s=&records[*position];
  s->Reason=reason;s->Detail=detail;s->RFence=s->LastRFence=rf;
  s->CFence=s->LastCFence=cf;
  s->ProcessId=pid;s->Count=1u;
  s->RunningKind=running;s->NextKind=next;s->Barrier=barrier;
  s->FirstLow=s->LastLow=(unsigned int)qpc;
  s->FirstHigh=s->LastHigh=(unsigned int)(qpc>>32);
  *position=(*position+1u)%capacity;
  if(*count<capacity) ++*count;
  else if(*overwritten!=~0u) ++*overwritten;
}

/* Legacy caller has no class evidence. Zero remains explicitly unknown. */
static __inline void AdmissionRcDecision(ADMISSION_RC_DECISIONS *r,
    unsigned int reason, unsigned int detail, unsigned int rf,
    unsigned int cf, unsigned int pid, unsigned long long qpc) {
  AdmissionRcDecisionTyped(r,reason,detail,rf,cf,pid,qpc,0u,0u,0u);
}
/* Stable wire values mirror render_job_window.h; presentation/unknown jobs
 * are not proof classes. Validation does not enable any admission class. */
static __inline unsigned int AdmissionRcTypedPair(unsigned int running,
    unsigned int next, unsigned int barrier) {
  unsigned int render=running==1u || running==2u;
  unsigned int expected;
  if ((!render && running!=3u) || (next!=1u && next!=2u && next!=3u)) return 0u;
  expected=next==1u ? (render ? 2u:1u) : (render ? 3u:4u);
  return barrier==expected;
}

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
 * successor first queue still pending. Neither proves a transition without
 * an earlier negative sample of THIS pair. Missing/overflowed samples yield no experiment PASS. */
static __inline unsigned int AdmissionRcEvidence(const ADMISSION_RC_SAMPLE *s) {
  if(!AdmissionRcTypedPair(s->RunningKind,s->NextKind,s->Barrier) ||
     s->Valid!=0x1fffu || !s->RFence || !s->CFence || !s->ExpectedC ||
     !s->RFlight || s->RFlight>2u || !s->CFlight || s->CFlight>2u ||
     s->RFlight==s->CFlight || s->WaitValue!=s->ExpectedR ||
     (!s->WaitLow && !s->WaitHigh) ||
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
  r->Version=2u;r->Bytes=(unsigned int)sizeof(*r);
  if(r->Count && s->Stage==ADMISSION_RC_POLL) {
    const ADMISSION_RC_SAMPLE *last=&r->Samples[r->Count-1u];
    if(last->Stage==s->Stage && last->RFence==s->RFence && last->CFence==s->CFence &&
       last->Valid==s->Valid && last->CFlags==s->CFlags &&
       last->RunningKind==s->RunningKind && last->NextKind==s->NextKind &&
       last->Barrier==s->Barrier && last->ExpectedR==s->ExpectedR &&
       last->ExpectedC==s->ExpectedC && last->RFlight==s->RFlight &&
       last->CFlight==s->CFlight && last->WaitLow==s->WaitLow &&
       last->WaitHigh==s->WaitHigh && last->WaitValue==s->WaitValue &&
       last->EventCount==s->EventCount && last->PreviousThreshold==s->PreviousThreshold &&
       last->ProcessId==s->ProcessId) {
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
