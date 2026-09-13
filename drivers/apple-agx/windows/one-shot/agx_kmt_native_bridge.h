#ifndef AGX_KMT_NATIVE_BRIDGE_H
#define AGX_KMT_NATIVE_BRIDGE_H

#include <windows.h>
#include <d3dkmthk.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "agx_win32_asahi_batch.h"
#ifdef __cplusplus
}
#endif

typedef struct _AGX_KMT_NATIVE_BRIDGE AGX_KMT_NATIVE_BRIDGE;
typedef struct _AGX_KMT_NATIVE_BINDING {
  AGX_WIN32_SCREEN *Windows;
  AGX_WIN32_ASAHI_BACKEND *Backend;
  const AGX_WIN32_ASAHI_OWNER_OPS *OwnerOperations;
  void *Owner;
  const AGX_WIN32_ASAHI_BATCH_OPS *BatchOperations;
  /* Current compiled KMD contract and boot generation, not live GPU inventory. */
  const AGX_WIN32_DEVICE_INFO *DeviceInfo;
  D3DKMT_HANDLE ContextHandle;
} AGX_KMT_NATIVE_BINDING;

typedef enum _AGX_KMT_NATIVE_STAGE {
  AgxKmtNativeNone, AgxKmtNativeQuery, AgxKmtNativeContextCreate,
  AgxKmtNativeAllocate, AgxKmtNativeResident, AgxKmtNativePagingWait,
  AgxKmtNativeLock, AgxKmtNativeUnlock, AgxKmtNativeDeallocate,
  AgxKmtNativeRender, AgxKmtNativeSignal, AgxKmtNativeContextDestroy,
  AgxKmtNativeOwnerError, AgxKmtNativeEvict
} AGX_KMT_NATIVE_STAGE;

typedef struct _AGX_KMT_NATIVE_RECEIPT {
  UINT Bytes;
  AGX_KMT_NATIVE_STAGE Stage;
  LONG Status;
  HRESULT Result;
  BOOL CalledKmt;
  D3DKMT_HANDLE Handle;
  UINT Count;
  ULONGLONG Sequence, PagingExpected, PagingObserved;
  UINT Allocations, Deallocations, Locks, Unlocks, Renders, Signals;
  UINT ResidencyAcquires, ResidencyReuses, ResidencyEvicts;
  ULONGLONG CommandHash;
  UINT Win32Generation, References, Relocations, CommandBytes;
} AGX_KMT_NATIVE_RECEIPT;

#ifdef __cplusplus
extern "C" {
#endif
/* Borrow the three real KMT handles and paging-fence mapping until Close
 * succeeds. Create owns one context through the existing UMD initialization.
 * On initialization failure a nonnull output remains caller-owned for receipt
 * inspection and retryable Close; never discard it merely because HRESULT failed.
 * All calls and native production are serialized on the creating thread. */
HRESULT AgxKmtNativeBridgeCreate(D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device,
    D3DKMT_HANDLE PagingQueue, volatile const UINT64 *PagingFence,
    DWORD ResidencyTimeoutMs, AGX_KMT_NATIVE_BRIDGE **Bridge);
HRESULT AgxKmtNativeBridgeGetBinding(AGX_KMT_NATIVE_BRIDGE *, AGX_KMT_NATIVE_BINDING *);
HRESULT AgxKmtNativeBridgeGetReceipt(const AGX_KMT_NATIVE_BRIDGE *, AGX_KMT_NATIVE_RECEIPT *);
/* Native scene/context/screen teardown must finish first. Busy/failure leaves
 * the bridge reachable. Borrowed adapter/device/paging handles are not closed. */
HRESULT AgxKmtNativeBridgeClose(AGX_KMT_NATIVE_BRIDGE **);
#if defined(AGX_KMT_NATIVE_BRIDGE_TEST)
/* Deterministic residency accounting only, in the existing UmdContractTest.
 * Never compiled into or called by the hardware qualification client. */
unsigned AgxKmtNativeBridgeResidencyContractTest(void);
#endif
#ifdef __cplusplus
}
#endif
#endif
