
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#ifndef APPLE_AGX_UMD_RESOURCE_LIFETIME_H
#define APPLE_AGX_UMD_RESOURCE_LIFETIME_H



typedef HRESULT(APIENTRY *ADMISSION_UMD_DEALLOCATE_RESOURCE)(
    void *Context, HANDLE RuntimeResource);
typedef VOID(APIENTRY *ADMISSION_UMD_REPORT_RESOURCE_ERROR)(
    void *Context, HRESULT Error);

typedef struct _ADMISSION_UMD_RETIREMENT {
  struct _ADMISSION_UMD_RETIREMENT *Next;
  HANDLE RuntimeResource;
  ULONG KernelResource;
  ULONG KernelAllocation;
  BOOL Primary;
  BOOL Shared;
} ADMISSION_UMD_RETIREMENT;

typedef struct _ADMISSION_UMD_RETIREMENT_QUEUE {
  SRWLOCK Lock;
  ADMISSION_UMD_RETIREMENT *Head;
  ADMISSION_UMD_RETIREMENT *Tail;
  ULONG Count;
  BOOL HasImmediateCommands;
  void *CallbackContext;
  ADMISSION_UMD_DEALLOCATE_RESOURCE Deallocate;
  ADMISSION_UMD_REPORT_RESOURCE_ERROR ReportError;
} ADMISSION_UMD_RETIREMENT_QUEUE;

typedef struct _ADMISSION_UMD_RETIREMENT_FINALIZE_RESULT {
  ULONG Attempted;
  ULONG Deallocated;
  ULONG Undeallocated;
  HRESULT FirstError;
  HRESULT LastError;
} ADMISSION_UMD_RETIREMENT_FINALIZE_RESULT;

VOID AdmissionUmdRetirementInitialize(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue, void *CallbackContext,
    ADMISSION_UMD_DEALLOCATE_RESOURCE Deallocate,
    ADMISSION_UMD_REPORT_RESOURCE_ERROR ReportError);
ADMISSION_UMD_RETIREMENT *AdmissionUmdRetirementCreate(void);
VOID AdmissionUmdRetirementFree(ADMISSION_UMD_RETIREMENT *Retirement);
VOID AdmissionUmdRetirementQueue(ADMISSION_UMD_RETIREMENT_QUEUE *Queue,
                                 ADMISSION_UMD_RETIREMENT *Retirement);
HRESULT AdmissionUmdRetirementDeallocate(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue,
    ADMISSION_UMD_RETIREMENT *Retirement);
BOOL AdmissionUmdRetirementDrain(ADMISSION_UMD_RETIREMENT_QUEUE *Queue);
VOID AdmissionUmdRetirementFinalize(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue,
    ADMISSION_UMD_RETIREMENT_FINALIZE_RESULT *Result);

#endif /* APPLE_AGX_UMD_RESOURCE_LIFETIME_H */


VOID AdmissionUmdRetirementInitialize(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue, void *CallbackContext,
    ADMISSION_UMD_DEALLOCATE_RESOURCE Deallocate,
    ADMISSION_UMD_REPORT_RESOURCE_ERROR ReportError) {
  if (Queue == NULL)
    return;
  ZeroMemory(Queue, sizeof(*Queue));
  InitializeSRWLock(&Queue->Lock);
  Queue->CallbackContext = CallbackContext;
  Queue->Deallocate = Deallocate;
  Queue->ReportError = ReportError;
}

ADMISSION_UMD_RETIREMENT *AdmissionUmdRetirementCreate(void) {
  return (ADMISSION_UMD_RETIREMENT *)HeapAlloc(
      GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ADMISSION_UMD_RETIREMENT));
}

VOID AdmissionUmdRetirementFree(ADMISSION_UMD_RETIREMENT *Retirement) {
  if (Retirement != NULL)
    HeapFree(GetProcessHeap(), 0u, Retirement);
}

VOID AdmissionUmdRetirementQueue(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue,
    ADMISSION_UMD_RETIREMENT *Retirement) {
  if (Queue == NULL || Retirement == NULL)
    return;
  Retirement->Next = NULL;
  AcquireSRWLockExclusive(&Queue->Lock);
  if (Queue->Tail != NULL)
    Queue->Tail->Next = Retirement;
  else
    Queue->Head = Retirement;
  Queue->Tail = Retirement;
  ++Queue->Count;
  ReleaseSRWLockExclusive(&Queue->Lock);
}

