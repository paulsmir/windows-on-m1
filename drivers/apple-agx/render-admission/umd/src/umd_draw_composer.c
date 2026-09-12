#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
extern "C" {
#include "umd_internal.h"
}

static ADMISSION_UMD_SCREEN_BUFFER *find(ADMISSION_UMD_DEVICE *d,
                                        APPLE_AGX_U64 token) {
  for (UINT i=0; i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++i)
    if(d->ScreenBuffers[i].Active && d->ScreenBuffers[i].Token==token)
      return &d->ScreenBuffers[i];
  return NULL;
}
static BOOL identity(ADMISSION_UMD_DEVICE *d, ADMISSION_UMD_SCREEN_BUFFER *b,
                     const AGX_WIN32_RELOC_ALLOCATION *a) {
  return b && !b->Transition && a->Owner && a->Owner==d->OwnerCookie &&
    a->Generation==d->Win32Generation && a->Token==b->Token &&
    a->Serial && a->Serial==b->Serial && a->Bytes==b->Bytes && b->KernelAllocation;
}
static BOOL owns(ADMISSION_UMD_DEVICE *d, ADMISSION_UMD_DRAW_SUBMISSION *s) {
  if(d->DrawSubmission!=s || s->Owner!=d->OwnerCookie ||
     s->Generation!=d->Win32Generation || s->Context!=d->KernelContext)
    return FALSE;
  for(UINT i=0;i<s->Count;++i) {
    ADMISSION_UMD_SCREEN_BUFFER *b=find(d,s->Identities[i].Token);
    if(!identity(d,b,&s->Identities[i]) || !b->SubmissionHolds ||
       b->KernelAllocation!=s->Allocations[i].hAllocation) return FALSE;
  }
  return TRUE;
}
/* Called under owner lock, after owns(). */
static void release(ADMISSION_UMD_DEVICE *d, ADMISSION_UMD_DRAW_SUBMISSION *s) {
  for(UINT i=0;i<s->Count;++i) --find(d,s->Identities[i].Token)->SubmissionHolds;
  d->DrawSubmission=NULL;
}

struct ComposerLookup {
  ADMISSION_UMD_DEVICE *Device;
  const ADMISSION_UMD_DRAW_SUBMISSION *Submission;
};
static int lookup(void *context, APPLE_AGX_U32 index, ADMISSION_WIN32_ALLOCATION_FACT *fact) {
  ComposerLookup *l=(ComposerLookup *)context;
  if(index>=l->Submission->Count) return 0;
  ADMISSION_UMD_SCREEN_BUFFER *b=find(l->Device,l->Submission->Identities[index].Token);
  if(!identity(l->Device,b,&l->Submission->Identities[index])) return 0;
  ZeroMemory(fact,sizeof(*fact));
  fact->AllocationToken=b->KernelAllocation; fact->Bytes=b->Bytes;
  fact->Generation=l->Device->Win32Generation; fact->ClassId=b->ClassId;
  fact->Flags=b->Flags; fact->Writable=(b->Flags & AppleAgxWin32BufferGpuWrite)!=0;
  return 1;
}
/* Reuse the production KMD reference/class/overlap validator. Physical
 * placement, display ownership and residency still require KMD live facts. */
