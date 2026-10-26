/* MIT: clock-independent collector, shared by runtime and host replay. */
#ifndef ADMISSION_UMD_WAIT_PROFILE_H
#define ADMISSION_UMD_WAIT_PROFILE_H
#include "../../../mesa/winsys/agx_win32_wait.h"
#include <string.h>
#define AGX_WAIT_STACK 64u
#define AGX_WAIT_KEYS (AgxWaitPhaseCount * AgxWaitSiteCount)
typedef struct {
  unsigned Active, Serial, Phase, Depth, Flags;
  unsigned long long Frequency;
  struct { unsigned Key; unsigned long long Start, Children; } Stack[AGX_WAIT_STACK];
  struct { unsigned Count; unsigned long long Ticks, Inclusive; } Sites[AGX_WAIT_KEYS], Query[3];
} AgxWaitProfile;
static inline void AgxWaitReset(AgxWaitProfile *p, unsigned serial,
                                unsigned long long hz) {
  memset(p,0,sizeof(*p)); p->Active=1; p->Serial=serial; p->Frequency=hz;
}
static inline unsigned AgxWaitPush(AgxWaitProfile *p, unsigned site,
                                  unsigned long long now) {
  if (!p->Active) return 0;
  if (!site || site>=AgxWaitSiteCount || p->Phase>=AgxWaitPhaseCount ||
      p->Depth>=AGX_WAIT_STACK) { p->Flags|=1; return 0; }
  p->Stack[p->Depth].Key=p->Phase*AgxWaitSiteCount+site;
  p->Stack[p->Depth].Start=now; p->Stack[p->Depth].Children=0;
  return ++p->Depth;
}
static inline void AgxWaitPop(AgxWaitProfile *p, unsigned token,
                              unsigned long long now) {
  unsigned key, phase;
  unsigned long long elapsed, exclusive;
  if (!token || !p->Active) return;
  if (token!=p->Depth) { p->Flags|=1; return; }
  --p->Depth;
  if (now<p->Stack[p->Depth].Start) { p->Flags|=1; return; }
  elapsed=now-p->Stack[p->Depth].Start;
  key=p->Stack[p->Depth].Key; phase=key/AgxWaitSiteCount;
  exclusive=elapsed>=p->Stack[p->Depth].Children ? elapsed-p->Stack[p->Depth].Children : 0;
  if (p->Depth) p->Stack[p->Depth-1].Children+=elapsed;
  if (p->Sites[key].Count!=0xffffffffu) ++p->Sites[key].Count;
  else p->Flags|=2;
  p->Sites[key].Ticks+=exclusive;
  p->Sites[key].Inclusive+=elapsed;
  if (key%AgxWaitSiteCount==AgxWaitQueryCommand &&
      phase>=AgxWaitPhaseQueryBegin && phase<=AgxWaitPhaseQueryResolve) {
    if (p->Query[phase-1].Count!=0xffffffffu) ++p->Query[phase-1].Count;
    else p->Flags|=2;
    p->Query[phase-1].Ticks+=elapsed;
  }
}
static inline unsigned AgxWaitUs(AgxWaitProfile *p, unsigned long long ticks) {
  unsigned long long seconds, us;
  if (!p->Frequency) { p->Flags|=1; return 0; }
  seconds=ticks/p->Frequency;
  if (seconds>4294u) { p->Flags|=2; return 0xffffffffu; }
  /* QPC frequency fits signed 64 bits. Floating remainder avoids overflow
   * for synthetic clocks as well as real QPC. Integer seconds stay exact. */
  us=seconds*1000000u+(unsigned long long)
      ((double)(ticks%p->Frequency)*1000000.0/(double)p->Frequency);
  if (us>0xffffffffu) { p->Flags|=2; return 0xffffffffu; }
  return (unsigned)us;
}
static inline int AgxWaitIsPath(unsigned key) {
  switch(key%AgxWaitSiteCount) {
  case AgxWaitInflightSlot: case AgxWaitBatchSlot: case AgxWaitSyncWriter:
  case AgxWaitSyncReaders: case AgxWaitFlush: case AgxWaitStagingBlit:
  case AgxWaitTransferMap: case AgxWaitQueryCreate: case AgxWaitQueryIncrement:
  case AgxWaitQueryWriters: case AgxWaitContextRetire: case AgxWaitCountMap: return 1;
  default: return 0;
  }
}
static inline unsigned long long AgxWaitTicks(const AgxWaitProfile *p,
                                              unsigned key, int inclusive) {
  return inclusive ? p->Sites[key].Inclusive : p->Sites[key].Ticks;
}
static inline void AgxWaitPackSites(AgxWaitProfile *p, unsigned out[16], int inclusive) {
  unsigned top[4]={0}, i,j;
  unsigned long long omitted=0;
  unsigned truncated=0;
  memset(out,0,16*sizeof(*out));
  for (i=1;i<AGX_WAIT_KEYS;++i) {
    if (!p->Sites[i].Count || (inclusive && !AgxWaitIsPath(i))) continue;
    unsigned candidate=i;
    for(j=0;j<4;++j) {
      if (!top[j] || AgxWaitTicks(p,candidate,inclusive)>AgxWaitTicks(p,top[j],inclusive)) {
        unsigned old=top[j];top[j]=candidate;candidate=old;
        if (!candidate) break;
      }
    }
    if(candidate && j==4) { omitted+=AgxWaitTicks(p,candidate,inclusive); truncated=4; }
  }
  out[0]=1;out[1]=p->Serial;out[2]=AgxWaitUs(p,omitted);
  for(j=0;j<4;++j) if(top[j]) {
    i=top[j]; out[4+j*3]=(i/AgxWaitSiteCount)*256+i%AgxWaitSiteCount;
    out[5+j*3]=p->Sites[i].Count; out[6+j*3]=AgxWaitUs(p,AgxWaitTicks(p,i,inclusive));
  }
  if(p->Depth) p->Flags|=1;
  out[3]=p->Flags|truncated;
}
static inline void AgxWaitPack(AgxWaitProfile *p, unsigned out[16], unsigned query[8]) {
  unsigned j;
  memset(query,0,8*sizeof(*query));query[0]=1;query[1]=p->Serial;
  for(j=0;j<3;++j) { query[2+j*2]=p->Query[j].Count;query[3+j*2]=AgxWaitUs(p,p->Query[j].Ticks); }
  AgxWaitPackSites(p,out,0);
}
#endif
