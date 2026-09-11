#include "apple_agx_driver.h"

/*
 * Baseline render-only WDDM contract.
 *
 * Registration of these callbacks is a structural requirement of a full KMD,
 * not a claim that the corresponding AGX operation exists.  Keep every entry
 * inert until a later experiment supplies an independently tested contract.
 */

#define APPLE_AGX_UNUSED(value) UNREFERENCED_PARAMETER(value)

_Use_decl_annotations_ NTSTATUS AppleAgxDdiNotifyAcpiEvent(
    PVOID MiniportDeviceContext, DXGK_EVENT_TYPE EventType, ULONG Event,
    PVOID Argument, PULONG AcpiFlags) {
  APPLE_AGX_UNUSED(MiniportDeviceContext);
  APPLE_AGX_UNUSED(EventType);
  APPLE_AGX_UNUSED(Event);
  APPLE_AGX_UNUSED(Argument);
  if (AcpiFlags != NULL)
    *AcpiFlags = 0;
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxDdiQueryInterface(PVOID MiniportDeviceContext,
                          PQUERY_INTERFACE QueryInterface) {
  APPLE_AGX_UNUSED(MiniportDeviceContext);
  APPLE_AGX_UNUSED(QueryInterface);
  return STATUS_NOT_SUPPORTED;
}

VOID AppleAgxDdiControlEtwLogging(BOOLEAN Enable, ULONG Flags, UCHAR Level) {
  APPLE_AGX_UNUSED(Enable);
  APPLE_AGX_UNUSED(Flags);
  APPLE_AGX_UNUSED(Level);
}

#define APPLE_AGX_FAIL_CLOSED_2(name, type1, arg1, type2, arg2)               \
  _Use_decl_annotations_ NTSTATUS name(type1 arg1, type2 arg2) {             \
    APPLE_AGX_UNUSED(arg1);                                                   \
    APPLE_AGX_UNUSED(arg2);                                                   \
    return STATUS_NOT_SUPPORTED;                                             \
  }

APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiCreateDevice, HANDLE, Adapter,
                        DXGKARG_CREATEDEVICE *, CreateDevice)

_Use_decl_annotations_ NTSTATUS AppleAgxDdiDestroyDevice(HANDLE Device) {
  APPLE_AGX_UNUSED(Device);
  return STATUS_SUCCESS;
}

APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiCreateAllocation, HANDLE, Adapter,
                        DXGKARG_CREATEALLOCATION *, CreateAllocation)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiDestroyAllocation, HANDLE, Adapter,
                        const DXGKARG_DESTROYALLOCATION *, DestroyAllocation)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiDescribeAllocation, HANDLE, Adapter,
                        DXGKARG_DESCRIBEALLOCATION *, DescribeAllocation)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiGetStandardAllocationDriverData, HANDLE,
                        Adapter, DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *,
                        StandardAllocation)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiOpenAllocation, HANDLE, Device,
                        const DXGKARG_OPENALLOCATION *, OpenAllocation)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiCloseAllocation, HANDLE, Device,
                        const DXGKARG_CLOSEALLOCATION *, CloseAllocation)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiPatch, HANDLE, Adapter,
                        const DXGKARG_PATCH *, Patch)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiSubmitCommand, HANDLE, Adapter,
                        const DXGKARG_SUBMITCOMMAND *, SubmitCommand)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiBuildPagingBuffer, HANDLE, Adapter,
                        DXGKARG_BUILDPAGINGBUFFER *, BuildPagingBuffer)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiPreemptCommand, HANDLE, Adapter,
                        const DXGKARG_PREEMPTCOMMAND *, PreemptCommand)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiRender, HANDLE, Context,
                        DXGKARG_RENDER *, Render)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiPresent, HANDLE, Context,
                        DXGKARG_PRESENT *, Present)

_Use_decl_annotations_ NTSTATUS AppleAgxDdiResetFromTimeout(HANDLE Adapter) {
  APPLE_AGX_UNUSED(Adapter);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiRestartFromTimeout(HANDLE Adapter) {
  APPLE_AGX_UNUSED(Adapter);
  return STATUS_NOT_SUPPORTED;
}

APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiEscape, HANDLE, Adapter,
                        const DXGKARG_ESCAPE *, Escape)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiCollectDbgInfo, HANDLE, Adapter,
                        const DXGKARG_COLLECTDBGINFO *, CollectDbgInfo)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiQueryCurrentFence, HANDLE, Adapter,
                        DXGKARG_QUERYCURRENTFENCE *, CurrentFence)

