#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d10umddi.h>
#pragma warning(pop)
extern "C" {
#include "umd_internal.h"
}
#include <assert.h>
#include <stdio.h>
VOID AdmissionUmdDiagnostic(PCSTR,HRESULT,const UINT *,UINT) {}
#include "r145_screen_under_test.inc"

struct Allocation { UINT CpuVisible; BYTE *Data; bool Live; };
static Allocation Allocations[64];
static UINT NextHandle, Creates, Locks, Frees, FailCreate, FailFreeHandle;
static HRESULT APIENTRY Allocate(HANDLE,D3DDDICB_ALLOCATE *q) {
  assert(q->NumAllocations==1);
  ++Creates;if(Creates==FailCreate) return E_OUTOFMEMORY;
  const D3DDDI_ALLOCATIONINFO *a=q->pAllocationInfo;
  const ADMISSION_ALLOCATION_DESCRIPTION *d;
  if(a->PrivateDriverDataSize==sizeof(ADMISSION_WIN32_ALLOCATION_CREATE)) {
    const auto *c=(const ADMISSION_WIN32_ALLOCATION_CREATE *)a->pPrivateDriverData;
    d=&c->Allocation;
  } else { assert(a->PrivateDriverDataSize==sizeof(*d));d=(const ADMISSION_ALLOCATION_DESCRIPTION *)a->pPrivateDriverData; }
  assert(NextHandle+1<64);
  UINT h=++NextHandle;
  Allocations[h]={d->CpuVisible,(BYTE *)calloc(1,(size_t)d->Size),true};
  assert(Allocations[h].Data);
  q->pAllocationInfo[0].hAllocation=h;return S_OK;
}
static HRESULT APIENTRY Lock(HANDLE,D3DDDICB_LOCK *q) {
  assert(q->hAllocation<64 && Allocations[q->hAllocation].Live);
  assert(Allocations[q->hAllocation].CpuVisible==1);++Locks;
  q->pData=Allocations[q->hAllocation].Data;return S_OK;
}
static HRESULT APIENTRY Unlock(HANDLE,const D3DDDICB_UNLOCK *q) {
  for(UINT i=0;i<q->NumAllocations;++i) assert(Allocations[q->phAllocations[i]].CpuVisible==1);
  return S_OK;
}
static HRESULT APIENTRY Free(HANDLE,const D3DDDICB_DEALLOCATE *q) {
  if(q->HandleList[0]==FailFreeHandle) return E_FAIL;
  for(UINT i=0;i<q->NumAllocations;++i){auto &a=Allocations[q->HandleList[i]];assert(a.Live);a.Live=false;free(a.Data);a.Data=NULL;++Frees;}
  return S_OK;
}
int main(void) {
  auto *d=(ADMISSION_UMD_DEVICE *)calloc(1,sizeof(ADMISSION_UMD_DEVICE));
  ADMISSION_UMD_ADAPTER adapter={};D3DDDI_DEVICECALLBACKS cb={};
  d->Magic=ADMISSION_UMD_DEVICE_MAGIC;d->Adapter=&adapter;d->KernelCallbacks=&cb;
  cb.pfnAllocateCb=Allocate;cb.pfnLockCb=Lock;cb.pfnUnlockCb=Unlock;cb.pfnDeallocateCb=Free;
  adapter.DeviceInfo.ClassCount=3;
  for(UINT i=0;i<3;++i) adapter.DeviceInfo.Classes[i]={i+1,65536,16ULL<<20,15};
  InitializeSRWLock(&d->ScreenBufferLock);
  for(UINT cls=1;cls<=3;++cls) {
    UINT before=NextHandle;APPLE_AGX_U64 token=0;
    assert(AdmissionUmdScreenCreateClassBuffer(d,cls,65536*cls,65536,6,&token));
    assert(NextHandle==before+2);
    assert(Allocations[before+1].CpuVisible==0 && Allocations[before+2].CpuVisible==1);
    void *mapped=NULL;
    assert(AdmissionUmdScreenMapBuffer(d,token,19,37,2,&mapped));
    assert(mapped==Allocations[before+2].Data+19);
    memset(mapped,0x91,37);
    auto *slot=AdmissionUmdScreenFind(d,token);
    slot->SubmissionHolds=1;
    assert(!AdmissionUmdScreenUnmapBuffer(d,token));
    assert(!AdmissionUmdScreenDestroyBuffer(d,token));
    slot->SubmissionHolds=0;
    assert(AdmissionUmdScreenUnmapBuffer(d,token));
    slot->SubmissionHolds=1;
    assert(!AdmissionUmdScreenMapBuffer(d,token,0,16,2,&mapped));
    slot->SubmissionHolds=0;
    assert(AdmissionUmdScreenDestroyBuffer(d,token));
    assert(!Allocations[before+1].Live && !Allocations[before+2].Live);
  }
  /* Failed second allocation must roll back the first. */
  UINT before=NextHandle;FailCreate=Creates+2;APPLE_AGX_U64 token=0;
  assert(!AdmissionUmdScreenCreateClassBuffer(d,1,65536,65536,15,&token));
  assert(!Allocations[before+1].Live && !token);FailCreate=0;
  /* Imported CPU storage is borrowed; only the local execution handle is owned. */
  UINT imported=++NextHandle;Allocations[imported]={1,(BYTE *)calloc(1,65536),true};
  AGX_WIN32_SCREEN_BUFFER buffer={};before=NextHandle;
  assert(SUCCEEDED(AdmissionUmdScreenAdoptAllocation(d,imported,65024,65536,1,15,TRUE,FALSE,&buffer)));
  assert(NextHandle==before+1 && Allocations[before+1].CpuVisible==0);
  AGX_WIN32_SCREEN_BUFFER alias={};
  assert(FAILED(AdmissionUmdScreenAdoptAllocation(d,imported,65024,65536,1,15,TRUE,FALSE,&alias)));
  assert(!alias.Transport.Token && NextHandle==before+1);
  void *mapped=NULL;assert(AdmissionUmdScreenMapBuffer(d,buffer.Transport.Token,0,65024,3,&mapped));
  assert(mapped==Allocations[imported].Data);
  assert(AdmissionUmdScreenUnmapBuffer(d,buffer.Transport.Token));
  assert(AdmissionUmdScreenDestroyBuffer(d,buffer.Transport.Token));
  assert(Allocations[imported].Live && !Allocations[before+1].Live);
  /* R158: a non-CPU-visible presentation allocation is rendered directly:
   * no new allocation, no CPU map, and destroy never frees the borrowed one. */
  { AGX_WIN32_SCREEN_BUFFER direct={},direct_alias={}; UINT prior=NextHandle;
    assert(SUCCEEDED(AdmissionUmdScreenAdoptAllocation(d,imported,65024,65536,1,15,TRUE,TRUE,&direct)));
    assert(NextHandle==prior);
    auto *ds=AdmissionUmdScreenFind(d,direct.Transport.Token);
    assert(ds && ds->Direct && ds->Borrowed && ds->KernelAllocation==imported && !ds->StagingAllocation);
    assert(FAILED(AdmissionUmdScreenAdoptAllocation(d,imported,65024,65536,1,15,TRUE,TRUE,&direct_alias)));
    void *m=NULL; assert(!AdmissionUmdScreenMapBuffer(d,direct.Transport.Token,0,16,3,&m));
    assert(AdmissionUmdScreenDestroyBuffer(d,direct.Transport.Token));
    assert(Allocations[imported].Live && !AdmissionUmdScreenFind(d,direct.Transport.Token)); }
  free(Allocations[imported].Data);
  /* A failed second release retains its exact ownership; retry only that handle. */
  before=NextHandle;assert(AdmissionUmdScreenCreateClassBuffer(d,1,65536,65536,15,&token));
  FailFreeHandle=before+2;
  assert(!AdmissionUmdScreenDestroyBuffer(d,token));
  auto *retained=AdmissionUmdScreenFind(d,token);
  assert(retained && !retained->KernelAllocation && retained->StagingAllocation==before+2);
  assert(!Allocations[before+1].Live && Allocations[before+2].Live);
  FailFreeHandle=0;assert(AdmissionUmdScreenDestroyBuffer(d,token));
  /* Failed rollback cannot drop the canonical handle or reuse its slot. */
  before=NextHandle;FailCreate=Creates+2;FailFreeHandle=before+1;
  assert(!AdmissionUmdScreenCreateClassBuffer(d,1,65536,65536,15,&token));
  assert(!token && Allocations[before+1].Live && d->DrawTerminal);
  bool found=false;
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(d->ScreenBuffers[i].Active && d->ScreenBuffers[i].KernelAllocation==before+1) found=true;
  assert(found); free(Allocations[before+1].Data);free(d);
  puts("R145 screen allocation/staging/import/rollback: PASS");
}