static VOID AdmissionUmdRetirementRequeueFront(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue,
    ADMISSION_UMD_RETIREMENT *Retirement) {
  AcquireSRWLockExclusive(&Queue->Lock);
  Retirement->Next = Queue->Head;
  Queue->Head = Retirement;
  if (Queue->Tail == NULL)
    Queue->Tail = Retirement;
  ++Queue->Count;
  ReleaseSRWLockExclusive(&Queue->Lock);
}

static ADMISSION_UMD_RETIREMENT *AdmissionUmdRetirementPop(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue) {
  ADMISSION_UMD_RETIREMENT *retirement;
  if (Queue == NULL)
    return NULL;
  AcquireSRWLockExclusive(&Queue->Lock);
  retirement = Queue->Head;
  if (retirement != NULL) {
    Queue->Head = retirement->Next;
    if (Queue->Head == NULL)
      Queue->Tail = NULL;
    retirement->Next = NULL;
    --Queue->Count;
  }
  ReleaseSRWLockExclusive(&Queue->Lock);
  return retirement;
}

HRESULT AdmissionUmdRetirementDeallocate(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue,
    ADMISSION_UMD_RETIREMENT *Retirement) {
  if (Queue == NULL || Retirement == NULL ||
      Retirement->RuntimeResource == NULL || Queue->Deallocate == NULL)
    return E_INVALIDARG;
  return Queue->Deallocate(Queue->CallbackContext,
                           Retirement->RuntimeResource);
}

BOOL AdmissionUmdRetirementDrain(ADMISSION_UMD_RETIREMENT_QUEUE *Queue) {
  ADMISSION_UMD_RETIREMENT *retirement;
  HRESULT result;
  if (Queue == NULL)
    return FALSE;
  if (Queue->HasImmediateCommands) {
    if (Queue->ReportError != NULL)
      Queue->ReportError(Queue->CallbackContext, E_NOTIMPL);
    return FALSE;
  }
  while ((retirement = AdmissionUmdRetirementPop(Queue)) != NULL) {
    result = AdmissionUmdRetirementDeallocate(Queue, retirement);
    if (FAILED(result)) {
      AdmissionUmdRetirementRequeueFront(Queue, retirement);
      if (Queue->ReportError != NULL)
        Queue->ReportError(Queue->CallbackContext, result);
      return FALSE;
    }
    AdmissionUmdRetirementFree(retirement);
  }
  return TRUE;
}

VOID AdmissionUmdRetirementFinalize(
    ADMISSION_UMD_RETIREMENT_QUEUE *Queue,
    ADMISSION_UMD_RETIREMENT_FINALIZE_RESULT *Result) {
  ADMISSION_UMD_RETIREMENT *retirement;
  HRESULT status;
  if (Result != NULL) {
    ZeroMemory(Result, sizeof(*Result));
    Result->FirstError = S_OK;
    Result->LastError = S_OK;
  }
  if (Queue == NULL || Result == NULL)
    return;
  while ((retirement = AdmissionUmdRetirementPop(Queue)) != NULL) {
    ++Result->Attempted;
    status = AdmissionUmdRetirementDeallocate(Queue, retirement);
    if (SUCCEEDED(status)) {
      ++Result->Deallocated;
    } else {
      if (Result->Undeallocated == 0u)
        Result->FirstError = status;
      Result->LastError = status;
      ++Result->Undeallocated;
    }
    AdmissionUmdRetirementFree(retirement);
  }
}
struct ADMISSION_UMD_RESOURCE { ADMISSION_UMD_RETIREMENT* Retirement; unsigned KernelAllocation; };
struct ADMISSION_UMD_ASAHI_BATCH {struct {HRESULT RenderStatus,PostStatus;} Submission;};
struct ADMISSION_UMD_DEVICE {ADMISSION_UMD_RETIREMENT_QUEUE Retirement;void*NativeBatchTransaction;BOOL DrawTerminal;HRESULT LastScreenError,LastRetirementError;};

