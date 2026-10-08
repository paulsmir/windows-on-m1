#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
#include <stdio.h>
#define SEAL_REJECT() do { fprintf(stderr,"NATIVE_COMPOSER_REJECT: line=%u\n",(unsigned)__LINE__); goto done; } while(0)
#else
#define SEAL_REJECT() goto done
#endif
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
  for (UINT i=0; i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(d); ++i)
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
     s->Generation!=d->Win32Generation || d->KernelContextQuiesced ||
     s->Context!=d->KernelContext)
    return FALSE;
  for(UINT i=0;i<s->Count;++i) {
    ADMISSION_UMD_SCREEN_BUFFER *b=find(d,s->Identities[i].Token);
    if(!identity(d,b,&s->Identities[i]) || !b->SubmissionHolds ||
       b->KernelAllocation!=s->Allocations[i].hAllocation) return FALSE;
  }
  return TRUE;
}
static BOOL owns_quiesced_retirement(ADMISSION_UMD_DEVICE *d,
                                     ADMISSION_UMD_DRAW_SUBMISSION *s) {
  if(!d->KernelContextQuiesced || d->KernelContext ||
     !d->QuiescedKernelContext || s->Context!=d->QuiescedKernelContext ||
     d->DrawSubmission!=s || !d->NativeBatchTransaction || !s->Fence ||
     s->Owner!=d->OwnerCookie || s->Generation!=d->Win32Generation ||
     s->RequestId!=d->LastDrawRequest || s->RequestId!=d->LastNativeRequest ||
     (s->Phase!=AdmissionDrawAccepted && s->Phase!=AdmissionDrawPostError &&
      s->Phase!=AdmissionDrawRetiring))
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

static VOID diagnose_residency(
    ADMISSION_UMD_DEVICE *d, const ADMISSION_UMD_DRAW_SUBMISSION *s) {
  if(!AdmissionUmdDiagnosticEnabled() || !d || !s || !d->KernelCallbacks ||
     !d->KernelCallbacks->pfnQueryResidencyCb) return;
  for(UINT index=0;index<s->Count;++index) {
    D3DKMT_HANDLE handle=s->Allocations[index].hAllocation;
    D3DDDI_RESIDENCYSTATUS status=(D3DDDI_RESIDENCYSTATUS)0;
    D3DDDICB_QUERYRESIDENCY query={};
    query.NumAllocations=1u;
    query.HandleList=&handle;
    query.pResidencyStatus=&status;
    HRESULT result=d->KernelCallbacks->pfnQueryResidencyCb(
        d->RuntimeDevice.handle,&query);
    UINT values[4]={index,(UINT)handle,s->Allocations[index].Value,(UINT)status};
    AdmissionUmdDiagnostic("native-residency",result,values,ARRAYSIZE(values));
  }
}

static HRESULT evict_residency(
    ADMISSION_UMD_DEVICE *d, ADMISSION_UMD_DRAW_SUBMISSION *s,
    UINT count) {
  D3DKMT_HANDLE handles[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  D3DDDICB_EVICT evict={};
  if(!d || !s || !count || count>s->Count || !d->KernelCallbacks ||
     !d->KernelCallbacks->pfnEvictCb) return E_INVALIDARG;
  for(UINT i=0;i<count;++i) handles[i]=s->Allocations[i].hAllocation;
  evict.NumAllocations=count;
  evict.AllocationList=handles;
  HRESULT result=d->KernelCallbacks->pfnEvictCb(
      d->RuntimeDevice.handle,&evict);
  UINT values[3]={count,(UINT)evict.NumBytesToTrim,
      (UINT)(evict.NumBytesToTrim>>32)};
  AdmissionUmdDiagnostic("native-evict",result,values,ARRAYSIZE(values));
  return result;
}

static HRESULT make_resident(
    ADMISSION_UMD_DEVICE *d, ADMISSION_UMD_DRAW_SUBMISSION *s) {
  D3DKMT_HANDLE handles[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  UINT priorities[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  D3DDDI_MAKERESIDENT make={};
  if(!d || !s || !s->Count || s->Count>ARRAYSIZE(handles) ||
     !d->PagingQueue || !d->KernelCallbacks ||
     !d->KernelCallbacks->pfnMakeResidentCb) return E_INVALIDARG;
  for(UINT i=0;i<s->Count;++i) {
    handles[i]=s->Allocations[i].hAllocation;
    priorities[i]=D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  }
  make.hPagingQueue=d->PagingQueue;
  make.NumAllocations=s->Count;
  make.AllocationList=handles;
  make.PriorityList=priorities;
  /* This submission owns the only current residency set, so there is nothing
   * else this UMD can trim before its final required attempt. */
  make.Flags.CantTrimFurther=1u;
  make.Flags.MustSucceed=1u;
  UINT requested=s->Count;
  HRESULT result=d->KernelCallbacks->pfnMakeResidentCb(
      d->RuntimeDevice.handle,&make);
  UINT values[5]={requested,make.NumAllocations,(UINT)make.PagingFenceValue,
      (UINT)(make.PagingFenceValue>>32),(UINT)make.NumBytesToTrim};
  AdmissionUmdDiagnostic("native-make-resident",result,values,ARRAYSIZE(values));
  if(result==E_PENDING) {
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait={};
    D3DKMT_HANDLE object=d->PagingSyncObject;
    UINT64 fence=make.PagingFenceValue;
    if(!object || !fence ||
       !d->KernelCallbacks->pfnWaitForSynchronizationObjectFromCpuCb) {
      (void)evict_residency(d,s,requested);
      return E_FAIL;
    }
    wait.ObjectCount=1u;
    wait.ObjectHandleArray=&object;
    wait.FenceValueArray=&fence;
    HRESULT waitResult=
        d->KernelCallbacks->pfnWaitForSynchronizationObjectFromCpuCb(
            d->RuntimeDevice.handle,&wait);
    UINT waitValues[3]={(UINT)object,(UINT)fence,(UINT)(fence>>32)};
    AdmissionUmdDiagnostic("native-residency-wait",waitResult,
        waitValues,ARRAYSIZE(waitValues));
    if(FAILED(waitResult)) {
      (void)evict_residency(d,s,requested);
      return waitResult;
    }
    result=S_OK;
  }
  if(FAILED(result) || make.NumAllocations!=requested) {
    UINT partial=make.NumAllocations<requested?make.NumAllocations:requested;
    if(partial && FAILED(evict_residency(d,s,partial))) d->DrawTerminal=TRUE;
    return FAILED(result)?result:E_FAIL;
  }
  /* The shared helper validates the callback and queries one allocation per
   * status slot; a single output slot cannot describe an allocation array. */
  diagnose_residency(d,s);
  s->ResidencyHeld=TRUE;
  return S_OK;
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
  if(AppleAgxWin32CommandValidate(s->Command,s->CommandBytes,s->Generation,s->Count,&view)
      !=AppleAgxWin32AbiSuccess) return FALSE;
  ADMISSION_WIN32_TRANSPORT_RESULT result=AdmissionWin32ValidateReferences(&view,s->Generation,
      lookup,&context,facts,ARRAYSIZE(facts));
#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
  if(APPLE_AGX_WIN32_COMMAND_IS_NATIVE(view.Header->Version)) {
    FILE *wire=NULL; (void)fopen_s(&wire,"native-producer-command.bin","wb");
    if(wire) { fwrite(s->Command,1,s->CommandBytes,wire); fclose(wire); }
    FILE *allocation=NULL; (void)fopen_s(&allocation,"native-producer-facts.bin","wb");
    for(unsigned i=0;i<s->Count;++i) {
      ADMISSION_WIN32_ALLOCATION_FACT fact={0};
      lookup(&context,i,&fact);
      if(allocation) fwrite(&fact,1,sizeof(fact),allocation);
    }
    if(allocation) fclose(allocation);
    if(result!=AdmissionWin32TransportSuccess) {
      fprintf(stderr,"NATIVE_REFERENCE_REJECT: result=%u\n",(unsigned)result);
      for(unsigned i=0;i<view.Header->ReferenceCount;++i) {
        const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *r=&view.References[i];
        ADMISSION_WIN32_ALLOCATION_FACT fact={0};lookup(&context,r->AllocationIndex,&fact);
        fprintf(stderr,"NATIVE_REFERENCE: index=%u role=%u class=%u access=%u offset=%llu bytes=%llu allocation=%u\n",
            i,r->Role,fact.ClassId,r->Access,r->Offset,r->Bytes,r->AllocationIndex);
      }
    }
  }
#endif
  return result==AdmissionWin32TransportSuccess;
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
     input->ReferenceCount>APPLE_AGX_WIN32_COMMAND_REFERENCE_LIMIT(version) ||
     input->RelocationCount>APPLE_AGX_WIN32_COMMAND_RELOCATION_LIMIT(version))
    return E_INVALIDARG;
  request=*input;
  CopyMemory(refs,input->References,input->ReferenceCount*sizeof(refs[0]));
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(d->DrawSubmission || d->ScreenClosing || d->DrawTerminal) {
    result=HRESULT_FROM_WIN32(ERROR_BUSY); SEAL_REJECT();
  }
  if(!d->Screen.Active || !d->OwnerCookie || !d->KernelContext ||
     request.Generation!=d->Win32Generation || requestId<=d->LastDrawRequest)
    SEAL_REJECT();
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
      SEAL_REJECT();
    for(index=0;index<candidate.Count;++index) {
      if(candidate.Identities[index].Token==a->Token) break;
      if(candidate.Allocations[index].hAllocation==b->KernelAllocation) SEAL_REJECT();
    }
    if(index==candidate.Count) {
      candidate.Identities[index]=*a;
      candidate.Identities[index].AllocationIndex=index;
      candidate.Allocations[index].hAllocation=b->KernelAllocation;
      ++candidate.Count;
    } else if(candidate.Identities[index].Serial!=a->Serial ||
              candidate.Identities[index].Bytes!=a->Bytes) SEAL_REJECT();
    if(access & AppleAgxWin32AccessWrite)
      candidate.Allocations[index].WriteOperation=1;
    refs[i].AllocationIndex=index;
  }
  request.References=refs; request.AllocationCount=candidate.Count;
  if(AgxWin32TransportBuildDrawVersion(&request,version,candidate.Command,
      sizeof(candidate.Command),&candidate.CommandBytes)!=AppleAgxWin32AbiSuccess)
    SEAL_REJECT();
  if(!validate(d,&candidate)) SEAL_REJECT();
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
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  result=AdmissionUmdScreenPrepareSubmissionMaps(d,s);
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(FAILED(result)) {
    if(owns(d,s) && s->Phase==AdmissionDrawSealed) {
      release(d,s); s->Phase=AdmissionDrawRejected;
    }
    ReleaseSRWLockExclusive(&d->ScreenBufferLock);
    return result;
  }
  if(!owns(d,s) || s->Phase!=AdmissionDrawSealed || d->ScreenClosing ||
     d->DrawTerminal) {
    if(owns(d,s) && s->Phase==AdmissionDrawSealed) {
      release(d,s); s->Phase=AdmissionDrawRejected;
    }
    ReleaseSRWLockExclusive(&d->ScreenBufferLock);
    return E_FAIL;
  }
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  diagnose_residency(d,s);
  result=make_resident(d,s);
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(FAILED(result)) {
    if(owns(d,s) && s->Phase==AdmissionDrawSealed) {
      release(d,s); s->Phase=AdmissionDrawRejected;
    }
    ReleaseSRWLockExclusive(&d->ScreenBufferLock);
    return result;
  }
  if(!owns(d,s) || s->Phase!=AdmissionDrawSealed || d->ScreenClosing ||
     d->DrawTerminal) {
    ReleaseSRWLockExclusive(&d->ScreenBufferLock);
    (void)evict_residency(d,s,s->Count);
    s->ResidencyHeld=FALSE;
    AcquireSRWLockExclusive(&d->ScreenBufferLock);
    if(owns(d,s) && s->Phase==AdmissionDrawSealed) {
      release(d,s); s->Phase=AdmissionDrawRejected;
    }
    ReleaseSRWLockExclusive(&d->ScreenBufferLock);
    return E_FAIL;
  }
  if(!AdmissionUmdNextRenderSequence(d,&render.RenderCBSequence)) {
    ReleaseSRWLockExclusive(&d->ScreenBufferLock);
    (void)evict_residency(d,s,s->Count);
    s->ResidencyHeld=FALSE;
    AcquireSRWLockExclusive(&d->ScreenBufferLock);
    if(owns(d,s) && s->Phase==AdmissionDrawSealed) {
      release(d,s); s->Phase=AdmissionDrawRejected;
    }
    ReleaseSRWLockExclusive(&d->ScreenBufferLock);
    return E_FAIL;
  }
  render.hContext=s->Context; render.CommandLength=s->CommandBytes;
  render.NumAllocations=s->Count;
  s->Phase=AdmissionDrawCalling;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  result=d->KernelCallbacks->pfnRenderCb(d->RuntimeDevice.handle,&render);
  {
    UINT_PTR context=(UINT_PTR)render.hContext;
    UINT values[5]={render.RenderCBSequence,(UINT)context,
        (UINT)(context>>32),render.NumAllocations,render.CommandLength};
    AdmissionUmdDiagnostic("native-render-callback",result,values,5u);
  }
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
                              DWORD timeout, BOOL quiescedRetirement) {
  HRESULT result;
  ADMISSION_UMD_DRAW_PHASE prior;
  if(!d || !s || d->Magic!=ADMISSION_UMD_DEVICE_MAGIC) return E_INVALIDARG;
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  BOOL valid=(quiescedRetirement?owns_quiesced_retirement(d,s):owns(d,s)) &&
      (s->Phase==AdmissionDrawAccepted || s->Phase==AdmissionDrawPostError);
  prior=s->Phase;
  if(valid) s->Phase=AdmissionDrawRetiring;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  if(!valid) return E_INVALIDARG;
  if(!s->Fence) {
    if(quiescedRetirement) { result=E_INVALIDARG;goto pending; }
    result=AdmissionUmdScreenSignalFence(d,&s->Fence);
    if(FAILED(result)) goto pending;
  }
  if(AgxWin32ScreenWaitFence(&d->Screen,s->Fence,timeout)!=AgxWin32ScreenSuccess) {
    result=d->LastScreenError; goto pending;
  }
  if(s->ResidencyHeld) {
    result=evict_residency(d,s,s->Count);
    if(FAILED(result)) goto pending;
    s->ResidencyHeld=FALSE;
  }
  if(AgxWin32ScreenRetireFence(&d->Screen,s->Fence)!=AgxWin32ScreenSuccess) {
    result=d->LastScreenError; goto pending;
  }
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(!(quiescedRetirement?owns_quiesced_retirement(d,s):owns(d,s))) result=E_FAIL;
  else { release(d,s); s->Phase=AdmissionDrawRetired; result=S_OK; }
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
pending:
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  s->Phase=prior;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
}
