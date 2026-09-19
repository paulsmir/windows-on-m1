#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
extern "C" {
#include "umd_internal.h"
#include "umd_asahi_batch_adapter.h"
}

static int valid(const ADMISSION_UMD_DEVICE *d,const ADMISSION_UMD_ASAHI_BATCH *b) {
  return d && b && b->Capture && b->Owner==d->OwnerCookie &&
    b->Generation==d->Win32Generation && b->RequestId &&
    b->Capture->Owner==b->Owner && b->Capture->Generation==b->Generation &&
    b->Capture->Request==b->RequestId;
}

static HRESULT seal_native(ADMISSION_UMD_DEVICE *d,
    AGX_WIN32_RELOC_CAPTURE *c,APPLE_AGX_U64 requestId,APPLE_AGX_U16 version,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw,
    const APPLE_AGX_WIN32_NATIVE_BATCH_METADATA *native,ADMISSION_UMD_ASAHI_BATCH *b) {
  AGX_WIN32_DRAW_REQUEST request={0};
  if(!d || !c || !draw || !b || b->Phase!=AdmissionAsahiBatchEmpty ||
     d->Magic!=ADMISSION_UMD_DEVICE_MAGIC || !requestId || c->Owner!=d->OwnerCookie ||
     c->Generation!=d->Win32Generation || c->Request!=requestId) return E_INVALIDARG;
  if(AgxWin32RelocPrepareDraw(c,version,draw,&request)!=AgxRelocOk) return E_INVALIDARG;
  request.NativeBatch=native;
  HRESULT hr=AdmissionUmdDrawSeal(d,requestId,version,&request,c->Allocations,&b->Submission);
  if(FAILED(hr)) { (void)AgxWin32RelocAbort(c); b->Phase=AdmissionAsahiBatchRejected; return hr; }
  b->Capture=c; b->Owner=c->Owner; b->Generation=c->Generation;
  b->RequestId=requestId; b->Phase=AdmissionAsahiBatchSealed;
  return S_OK;
}

extern "C" HRESULT AdmissionUmdAsahiBatchSeal(ADMISSION_UMD_DEVICE *d,
    AGX_WIN32_RELOC_CAPTURE *c,APPLE_AGX_U64 requestId,APPLE_AGX_U16 version,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw,ADMISSION_UMD_ASAHI_BATCH *b) {
  return seal_native(d,c,requestId,version,draw,NULL,b);
}
extern "C" HRESULT AdmissionUmdAsahiBatchSealNative(ADMISSION_UMD_DEVICE *d,
    AGX_WIN32_RELOC_CAPTURE *c,APPLE_AGX_U64 requestId,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw,
    const APPLE_AGX_WIN32_NATIVE_BATCH_METADATA *native,ADMISSION_UMD_ASAHI_BATCH *b) {
  if(!native || !c ||
     (c->CommandVersion!=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH &&
      c->CommandVersion!=APPLE_AGX_WIN32_COMMAND_VERSION_INDEXED_BATCH))
    return E_INVALIDARG;
  return seal_native(d,c,requestId,c->CommandVersion,draw,native,b);
}

extern "C" HRESULT AdmissionUmdAsahiBatchAbort(ADMISSION_UMD_DEVICE *d,
    ADMISSION_UMD_ASAHI_BATCH *b) {
  if(!valid(d,b) || b->Phase!=AdmissionAsahiBatchSealed) return E_INVALIDARG;
  HRESULT hr=AdmissionUmdDrawAbort(d,&b->Submission);
  if(FAILED(hr)) return hr;
  if(AgxWin32RelocAbort(b->Capture)!=AgxRelocOk) return E_FAIL;
  b->Phase=AdmissionAsahiBatchRejected;
  return S_OK;
}

extern "C" HRESULT AdmissionUmdAsahiBatchDispatch(ADMISSION_UMD_DEVICE *d,
    ADMISSION_UMD_ASAHI_BATCH *b) {
  if(!valid(d,b) || b->Phase!=AdmissionAsahiBatchSealed) return HRESULT_FROM_WIN32(ERROR_BUSY);
  HRESULT hr=AdmissionUmdDrawDispatch(d,&b->Submission);
  if(b->Submission.Phase==AdmissionDrawAccepted || b->Submission.Phase==AdmissionDrawPostError) {
    /* Composer keeps PostError after Render entry even when its ordered event
     * could not be enqueued. Capture still owns every native source in SEALED;
     * this adapter must prohibit replay/abort and let Retire retry the marker. */
    b->Phase=AdmissionAsahiBatchSubmitted;
    if(b->Submission.Fence &&
       AgxWin32RelocSubmitted(b->Capture,b->Submission.Fence)!=AgxRelocOk)
      return E_FAIL;
  } else if(b->Submission.Phase==AdmissionDrawRejected) {
    (void)AgxWin32RelocAbort(b->Capture);
    b->Phase=AdmissionAsahiBatchRejected;
  }
  return hr;
}

