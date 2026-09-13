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

extern "C" HRESULT AdmissionUmdAsahiBatchSeal(ADMISSION_UMD_DEVICE *d,
    AGX_WIN32_RELOC_CAPTURE *c,APPLE_AGX_U64 requestId,APPLE_AGX_U16 version,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw,ADMISSION_UMD_ASAHI_BATCH *b) {
  AGX_WIN32_DRAW_REQUEST request={0};
  if(!d || !c || !draw || !b || b->Phase!=AdmissionAsahiBatchEmpty ||
     d->Magic!=ADMISSION_UMD_DEVICE_MAGIC || !requestId || c->Owner!=d->OwnerCookie ||
     c->Generation!=d->Win32Generation || c->Request!=requestId) return E_INVALIDARG;
  if(AgxWin32RelocPrepareDraw(c,version,draw,&request)!=AgxRelocOk) return E_INVALIDARG;
  HRESULT hr=AdmissionUmdDrawSeal(d,requestId,version,&request,c->Allocations,&b->Submission);
  if(FAILED(hr)) { (void)AgxWin32RelocAbort(c); b->Phase=AdmissionAsahiBatchRejected; return hr; }
  b->Capture=c; b->Owner=c->Owner; b->Generation=c->Generation;
  b->RequestId=requestId; b->Phase=AdmissionAsahiBatchSealed;
  return S_OK;
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
  HRESULT hr=AdmissionUmdDrawRetire(d,&b->Submission,timeoutMs);
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