_Use_decl_annotations_ NTSTATUS AppleAgxDdiControlInterrupt(
    HANDLE Adapter, DXGK_INTERRUPT_TYPE InterruptType, BOOLEAN EnableInterrupt) {
  APPLE_AGX_UNUSED(Adapter);
  APPLE_AGX_UNUSED(InterruptType);
  APPLE_AGX_UNUSED(EnableInterrupt);
  return STATUS_NOT_SUPPORTED;
}

APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiCreateContext, HANDLE, Device,
                        DXGKARG_CREATECONTEXT *, CreateContext)

_Use_decl_annotations_ NTSTATUS AppleAgxDdiDestroyContext(HANDLE Context) {
  APPLE_AGX_UNUSED(Context);
  return STATUS_SUCCESS;
}

APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiRenderKm, HANDLE, Context,
                        DXGKARG_RENDER *, Render)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiQueryDependentEngineGroup, HANDLE, Adapter,
                        DXGKARG_QUERYDEPENDENTENGINEGROUP *, DependentGroup)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiQueryEngineStatus, HANDLE, Adapter,
                        DXGKARG_QUERYENGINESTATUS *, EngineStatus)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiResetEngine, HANDLE, Adapter,
                        DXGKARG_RESETENGINE *, ResetEngine)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiCancelCommand, HANDLE, Adapter,
                        const DXGKARG_CANCELCOMMAND *, CancelCommand)

_Use_decl_annotations_ NTSTATUS AppleAgxDdiSetPowerComponentFState(
    PVOID MiniportDeviceContext, UINT ComponentIndex, UINT FState) {
  APPLE_AGX_UNUSED(MiniportDeviceContext);
  APPLE_AGX_UNUSED(ComponentIndex);
  APPLE_AGX_UNUSED(FState);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiPowerRuntimeControlRequest(
    PVOID MiniportDeviceContext, LPCGUID PowerControlCode, PVOID InBuffer,
    SIZE_T InBufferSize, PVOID OutBuffer, SIZE_T OutBufferSize,
    PSIZE_T BytesReturned) {
  APPLE_AGX_UNUSED(MiniportDeviceContext);
  APPLE_AGX_UNUSED(PowerControlCode);
  APPLE_AGX_UNUSED(InBuffer);
  APPLE_AGX_UNUSED(InBufferSize);
  APPLE_AGX_UNUSED(OutBuffer);
  APPLE_AGX_UNUSED(OutBufferSize);
  if (BytesReturned != NULL)
    *BytesReturned = 0;
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiGetNodeMetadata(
    HANDLE Adapter, UINT NodeOrdinal, DXGKARG_GETNODEMETADATA *NodeMetadata) {
  APPLE_AGX_UNUSED(Adapter);
  APPLE_AGX_UNUSED(NodeOrdinal);
  APPLE_AGX_UNUSED(NodeMetadata);
  return STATUS_NOT_SUPPORTED;
}

APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiSubmitCommandVirtual, HANDLE, Adapter,
                        const DXGKARG_SUBMITCOMMANDVIRTUAL *, SubmitCommand)
APPLE_AGX_FAIL_CLOSED_2(AppleAgxDdiCreateProcess, PVOID, MiniportDeviceContext,
                        DXGKARG_CREATEPROCESS *, CreateProcess)

_Use_decl_annotations_ NTSTATUS AppleAgxDdiDestroyProcess(
    PVOID MiniportDeviceContext, HANDLE KmdProcessHandle) {
  APPLE_AGX_UNUSED(MiniportDeviceContext);
  APPLE_AGX_UNUSED(KmdProcessHandle);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiCalibrateGpuClock(
    HANDLE Adapter, UINT32 NodeOrdinal, UINT32 EngineOrdinal,
    DXGKARG_CALIBRATEGPUCLOCK *ClockCalibration) {
  APPLE_AGX_UNUSED(Adapter);
  APPLE_AGX_UNUSED(NodeOrdinal);
  APPLE_AGX_UNUSED(EngineOrdinal);
  APPLE_AGX_UNUSED(ClockCalibration);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ VOID AppleAgxDdiSetStablePowerState(
    HANDLE Adapter, const DXGKARG_SETSTABLEPOWERSTATE *StablePowerState) {
  APPLE_AGX_UNUSED(Adapter);
  APPLE_AGX_UNUSED(StablePowerState);
}

#undef APPLE_AGX_FAIL_CLOSED_2
#undef APPLE_AGX_UNUSED
