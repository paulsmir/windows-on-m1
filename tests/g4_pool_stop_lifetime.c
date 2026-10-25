/* Actual Windows stop/free functions plus the production slab allocator.
 * WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Live private leases and
 * failed revocation retain their complete owner; acknowledged ordinary stop
 * frees pages only after the CPU mapping and window aliases are retired. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "render_pool_slab.h"

typedef void VOID;
typedef unsigned ULONG;
typedef unsigned long long ULONGLONG;
typedef long LONG;
typedef int BOOLEAN;
typedef size_t SIZE_T;
typedef struct {long long QuadPart;} LARGE_INTEGER;
#define FALSE 0
#define KernelMode 0
#define ADMISSION_POOL_TAG 1
#define AGX_POOL_WINDOW_CMD_UNMAP_ALL 7
#define AgxPoolWindowOk 0

typedef struct {
  void *PoolDemand, *PoolCpu, *BrokerBase;
  ULONGLONG PoolBytes, PoolIpa;
  LONG PoolLeaks;
} ADMISSION_CONTEXT;
typedef struct {
  LONG Stopping, WorkPending;
  int Timer;
  void *HighMemory, *LowMemory, *Work, *SlotOf, *Candidates, *Granules;
  ADMISSION_POOL_SLAB **SlabOf;
  struct {ULONGLONG Count;} Demand;
  ADMISSION_POOL_SLABS Slabs;
} ADMISSION_POOL_RUNTIME;

static ADMISSION_POOL_RUNTIME *owner;
static unsigned freed_owner, freed_slabs, freed_fields, unmaps, commands;
static unsigned cancelled, flushed, waits, finish_after;
static int broker_result;
static LONG InterlockedExchange(LONG *p, LONG n) {LONG old=*p;*p=n;return old;}
static LONG InterlockedCompareExchange(LONG *p,LONG n,LONG old) {
  LONG r=*p;if(r==old)*p=n;return r;
}
static LONG InterlockedIncrement(LONG *p) {return ++*p;}
static void KeCancelTimer(int *p) {assert(p==&owner->Timer);++cancelled;}
static void KeFlushQueuedDpcs(void) {assert(cancelled);++flushed;}
static int KeDelayExecutionThread(int mode,int alert,LARGE_INTEGER *delay) {
  assert(mode==0 && !alert && delay->QuadPart==-10000 && flushed);
  ++waits;
  if(finish_after && waits==finish_after) owner->WorkPending=0;
  return 0;
}
static void MmUnmapIoSpace(void *p,SIZE_T size) {
  assert(p==(void *)(uintptr_t)0x10000 && size==0x40000);
  assert(!owner->WorkPending && !owner->Slabs.PrivateCount);
  ++unmaps;
}
static int AdmissionPoolWindowCommand(ADMISSION_CONTEXT *ctx,unsigned cmd,
    ULONGLONG ipa,ULONGLONG bytes) {
  assert(ctx->BrokerBase && cmd==AGX_POOL_WINDOW_CMD_UNMAP_ALL && !ipa && !bytes);
  assert(unmaps==1 && !owner->Slabs.PrivateCount);
  ++commands;
  return broker_result;
}
static void ObDereferenceObject(void *p) {assert(p);++freed_fields;free(p);}
static void IoFreeWorkItem(void *p) {assert(p);++freed_fields;free(p);}
static void ExFreePoolWithTag(void *p,unsigned tag) {
  assert(p && tag==ADMISSION_POOL_TAG);
  if(p==owner) {++freed_owner;assert(!owner->Slabs.List);}
  else ++freed_fields;
  free(p);
}
static ADMISSION_POOL_SLAB *NewSlab(void *ctx) {
  assert(ctx==owner);
  ADMISSION_POOL_SLAB *s=calloc(1,sizeof(*s));assert(s);
  for(unsigned i=0;i<32;++i)s->Pa[i]=0x200000ULL+i*0x10000ULL;
  return s;
}
static void FreeSlab(void *ctx,ADMISSION_POOL_SLAB *slab) {
  assert(ctx==owner && !freed_owner && !owner->Slabs.PrivateCount);
  ++freed_slabs;free(slab);
}
#include "stop.inc"

int main(int argc,char **argv) {
  assert(argc==2);
  ADMISSION_CONTEXT ctx={0};
  if(!strcmp(argv[1],"empty")) {AdmissionPoolDemandStop(&ctx);return 0;}
  owner=calloc(1,sizeof(*owner));assert(owner);
  owner->HighMemory=malloc(1);owner->LowMemory=malloc(1);owner->Work=malloc(1);
  owner->SlotOf=malloc(1);owner->Candidates=malloc(1);owner->Granules=malloc(1);
  owner->SlabOf=calloc(4,sizeof(*owner->SlabOf));owner->Demand.Count=4;
  owner->Slabs.Context=owner;owner->Slabs.NewSlab=NewSlab;
  owner->Slabs.FreeSlab=FreeSlab;owner->Slabs.PrivateCap=2;
  owner->Slabs.SpareLimit=1;
  ADMISSION_POOL_PRIVATE lease={0};
  int private=!strcmp(argv[1],"private"), refused=!strcmp(argv[1],"refused");
  int absent=!strcmp(argv[1],"absent"), worker=!strcmp(argv[1],"worker");
  if(private) assert(AdmissionPoolSlabTakePrivate(&owner->Slabs,&lease));
  else {
    unsigned slot;ADMISSION_POOL_SLAB *slab;
    assert(AdmissionPoolSlabTake(&owner->Slabs,1,&slab,&slot));
    owner->SlabOf[0]=slab;
  }
  if(worker || !strcmp(argv[1],"drained_worker"))owner->WorkPending=1;
  if(!strcmp(argv[1],"drained_worker"))finish_after=3;
  ctx.PoolDemand=owner;ctx.PoolCpu=(void *)(uintptr_t)0x10000;
  ctx.PoolBytes=0x40000;ctx.PoolIpa=0x100000;
  ctx.BrokerBase=absent?NULL:(void *)(uintptr_t)1;
  broker_result=refused?1:0;
  AdmissionPoolDemandStop(&ctx);
  assert(ctx.PoolDemand==NULL && cancelled==1 && flushed==1);
  if(private || refused || absent || worker) {
    assert(!freed_owner && !freed_slabs && !freed_fields && ctx.PoolLeaks==1);
    assert(owner->Stopping && owner->Slabs.List);
    if(private || worker)assert(!unmaps && !commands);
    else assert(unmaps==1 && commands==(unsigned)!absent);
    if(worker)assert(waits==10000);
    /* Test cleanup only, after externally retiring all simulated users.
     * The production policy intentionally retains uncertain owners. */
    owner->WorkPending=0;
    if(private)assert(AdmissionPoolSlabGivePrivate(&owner->Slabs,&lease));
    AdmissionPoolSlabDrain(&owner->Slabs);
    AdmissionPoolDemandFree(owner);
  } else {
    assert(freed_owner==1 && freed_slabs==1 && freed_fields==7);
    assert(unmaps==1 && commands==1 && !ctx.PoolLeaks);
    assert(!ctx.PoolCpu && !ctx.PoolIpa && !ctx.PoolBytes);
    if(finish_after)assert(waits==finish_after);
  }
  assert(freed_owner==1 && freed_slabs==1);
  return 0;
}