static BOOL validate(ADMISSION_UMD_DEVICE *d, const ADMISSION_UMD_DRAW_SUBMISSION *s) {
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  ADMISSION_WIN32_ALLOCATION_FACT facts[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  ComposerLookup context={d,s};
  return AppleAgxWin32CommandValidate(s->Command,s->CommandBytes,s->Generation,s->Count,&view)
    ==AppleAgxWin32AbiSuccess && AdmissionWin32ValidateReferences(&view,s->Generation,
         lookup,&context,facts,ARRAYSIZE(facts))==AdmissionWin32TransportSuccess;
}

HRESULT AdmissionUmdDrawSeal(ADMISSION_UMD_DEVICE *d,
    APPLE_AGX_U64 requestId, APPLE_AGX_U16 version,
    const AGX_WIN32_DRAW_REQUEST *input,
    const AGX_WIN32_RELOC_ALLOCATION *ids, ADMISSION_UMD_DRAW_SUBMISSION *s) {
  ADMISSION_UMD_DRAW_SUBMISSION candidate={};
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE refs[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  AGX_WIN32_DRAW_REQUEST request;
  HRESULT result=E_INVALIDARG;
  if(!d || d->Magic!=ADMISSION_UMD_DEVICE_MAGIC || !input || !ids || !s ||
     s->Phase!=AdmissionDrawEmpty || !requestId || !input->References ||
     !input->Relocations || !input->ReferenceCount ||
     input->ReferenceCount>APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
     input->RelocationCount>APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS)
    return E_INVALIDARG;
  request=*input;
  CopyMemory(refs,input->References,input->ReferenceCount*sizeof(refs[0]));
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(d->DrawSubmission || d->ScreenClosing || d->DrawTerminal) {
    result=HRESULT_FROM_WIN32(ERROR_BUSY); goto done;
  }
  if(!d->Screen.Active || !d->OwnerCookie || !d->KernelContext ||
     request.Generation!=d->Win32Generation || requestId<=d->LastDrawRequest)
    goto done;
  candidate.Owner=d->OwnerCookie; candidate.Generation=d->Win32Generation;
  candidate.Context=d->KernelContext; candidate.RequestId=requestId;
  for(UINT i=0;i<request.ReferenceCount;++i) {
    const AGX_WIN32_RELOC_ALLOCATION *a=&ids[i];
    ADMISSION_UMD_SCREEN_BUFFER *b=find(d,a->Token);
    UINT access=refs[i].Access, index;
    if(!identity(d,b,a) || !access || (access & ~7u) ||
       (access & a->Access)!=access || b->SubmissionHolds==MAXUINT32 ||
       !refs[i].Bytes || refs[i].Offset>b->Bytes || refs[i].Bytes>b->Bytes-refs[i].Offset ||
       ((access & (AppleAgxWin32AccessRead|AppleAgxWin32AccessExecute)) &&
         !(b->Flags & AppleAgxWin32BufferGpuRead)) ||
       ((access & AppleAgxWin32AccessWrite) && !(b->Flags & AppleAgxWin32BufferGpuWrite)) ||
       ((access & AppleAgxWin32AccessExecute) && b->ClassId!=AgxWin32BufferClassShader))
      goto done;
    for(index=0;index<candidate.Count;++index) {
      if(candidate.Identities[index].Token==a->Token) break;
      if(candidate.Allocations[index].hAllocation==b->KernelAllocation) goto done;
    }
    if(index==candidate.Count) {
      candidate.Identities[index]=*a;
      candidate.Identities[index].AllocationIndex=index;
      candidate.Allocations[index].hAllocation=b->KernelAllocation;
      ++candidate.Count;
    } else if(candidate.Identities[index].Serial!=a->Serial ||
              candidate.Identities[index].Bytes!=a->Bytes) goto done;
    if(access & AppleAgxWin32AccessWrite)
      candidate.Allocations[index].WriteOperation=1;
    refs[i].AllocationIndex=index;
  }
  request.References=refs; request.AllocationCount=candidate.Count;
  if(AgxWin32TransportBuildDrawVersion(&request,version,candidate.Command,
      sizeof(candidate.Command),&candidate.CommandBytes)!=AppleAgxWin32AbiSuccess)
    goto done;
  if(!validate(d,&candidate)) goto done;
  for(UINT i=0;i<candidate.Count;++i)
    ++find(d,candidate.Identities[i].Token)->SubmissionHolds;
  candidate.Phase=AdmissionDrawSealed;
  *s=candidate; d->DrawSubmission=s; d->LastDrawRequest=requestId;
  result=S_OK;
done:
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
}

HRESULT AdmissionUmdDrawAbort(ADMISSION_UMD_DEVICE *d, ADMISSION_UMD_DRAW_SUBMISSION *s) {
  HRESULT result=E_INVALIDARG;
  if(!d || !s || d->Magic!=ADMISSION_UMD_DEVICE_MAGIC) return result;
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(owns(d,s) && s->Phase==AdmissionDrawSealed) {
    release(d,s); s->Phase=AdmissionDrawRejected; result=S_OK;
  }
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
}

HRESULT AdmissionUmdDrawDispatch(ADMISSION_UMD_DEVICE *d, ADMISSION_UMD_DRAW_SUBMISSION *s) {
  D3DDDICB_RENDER render={};
  HRESULT result;
  if(!d || !s || d->Magic!=ADMISSION_UMD_DEVICE_MAGIC) return E_INVALIDARG;
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(!owns(d,s) || s->Phase!=AdmissionDrawSealed) {
    ReleaseSRWLockExclusive(&d->ScreenBufferLock);
    return HRESULT_FROM_WIN32(ERROR_BUSY);
  }
  if(d->ScreenClosing || d->DrawTerminal || !d->Screen.Active ||
     !d->KernelCallbacks || !d->KernelCallbacks->pfnRenderCb ||
     !d->CommandBuffer || d->CommandBufferSize<s->CommandBytes ||
     !d->AllocationList || d->AllocationListSize<s->Count || !d->PatchList ||
     !validate(d,s)) {
    release(d,s); s->Phase=AdmissionDrawRejected;
    ReleaseSRWLockExclusive(&d->ScreenBufferLock); return E_INVALIDARG;
  }
  CopyMemory(d->CommandBuffer,s->Command,s->CommandBytes);
  CopyMemory(d->AllocationList,s->Allocations,s->Count*sizeof(s->Allocations[0]));
  render.hContext=s->Context; render.CommandLength=s->CommandBytes;
  render.NumAllocations=s->Count;
  s->Phase=AdmissionDrawCalling;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  result=d->KernelCallbacks->pfnRenderCb(d->RuntimeDevice.handle,&render);
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  s->RenderStatus=result;
  if(FAILED(result)) {
    /* Callback failure alone is not proof that no work consumed the source.
     * Preserve identities until an ordered marker or terminal teardown proves
     * quiescence. Never replay this Render or reuse its command buffers. */
    d->DrawTerminal=TRUE; d->CommandBuffer=NULL; d->CommandBufferSize=0;
    s->PostStatus=result;
  } else if(!render.pNewCommandBuffer || !render.NewCommandBufferSize ||
     !render.pNewAllocationList || !render.NewAllocationListSize ||
     !render.pNewPatchLocationList || !render.NewPatchLocationListSize || result!=S_OK) {
    d->DrawTerminal=TRUE; d->CommandBuffer=NULL; d->CommandBufferSize=0;
    s->PostStatus=E_FAIL;
  } else {
    d->CommandBuffer=render.pNewCommandBuffer; d->CommandBufferSize=render.NewCommandBufferSize;
    d->AllocationList=render.pNewAllocationList; d->AllocationListSize=render.NewAllocationListSize;
    d->PatchList=render.pNewPatchLocationList; d->PatchListSize=render.NewPatchLocationListSize;
  }
  s->Phase=AdmissionDrawSynchronizing;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  result=AdmissionUmdScreenSignalFence(d,&s->Fence);
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(FAILED(result)) s->PostStatus=result;
  s->Phase=FAILED(s->PostStatus)?AdmissionDrawPostError:AdmissionDrawAccepted;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return s->Phase==AdmissionDrawPostError?s->PostStatus:S_OK;
}

HRESULT AdmissionUmdDrawRetire(ADMISSION_UMD_DEVICE *d, ADMISSION_UMD_DRAW_SUBMISSION *s,
                              DWORD timeout) {
  HRESULT result;
  ADMISSION_UMD_DRAW_PHASE prior;
  if(!d || !s || d->Magic!=ADMISSION_UMD_DEVICE_MAGIC) return E_INVALIDARG;
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  BOOL valid=owns(d,s) && (s->Phase==AdmissionDrawAccepted || s->Phase==AdmissionDrawPostError);
  prior=s->Phase;
  if(valid) s->Phase=AdmissionDrawRetiring;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  if(!valid) return E_INVALIDARG;
  if(!s->Fence) {
    result=AdmissionUmdScreenSignalFence(d,&s->Fence);
    if(FAILED(result)) goto pending;
  }
  if(AgxWin32ScreenWaitFence(&d->Screen,s->Fence,timeout)!=AgxWin32ScreenSuccess) {
    result=d->LastScreenError; goto pending;
  }
  if(AgxWin32ScreenRetireFence(&d->Screen,s->Fence)!=AgxWin32ScreenSuccess) {
    result=d->LastScreenError; goto pending;
  }
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(!owns(d,s)) result=E_FAIL;
  else { release(d,s); s->Phase=AdmissionDrawRetired; result=S_OK; }
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
pending:
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  s->Phase=prior;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
}