static ADMISSION_UMD_DEVICE*AdmissionUmdDeviceFromHandle(D3D10DDI_HDEVICE x){return (ADMISSION_UMD_DEVICE*)x.pDrvPrivate;}
static ADMISSION_UMD_RESOURCE*AdmissionUmdResourceFromHandle(D3D10DDI_HRESOURCE x){return (ADMISSION_UMD_RESOURCE*)x.pDrvPrivate;}
static unsigned errors,closes,registered;static BOOL fail_deallocate;
static void AdmissionUmdSetError(ADMISSION_UMD_DEVICE*,HRESULT){++errors;}
VOID APIENTRY AdmissionUmdDestroyResource(
    D3D10DDI_HDEVICE DeviceHandle, D3D10DDI_HRESOURCE ResourceHandle) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  ADMISSION_UMD_RESOURCE *resource =
      AdmissionUmdResourceFromHandle(ResourceHandle);
  ADMISSION_UMD_RETIREMENT *retirement;
  HRESULT result;
  if (device == NULL || resource == NULL) {
    AdmissionUmdSetError(device, E_INVALIDARG);
    return;
  }
  retirement = resource->Retirement;
  ZeroMemory(resource, sizeof(*resource));
  if (retirement == NULL)
    return;
  if (retirement->Primary && !retirement->Shared) {
    result = AdmissionUmdRetirementDeallocate(&device->Retirement, retirement);
    if (FAILED(result)) {
      AdmissionUmdRetirementQueue(&device->Retirement, retirement);
      AdmissionUmdSetError(device, result);
      return;
    }
    AdmissionUmdRetirementFree(retirement);
  } else {
    AdmissionUmdRetirementQueue(&device->Retirement, retirement);
  }
}
struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE {AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE*Next;ADMISSION_UMD_RESOURCE Resource;};
struct AGX_D3D10_WINDOWS_DEVICE {int Stage;struct{int Failed;}Backend;ADMISSION_UMD_DEVICE Runtime;AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE*PendingPresentations;};
#define AgxD3d10DeviceReady 7
static void AgxWin32AsahiCollect(void*){}
static BOOL AdmissionUmdScreenAllocationRegistered(ADMISSION_UMD_DEVICE*,unsigned h){return h==registered;}
static BOOL collect_presentations(AGX_D3D10_WINDOWS_DEVICE *device) {
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE **link=&device->PendingPresentations;
  while(*link) {
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *record=*link;
    (void)AgxWin32AsahiCollect(&device->Backend);
    if(AdmissionUmdScreenAllocationRegistered(&device->Runtime,
                                               record->Resource.KernelAllocation)) {
      link=&record->Next;continue;
    }
    D3D10DDI_HDEVICE deviceHandle={0};D3D10DDI_HRESOURCE resourceHandle={0};
    deviceHandle.pDrvPrivate=&device->Runtime;
    resourceHandle.pDrvPrivate=&record->Resource;
    AdmissionUmdDestroyResource(deviceHandle,resourceHandle);
    *link=record->Next;
    HeapFree(GetProcessHeap(),0,record);
  }
  return device->PendingPresentations==NULL;
}
static void AgxD3d10WindowsDiagnosticState(AGX_D3D10_WINDOWS_DEVICE*,const char*){}
HRESULT AgxD3d10WindowsFlushStatus(AGX_D3D10_WINDOWS_DEVICE *Device) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady) return E_INVALIDARG;
  AgxD3d10WindowsDiagnosticState(Device,"flush-state");
  ADMISSION_UMD_ASAHI_BATCH *batch=
      (ADMISSION_UMD_ASAHI_BATCH *)Device->Runtime.NativeBatchTransaction;
  if(batch && (FAILED(batch->Submission.RenderStatus) ||
               FAILED(batch->Submission.PostStatus)))
    return FAILED(batch->Submission.PostStatus)?batch->Submission.PostStatus:
        batch->Submission.RenderStatus;
  if(!Device->Backend.Failed && !Device->Runtime.DrawTerminal) return S_OK;
  return FAILED(Device->Runtime.LastScreenError)?
      Device->Runtime.LastScreenError:E_FAIL;
}
HRESULT AgxD3d10WindowsFlushDeferredResources(AGX_D3D10_WINDOWS_DEVICE *Device) {
  if(!Device || Device->Stage!=AgxD3d10DeviceReady) return E_INVALIDARG;
  /* Flush must release eligible shared resources even with no new commands.
   * A registered native BO still owns source/submission references: collection
   * leaves it pending, and only fully retired resources reach this queue. */
  (void)collect_presentations(Device);
  if(!AdmissionUmdRetirementDrain(&Device->Runtime.Retirement))
    return FAILED(Device->Runtime.LastRetirementError) ?
        Device->Runtime.LastRetirementError : E_FAIL;
  return S_OK;
}
static HRESULT AgxD3d10WindowsQueryCollect(AGX_D3D10_WINDOWS_DEVICE*){return S_OK;}
struct Pipe{void(*flush)(Pipe*,void*,unsigned);};
struct Device{AGX_D3D10_WINDOWS_DEVICE*windows;Pipe*pipe;};
static Device*CastDevice(D3D10DDI_HDEVICE x){return (Device*)x.pDrvPrivate;}
static void SetError(D3D10DDI_HDEVICE,HRESULT){++errors;}
static void APIENTRY ActualFrontendFlush(D3D10DDI_HDEVICE hDevice){   Device *pDevice = CastDevice(hDevice);
   HRESULT result = AgxD3d10WindowsQueryCollect(pDevice->windows);
   if (SUCCEEDED(result)) {
      pDevice->pipe->flush(pDevice->pipe, NULL, 0);
      result = AgxD3d10WindowsFlushStatus(pDevice->windows);
   }
   if (SUCCEEDED(result)) result = AgxD3d10WindowsFlushDeferredResources(pDevice->windows);
   if (SUCCEEDED(result)) result = AgxD3d10WindowsQueryCollect(pDevice->windows);
   if (FAILED(result)) SetError(hDevice, result);}