extern "C" HRESULT AdmissionUmdAsahiBatchRetire(ADMISSION_UMD_DEVICE *d,
    ADMISSION_UMD_ASAHI_BATCH *b,DWORD timeoutMs) {
  if(!valid(d,b) || b->Phase!=AdmissionAsahiBatchSubmitted) return E_INVALIDARG;
  BOOL quiesced=d->KernelContextQuiesced && !d->KernelContext &&
      d->QuiescedKernelContext && d->NativeBatchTransaction==b &&
      d->DrawSubmission==&b->Submission &&
      b->Submission.Context==d->QuiescedKernelContext &&
      b->RequestId==d->LastNativeRequest;
  HRESULT hr=AdmissionUmdDrawRetire(d,&b->Submission,timeoutMs,quiesced);
  /* DrawRetire may acquire the previously missing fence and then time out.
   * Bind capture to that same fence without dropping its holds; completion is
   * proved only when DrawRetire succeeds. No second Render is issued. */
  if(!b->Capture->Fence && b->Submission.Fence &&
     AgxWin32RelocSubmitted(b->Capture,b->Submission.Fence)!=AgxRelocOk)
    return E_FAIL;
  if(FAILED(hr)) return hr;
  if(AgxWin32RelocRetire(b->Capture,b->Owner,b->Generation,b->RequestId,
      b->Submission.Fence)!=AgxRelocOk) return E_FAIL;
  b->Phase=AdmissionAsahiBatchRetired;
  return S_OK;
}

extern "C" {
#include "umd_asahi_owner.h"
#include "agx_win32_asahi_batch.h"
}
static void *native_create(void *context,APPLE_AGX_U64 *request) {
  ADMISSION_UMD_ASAHI_OWNER *owner=(ADMISSION_UMD_ASAHI_OWNER *)context;
  if(!owner || !owner->Device || !request) return NULL;
  ADMISSION_UMD_DEVICE *d=owner->Device;
  ADMISSION_UMD_ASAHI_BATCH *b=(ADMISSION_UMD_ASAHI_BATCH *)HeapAlloc(
      GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*b));
  if(!b) return NULL;
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  APPLE_AGX_U64 next=d->LastNativeRequest>d->LastDrawRequest?d->LastNativeRequest:d->LastDrawRequest;
  BOOL allowed=d->Magic==ADMISSION_UMD_DEVICE_MAGIC && !d->ScreenClosing &&
      !d->DrawTerminal && !d->DrawSubmission && !d->NativeBatchTransaction && next!=~0ULL;
  if(allowed) {
    d->LastNativeRequest=++next; d->NativeBatchTransaction=b;
    b->Owner=d->OwnerCookie; b->Generation=d->Win32Generation; b->RequestId=next;
    *request=next;
  }
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  if(!allowed) { HeapFree(GetProcessHeap(),0,b); return NULL; }
  return b;
}
static int32_t native_submit(void *context,void *transaction,AGX_WIN32_RELOC_CAPTURE *capture,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw,const APPLE_AGX_WIN32_NATIVE_BATCH_METADATA *metadata,
    int *entered) {
  ADMISSION_UMD_ASAHI_OWNER *owner=(ADMISSION_UMD_ASAHI_OWNER *)context;
  ADMISSION_UMD_ASAHI_BATCH *b=(ADMISSION_UMD_ASAHI_BATCH *)transaction;
  if(entered) *entered=0;
  if(!owner || !owner->Device || !b || !entered ||
      owner->Device->NativeBatchTransaction!=b) return E_INVALIDARG;
  HRESULT hr=AdmissionUmdAsahiBatchSealNative(owner->Device,capture,b->RequestId,draw,metadata,b);
  if(SUCCEEDED(hr)) hr=AdmissionUmdAsahiBatchDispatch(owner->Device,b);
  *entered=b->Phase==AdmissionAsahiBatchSubmitted;
  return hr;
}
static int32_t native_retire(void *context,void *transaction,APPLE_AGX_U32 timeout) {
  ADMISSION_UMD_ASAHI_OWNER *owner=(ADMISSION_UMD_ASAHI_OWNER *)context;
  if(!owner || !owner->Device || owner->Device->NativeBatchTransaction!=transaction) return E_INVALIDARG;
  return AdmissionUmdAsahiBatchRetire(owner->Device,(ADMISSION_UMD_ASAHI_BATCH *)transaction,timeout);
}
static int native_destroy(void *context,void *transaction) {
  ADMISSION_UMD_ASAHI_OWNER *owner=(ADMISSION_UMD_ASAHI_OWNER *)context;
  ADMISSION_UMD_ASAHI_BATCH *b=(ADMISSION_UMD_ASAHI_BATCH *)transaction;
  if(!owner || !owner->Device || !b || owner->Device->NativeBatchTransaction!=b ||
      b->Phase==AdmissionAsahiBatchSubmitted) return 0;
  if(b->Phase==AdmissionAsahiBatchSealed && FAILED(AdmissionUmdAsahiBatchAbort(owner->Device,b))) return 0;
  AcquireSRWLockExclusive(&owner->Device->ScreenBufferLock);
  owner->Device->NativeBatchTransaction=NULL;
  ReleaseSRWLockExclusive(&owner->Device->ScreenBufferLock);
  HeapFree(GetProcessHeap(),0,b); return 1;
}
extern "C" const AGX_WIN32_ASAHI_BATCH_OPS *AdmissionUmdAsahiBatchOperations(void) {
  static const AGX_WIN32_ASAHI_BATCH_OPS ops={native_create,native_submit,native_retire,native_destroy};
  return &ops;
}
