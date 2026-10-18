#ifndef APPLE_AGX_UMD_RESOURCE_LIFETIME_H
#define APPLE_AGX_UMD_RESOURCE_LIFETIME_H

#include <windows.h>

typedef struct _ADMISSION_UMD_RETIREMENT ADMISSION_UMD_RETIREMENT;
typedef HRESULT(APIENTRY *ADMISSION_UMD_DEALLOCATE_RESOURCE)(
    void *Context, const ADMISSION_UMD_RETIREMENT *Retirement);
typedef VOID(APIENTRY *ADMISSION_UMD_REPORT_RESOURCE_ERROR)(
    void *Context, HRESULT Error);

struct _ADMISSION_UMD_RETIREMENT {
  ADMISSION_UMD_RETIREMENT *Next;
  HANDLE RuntimeResource;
  ULONG KernelResource;
  ULONG KernelAllocation;
  ULONG Origin; /* 1=CreateResource, 2=OpenResource; diagnostic only. */
  BOOL Primary;
  BOOL Shared;
};

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