static void FrontendFlush(Device*x){
 D3D10DDI_HDEVICE h={};h.pDrvPrivate=x;
 D3D10DDI_DEVICEFUNCS functions={};functions.pfnFlush=ActualFrontendFlush;
 functions.pfnFlush(h);
}
static void pipeflush(Pipe*,void*,unsigned){}
static HRESULT APIENTRY deallocate(void*,HANDLE){if(fail_deallocate)return E_FAIL;++closes;return S_OK;}
static void APIENTRY report(void*c,HRESULT h){((ADMISSION_UMD_DEVICE*)c)->LastRetirementError=h;}
static void enqueue(AGX_D3D10_WINDOWS_DEVICE*d,unsigned h){
 auto*r=(AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE*)calloc(1,sizeof(AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE));
 if(!r) abort();
 r->Resource.KernelAllocation=h;r->Resource.Retirement=AdmissionUmdRetirementCreate();
 if(!r->Resource.Retirement) abort();
 r->Resource.Retirement->RuntimeResource=(void*)(uintptr_t)h;r->Resource.Retirement->Shared=TRUE;
 r->Next=d->PendingPresentations;d->PendingPresentations=r;
}
int main(){
 (void)&collect_presentations;
 AGX_D3D10_WINDOWS_DEVICE d={};d.Stage=AgxD3d10DeviceReady;
 AdmissionUmdRetirementInitialize(&d.Runtime.Retirement,&d.Runtime,deallocate,report);
 Pipe p={pipeflush};Device frontend={&d,&p};
 // Native BO still owns shared allocation: no callback, no queue consumption.
 ADMISSION_UMD_ASAHI_BATCH pending={};d.Runtime.NativeBatchTransaction=&pending;
 enqueue(&d,17);registered=17;FrontendFlush(&frontend);assert(closes==0&&d.PendingPresentations);
 // Completion removes BO, empty Flush must collect and deallocate shared alias.
 d.Runtime.NativeBatchTransaction=nullptr;registered=0;FrontendFlush(&frontend);assert(closes==1&&!d.PendingPresentations&&d.Runtime.Retirement.Count==0);
 // Repeated create/destroy on a stable device must not grow retirement queue.
 for(unsigned i=0;i<64;++i){enqueue(&d,100+i);FrontendFlush(&frontend);assert(d.Runtime.Retirement.Count==0);}
 assert(closes==65);
 // Callback failure retains exact record; retry closes exactly once.
 enqueue(&d,500);fail_deallocate=TRUE;FrontendFlush(&frontend);
 assert(errors==1&&closes==65&&d.Runtime.Retirement.Count==1);
 fail_deallocate=FALSE;FrontendFlush(&frontend);assert(closes==66&&d.Runtime.Retirement.Count==0);
 // Failed/uncertain submission cannot be papered over by cleanup.
 enqueue(&d,600);d.Runtime.DrawTerminal=TRUE;FrontendFlush(&frontend);assert(closes==66&&d.PendingPresentations);
 d.Runtime.DrawTerminal=FALSE;FrontendFlush(&frontend);assert(closes==67);
 puts("R148 shared retirement: PASS");
}
