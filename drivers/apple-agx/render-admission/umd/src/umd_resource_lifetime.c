#include "umd_resource_lifetime.h"

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
