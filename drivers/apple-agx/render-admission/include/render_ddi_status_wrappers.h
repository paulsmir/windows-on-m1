/* Return-boundary wrappers: retain the original DDI signatures and behavior.
 * Included only by driver.c, after render_admission.h. No handle dereference
 * after calling the implementation (Destroy callbacks may free it). */
#define ADMISSION_STATUS_WRAP(Id, Name, Signature, Arguments, Object, Allocation, Operation, Extra) \
  static NTSTATUS Name##Status Signature { \
    ULONGLONG allocation = (ULONGLONG)(ULONG_PTR)(Allocation); \
    ULONG operation = (ULONG)(Operation); \
    NTSTATUS status = Name Arguments; \
    BOOLEAN first = AdmissionRecordDdiStatus(Id, status, operation, allocation, \
        (ULONGLONG)(ULONG_PTR)(Object)); \
    (void)first; \
    Extra; \
    return status; \
  }

/* 0: DxgkDdiAddDevice */
ADMISSION_STATUS_WRAP(0, AdmissionDdiAddDevice,
    (PDEVICE_OBJECT PhysicalDeviceObject, PVOID *MiniportDeviceContext),
    (PhysicalDeviceObject, MiniportDeviceContext), PhysicalDeviceObject, 0, MAXULONG,
    (void)0)
/* 1: DxgkDdiStartDevice */
ADMISSION_STATUS_WRAP(1, AdmissionDdiStartDevice,
    (PVOID MiniportDeviceContext, PDXGK_START_INFO DxgkStartInfo, PDXGKRNL_INTERFACE DxgkInterface, PULONG NumberOfVideoPresentSources, PULONG NumberOfChildren),
    (MiniportDeviceContext, DxgkStartInfo, DxgkInterface, NumberOfVideoPresentSources, NumberOfChildren), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 2: DxgkDdiStopDevice */
ADMISSION_STATUS_WRAP(2, AdmissionDdiStopDevice,
    (PVOID MiniportDeviceContext),
    (MiniportDeviceContext), MiniportDeviceContext, 0, MAXULONG,
    (void)0)
/* 3: DxgkDdiRemoveDevice */
ADMISSION_STATUS_WRAP(3, AdmissionDdiRemoveDevice,
    (PVOID MiniportDeviceContext),
    (MiniportDeviceContext), MiniportDeviceContext, 0, MAXULONG,
    (void)0)
/* 4: DxgkDdiDispatchIoRequest */
ADMISSION_STATUS_WRAP(4, AdmissionDdiDispatchIoRequest,
    (PVOID MiniportDeviceContext, ULONG VidPnSourceId, PVIDEO_REQUEST_PACKET VideoRequestPacket),
    (MiniportDeviceContext, VidPnSourceId, VideoRequestPacket), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 5: DxgkDdiQueryChildRelations */
ADMISSION_STATUS_WRAP(5, AdmissionDdiQueryChildRelations,
    (PVOID MiniportDeviceContext, PDXGK_CHILD_DESCRIPTOR ChildRelations, ULONG ChildRelationsSize),
    (MiniportDeviceContext, ChildRelations, ChildRelationsSize), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 6: DxgkDdiQueryChildStatus */
ADMISSION_STATUS_WRAP(6, AdmissionDdiQueryChildStatus,
    (PVOID MiniportDeviceContext, PDXGK_CHILD_STATUS ChildStatus, BOOLEAN NonDestructiveOnly),
    (MiniportDeviceContext, ChildStatus, NonDestructiveOnly), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 7: DxgkDdiQueryDeviceDescriptor */
ADMISSION_STATUS_WRAP(7, AdmissionDdiQueryDeviceDescriptor,
    (PVOID MiniportDeviceContext, ULONG ChildUid, PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor),
    (MiniportDeviceContext, ChildUid, DeviceDescriptor), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 8: DxgkDdiSetPowerState */
ADMISSION_STATUS_WRAP(8, AdmissionDdiSetPowerState,
    (PVOID MiniportDeviceContext, ULONG DeviceUid, DEVICE_POWER_STATE DevicePowerState, POWER_ACTION ActionType),
    (MiniportDeviceContext, DeviceUid, DevicePowerState, ActionType), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 9: DxgkDdiQueryAdapterInfo */
ADMISSION_STATUS_WRAP(9, AdmissionDdiQueryAdapterInfo,
    (HANDLE Adapter, const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo),
    (Adapter, QueryAdapterInfo), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 10: DxgkDdiSetPalette */
ADMISSION_STATUS_WRAP(10, AdmissionDdiSetPalette,
    (HANDLE Adapter, const DXGKARG_SETPALETTE *SetPalette),
    (Adapter, SetPalette), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 11: DxgkDdiSetPointerPosition */
ADMISSION_STATUS_WRAP(11, AdmissionDdiSetPointerPosition,
    (CONST HANDLE MiniportDeviceContext, CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition),
    (MiniportDeviceContext, SetPointerPosition), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 12: DxgkDdiSetPointerShape */
ADMISSION_STATUS_WRAP(12, AdmissionDdiSetPointerShape,
    (CONST HANDLE MiniportDeviceContext, CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape),
    (MiniportDeviceContext, SetPointerShape), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 13: DxgkDdiIsSupportedVidPn */
ADMISSION_STATUS_WRAP(13, AdmissionTraceDdiIsSupportedVidPn,
    (CONST HANDLE MiniportDeviceContext, DXGKARG_ISSUPPORTEDVIDPN * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 14: DxgkDdiRecommendFunctionalVidPn */
ADMISSION_STATUS_WRAP(14, AdmissionTraceDdiRecommendFunctionalVidPn,
    (CONST HANDLE MiniportDeviceContext, const DXGKARG_RECOMMENDFUNCTIONALVIDPN * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 15: DxgkDdiEnumVidPnCofuncModality */
ADMISSION_STATUS_WRAP(15, AdmissionTraceDdiEnumVidPnCofuncModality,
    (CONST HANDLE MiniportDeviceContext, const DXGKARG_ENUMVIDPNCOFUNCMODALITY * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 16: DxgkDdiSetVidPnSourceVisibility */
ADMISSION_STATUS_WRAP(16, AdmissionTraceDdiSetVidPnSourceVisibility,
    (CONST HANDLE MiniportDeviceContext, const DXGKARG_SETVIDPNSOURCEVISIBILITY * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 17: DxgkDdiCommitVidPn */
ADMISSION_STATUS_WRAP(17, AdmissionTraceDdiCommitVidPn,
    (CONST HANDLE MiniportDeviceContext, const DXGKARG_COMMITVIDPN * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 18: DxgkDdiUpdateActiveVidPnPresentPath */
ADMISSION_STATUS_WRAP(18, AdmissionTraceDdiUpdateActiveVidPnPresentPath,
    (CONST HANDLE MiniportDeviceContext, const DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 19: DxgkDdiRecommendMonitorModes */
ADMISSION_STATUS_WRAP(19, AdmissionTraceDdiRecommendMonitorModes,
    (CONST HANDLE MiniportDeviceContext, const DXGKARG_RECOMMENDMONITORMODES * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 20: DxgkDdiUpdateMonitorLinkInfo */
ADMISSION_STATUS_WRAP(20, AdmissionTraceDdiUpdateMonitorLinkInfo,
    (CONST HANDLE MiniportDeviceContext, DXGKARG_UPDATEMONITORLINKINFO * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 21: DxgkDdiGetScanLine */
ADMISSION_STATUS_WRAP(21, AdmissionDdiGetScanLine,
    (HANDLE Adapter, DXGKARG_GETSCANLINE *GetScanLine),
    (Adapter, GetScanLine), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 22: DxgkDdiStopCapture */
ADMISSION_STATUS_WRAP(22, AdmissionDdiStopCapture,
    (HANDLE Adapter, const DXGKARG_STOPCAPTURE *StopCapture),
    (Adapter, StopCapture), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 23: DxgkDdiQueryVidPnHWCapability */
ADMISSION_STATUS_WRAP(23, AdmissionTraceDdiQueryVidPnHWCapability,
    (CONST HANDLE MiniportDeviceContext, DXGKARG_QUERYVIDPNHWCAPABILITY * Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 24: DxgkDdiSetVidPnSourceAddress */
ADMISSION_STATUS_WRAP(24, AdmissionDdiSetVidPnSourceAddress,
    (CONST HANDLE MiniportDeviceContext, CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress),
    (MiniportDeviceContext, SetVidPnSourceAddress), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 25: DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3 */
ADMISSION_STATUS_WRAP(25, AdmissionDdiSetVidPnSourceAddressWithMultiPlaneOverlay3,
    (CONST HANDLE MiniportDeviceContext, DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 *Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 26: DxgkDdiStopDeviceAndReleasePostDisplayOwnership */
ADMISSION_STATUS_WRAP(26, AdmissionDdiStopDeviceAndReleasePostDisplayOwnership,
    (PVOID MiniportDeviceContext, D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, PDXGK_DISPLAY_INFORMATION DisplayInfo),
    (MiniportDeviceContext, TargetId, DisplayInfo), MiniportDeviceContext, 0, MAXULONG,
    (void)0)
/* 27: DxgkDdiNotifyAcpiEvent */
ADMISSION_STATUS_WRAP(27, AdmissionDdiNotifyAcpiEvent,
    (PVOID MiniportDeviceContext, DXGK_EVENT_TYPE EventType, ULONG Event, PVOID Argument, PULONG AcpiFlags),
    (MiniportDeviceContext, EventType, Event, Argument, AcpiFlags), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 28: DxgkDdiQueryInterface */
ADMISSION_STATUS_WRAP(28, AdmissionDdiQueryInterface,
    (PVOID MiniportDeviceContext, PQUERY_INTERFACE QueryInterface),
    (MiniportDeviceContext, QueryInterface), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 29: DxgkDdiCreateDevice */
ADMISSION_STATUS_WRAP(29, AdmissionDdiCreateDevice,
    (HANDLE Adapter, DXGKARG_CREATEDEVICE *Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 30: DxgkDdiDestroyDevice */
ADMISSION_STATUS_WRAP(30, AdmissionDdiDestroyDevice,
    (HANDLE Device),
    (Device), Device, 0, MAXULONG,
    (void)0)
/* 31: DxgkDdiCreateAllocation */
ADMISSION_STATUS_WRAP(31, AdmissionDdiCreateAllocation,
    (HANDLE Adapter, DXGKARG_CREATEALLOCATION *Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 32: DxgkDdiDestroyAllocation */
ADMISSION_STATUS_WRAP(32, AdmissionDdiDestroyAllocation,
    (HANDLE Adapter, const DXGKARG_DESTROYALLOCATION *Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 33: DxgkDdiDescribeAllocation */
ADMISSION_STATUS_WRAP(33, AdmissionDdiDescribeAllocation,
    (HANDLE Adapter, DXGKARG_DESCRIBEALLOCATION *Args),
    (Adapter, Args), Adapter, Args ? Args->hAllocation : NULL, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 34: DxgkDdiGetStandardAllocationDriverData */
ADMISSION_STATUS_WRAP(34, AdmissionDdiGetStandardAllocationDriverData,
    (HANDLE Adapter, DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *StandardAllocation),
    (Adapter, StandardAllocation), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 35: DxgkDdiOpenAllocation */
ADMISSION_STATUS_WRAP(35, AdmissionDdiOpenAllocation,
    (HANDLE Device, const DXGKARG_OPENALLOCATION *Args),
    (Device, Args), Device, 0, MAXULONG,
    (void)0)
/* 36: DxgkDdiCloseAllocation */
ADMISSION_STATUS_WRAP(36, AdmissionDdiCloseAllocation,
    (HANDLE Device, const DXGKARG_CLOSEALLOCATION *Args),
    (Device, Args), Device, 0, MAXULONG,
    (void)0)
/* 37: DxgkDdiPatch */
ADMISSION_STATUS_WRAP(37, AdmissionDdiPatch,
    (HANDLE Adapter, const DXGKARG_PATCH *Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 38: DxgkDdiSubmitCommand */
ADMISSION_STATUS_WRAP(38, AdmissionDdiSubmitCommand,
    (HANDLE Adapter, const DXGKARG_SUBMITCOMMAND *Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 39: DxgkDdiBuildPagingBuffer */
ADMISSION_STATUS_WRAP(39, AdmissionDdiBuildPagingBuffer,
    (HANDLE Adapter, DXGKARG_BUILDPAGINGBUFFER *Args),
    (Adapter, Args), Adapter, AdmissionPagingStatusAllocation(Args), Args ? Args->Operation : MAXULONG,
    first |= AdmissionRecordDdiStatus(80u + (operation < 31u ? operation : 31u), status, operation, allocation, (ULONGLONG)(ULONG_PTR)Adapter); if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 40: DxgkDdiPreemptCommand */
ADMISSION_STATUS_WRAP(40, AdmissionDdiPreemptCommand,
    (HANDLE Adapter, const DXGKARG_PREEMPTCOMMAND *PreemptCommand),
    (Adapter, PreemptCommand), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 41: DxgkDdiRender */
ADMISSION_STATUS_WRAP(41, AdmissionDdiRender,
    (HANDLE Context, DXGKARG_RENDER *Args),
    (Context, Args), Context, 0, MAXULONG,
    (void)0)
/* 42: DxgkDdiPresent */
ADMISSION_STATUS_WRAP(42, AdmissionDdiPresent,
    (HANDLE Context, DXGKARG_PRESENT *Present),
    (Context, Present), Context, 0, MAXULONG,
    (void)0)
/* 43: DxgkDdiCreateOverlay */
ADMISSION_STATUS_WRAP(43, AdmissionDdiCreateOverlay,
    (HANDLE Adapter, DXGKARG_CREATEOVERLAY * Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 44: DxgkDdiUpdateOverlay */
ADMISSION_STATUS_WRAP(44, AdmissionDdiUpdateOverlay,
    (HANDLE Overlay, const DXGKARG_UPDATEOVERLAY * Args),
    (Overlay, Args), Overlay, 0, MAXULONG,
    (void)0)
/* 45: DxgkDdiFlipOverlay */
ADMISSION_STATUS_WRAP(45, AdmissionDdiFlipOverlay,
    (HANDLE Overlay, const DXGKARG_FLIPOVERLAY * Args),
    (Overlay, Args), Overlay, 0, MAXULONG,
    (void)0)
/* 46: DxgkDdiDestroyOverlay */
ADMISSION_STATUS_WRAP(46, AdmissionDdiDestroyOverlay,
    (HANDLE Overlay),
    (Overlay), Overlay, 0, MAXULONG,
    (void)0)
/* 47: DxgkDdiResetFromTimeout */
ADMISSION_STATUS_WRAP(47, AdmissionDdiResetFromTimeout,
    (HANDLE Adapter),
    (Adapter), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 48: DxgkDdiRestartFromTimeout */
ADMISSION_STATUS_WRAP(48, AdmissionDdiRestartFromTimeout,
    (HANDLE Adapter),
    (Adapter), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 49: DxgkDdiEscape */
ADMISSION_STATUS_WRAP(49, AdmissionDdiEscape,
    (HANDLE Adapter, const DXGKARG_ESCAPE *Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 50: DxgkDdiCollectDbgInfo */
ADMISSION_STATUS_WRAP(50, AdmissionDdiCollectDbgInfo,
    (HANDLE Adapter, const DXGKARG_COLLECTDBGINFO *CollectDbgInfo),
    (Adapter, CollectDbgInfo), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 51: DxgkDdiQueryCurrentFence */
ADMISSION_STATUS_WRAP(51, AdmissionDdiQueryCurrentFence,
    (HANDLE Adapter, DXGKARG_QUERYCURRENTFENCE *CurrentFence),
    (Adapter, CurrentFence), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 52: DxgkDdiControlInterrupt */
ADMISSION_STATUS_WRAP(52, AdmissionDdiControlInterrupt,
    (HANDLE Adapter, DXGK_INTERRUPT_TYPE InterruptType, BOOLEAN EnableInterrupt),
    (Adapter, InterruptType, EnableInterrupt), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 53: DxgkDdiCreateContext */
ADMISSION_STATUS_WRAP(53, AdmissionDdiCreateContext,
    (HANDLE Device, DXGKARG_CREATECONTEXT *Args),
    (Device, Args), Device, 0, MAXULONG,
    (void)0)
/* 54: DxgkDdiDestroyContext */
ADMISSION_STATUS_WRAP(54, AdmissionDdiDestroyContext,
    (HANDLE Context),
    (Context), Context, 0, MAXULONG,
    (void)0)
/* 55: DxgkDdiRenderKm */
ADMISSION_STATUS_WRAP(55, AdmissionDdiRenderKm,
    (HANDLE Context, DXGKARG_RENDER *Args),
    (Context, Args), Context, 0, MAXULONG,
    (void)0)
/* 56: DxgkDdiQueryDependentEngineGroup */
ADMISSION_STATUS_WRAP(56, AdmissionDdiQueryDependentEngineGroup,
    (HANDLE Adapter, DXGKARG_QUERYDEPENDENTENGINEGROUP *DependentGroup),
    (Adapter, DependentGroup), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 57: DxgkDdiQueryEngineStatus */
ADMISSION_STATUS_WRAP(57, AdmissionDdiQueryEngineStatus,
    (HANDLE Adapter, DXGKARG_QUERYENGINESTATUS *EngineStatus),
    (Adapter, EngineStatus), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 58: DxgkDdiResetEngine */
ADMISSION_STATUS_WRAP(58, AdmissionDdiResetEngine,
    (HANDLE Adapter, DXGKARG_RESETENGINE *ResetEngine),
    (Adapter, ResetEngine), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 59: DxgkDdiCancelCommand */
ADMISSION_STATUS_WRAP(59, AdmissionDdiCancelCommand,
    (HANDLE Adapter, const DXGKARG_CANCELCOMMAND *Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 60: DxgkDdiSetPowerComponentFState */
ADMISSION_STATUS_WRAP(60, AdmissionDdiSetPowerComponentFState,
    (PVOID MiniportDeviceContext, UINT ComponentIndex, UINT FState),
    (MiniportDeviceContext, ComponentIndex, FState), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 61: DxgkDdiPowerRuntimeControlRequest */
ADMISSION_STATUS_WRAP(61, AdmissionDdiPowerRuntimeControlRequest,
    (PVOID MiniportDeviceContext, LPCGUID PowerControlCode, PVOID InBuffer, SIZE_T InBufferSize, PVOID OutBuffer, SIZE_T OutBufferSize, PSIZE_T BytesReturned),
    (MiniportDeviceContext, PowerControlCode, InBuffer, InBufferSize, OutBuffer, OutBufferSize, BytesReturned), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 62: DxgkDdiGetNodeMetadata */
ADMISSION_STATUS_WRAP(62, AdmissionDdiGetNodeMetadata,
    (HANDLE Adapter, UINT NodeOrdinal, DXGKARG_GETNODEMETADATA *NodeMetadata),
    (Adapter, NodeOrdinal, NodeMetadata), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 63: DxgkDdiSubmitCommandVirtual */
ADMISSION_STATUS_WRAP(63, AdmissionDdiSubmitCommandVirtual,
    (HANDLE Adapter, const DXGKARG_SUBMITCOMMANDVIRTUAL *Args),
    (Adapter, Args), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))
/* 64: DxgkDdiCreateProcess */
ADMISSION_STATUS_WRAP(64, AdmissionDdiCreateProcess,
    (PVOID MiniportDeviceContext, DXGKARG_CREATEPROCESS *Args),
    (MiniportDeviceContext, Args), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 65: DxgkDdiDestroyProcess */
ADMISSION_STATUS_WRAP(65, AdmissionDdiDestroyProcess,
    (PVOID MiniportDeviceContext, HANDLE KmdProcessHandle),
    (MiniportDeviceContext, KmdProcessHandle), MiniportDeviceContext, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)MiniportDeviceContext))
/* 66: DxgkDdiCalibrateGpuClock */
ADMISSION_STATUS_WRAP(66, AdmissionDdiCalibrateGpuClock,
    (HANDLE Adapter, UINT32 NodeOrdinal, UINT32 EngineOrdinal, DXGKARG_CALIBRATEGPUCLOCK *ClockCalibration),
    (Adapter, NodeOrdinal, EngineOrdinal, ClockCalibration), Adapter, 0, MAXULONG,
    (void)0; if (first) AdmissionQueueDdiStatus((ADMISSION_CONTEXT *)Adapter))

#undef ADMISSION_STATUS_WRAP

static void AdmissionInstallStatusReceipts(DRIVER_INITIALIZATION_DATA *Init) {
  Init->DxgkDdiAddDevice = AdmissionDdiAddDeviceStatus;
  Init->DxgkDdiStartDevice = AdmissionDdiStartDeviceStatus;
  Init->DxgkDdiStopDevice = AdmissionDdiStopDeviceStatus;
  Init->DxgkDdiRemoveDevice = AdmissionDdiRemoveDeviceStatus;
  Init->DxgkDdiDispatchIoRequest = AdmissionDdiDispatchIoRequestStatus;
  Init->DxgkDdiQueryChildRelations = AdmissionDdiQueryChildRelationsStatus;
  Init->DxgkDdiQueryChildStatus = AdmissionDdiQueryChildStatusStatus;
  Init->DxgkDdiQueryDeviceDescriptor = AdmissionDdiQueryDeviceDescriptorStatus;
  Init->DxgkDdiSetPowerState = AdmissionDdiSetPowerStateStatus;
  Init->DxgkDdiQueryAdapterInfo = AdmissionDdiQueryAdapterInfoStatus;
  Init->DxgkDdiSetPalette = AdmissionDdiSetPaletteStatus;
  Init->DxgkDdiSetPointerPosition = AdmissionDdiSetPointerPositionStatus;
  Init->DxgkDdiSetPointerShape = AdmissionDdiSetPointerShapeStatus;
  Init->DxgkDdiIsSupportedVidPn = AdmissionTraceDdiIsSupportedVidPnStatus;
  Init->DxgkDdiRecommendFunctionalVidPn = AdmissionTraceDdiRecommendFunctionalVidPnStatus;
  Init->DxgkDdiEnumVidPnCofuncModality = AdmissionTraceDdiEnumVidPnCofuncModalityStatus;
  Init->DxgkDdiSetVidPnSourceVisibility = AdmissionTraceDdiSetVidPnSourceVisibilityStatus;
  Init->DxgkDdiCommitVidPn = AdmissionTraceDdiCommitVidPnStatus;
  Init->DxgkDdiUpdateActiveVidPnPresentPath = AdmissionTraceDdiUpdateActiveVidPnPresentPathStatus;
  Init->DxgkDdiRecommendMonitorModes = AdmissionTraceDdiRecommendMonitorModesStatus;
  Init->DxgkDdiUpdateMonitorLinkInfo = AdmissionTraceDdiUpdateMonitorLinkInfoStatus;
  Init->DxgkDdiGetScanLine = AdmissionDdiGetScanLineStatus;
  Init->DxgkDdiStopCapture = AdmissionDdiStopCaptureStatus;
  Init->DxgkDdiQueryVidPnHWCapability = AdmissionTraceDdiQueryVidPnHWCapabilityStatus;
  Init->DxgkDdiSetVidPnSourceAddress = AdmissionDdiSetVidPnSourceAddressStatus;
  Init->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3 = AdmissionDdiSetVidPnSourceAddressWithMultiPlaneOverlay3Status;
  Init->DxgkDdiStopDeviceAndReleasePostDisplayOwnership = AdmissionDdiStopDeviceAndReleasePostDisplayOwnershipStatus;
  Init->DxgkDdiNotifyAcpiEvent = AdmissionDdiNotifyAcpiEventStatus;
  Init->DxgkDdiQueryInterface = AdmissionDdiQueryInterfaceStatus;
  Init->DxgkDdiCreateDevice = AdmissionDdiCreateDeviceStatus;
  Init->DxgkDdiDestroyDevice = AdmissionDdiDestroyDeviceStatus;
  Init->DxgkDdiCreateAllocation = AdmissionDdiCreateAllocationStatus;
  Init->DxgkDdiDestroyAllocation = AdmissionDdiDestroyAllocationStatus;
  Init->DxgkDdiDescribeAllocation = AdmissionDdiDescribeAllocationStatus;
  Init->DxgkDdiGetStandardAllocationDriverData = AdmissionDdiGetStandardAllocationDriverDataStatus;
  Init->DxgkDdiOpenAllocation = AdmissionDdiOpenAllocationStatus;
  Init->DxgkDdiCloseAllocation = AdmissionDdiCloseAllocationStatus;
  Init->DxgkDdiPatch = AdmissionDdiPatchStatus;
  Init->DxgkDdiSubmitCommand = AdmissionDdiSubmitCommandStatus;
  Init->DxgkDdiBuildPagingBuffer = AdmissionDdiBuildPagingBufferStatus;
  Init->DxgkDdiPreemptCommand = AdmissionDdiPreemptCommandStatus;
  Init->DxgkDdiRender = AdmissionDdiRenderStatus;
  Init->DxgkDdiPresent = AdmissionDdiPresentStatus;
  Init->DxgkDdiCreateOverlay = AdmissionDdiCreateOverlayStatus;
  Init->DxgkDdiUpdateOverlay = AdmissionDdiUpdateOverlayStatus;
  Init->DxgkDdiFlipOverlay = AdmissionDdiFlipOverlayStatus;
  Init->DxgkDdiDestroyOverlay = AdmissionDdiDestroyOverlayStatus;
  Init->DxgkDdiResetFromTimeout = AdmissionDdiResetFromTimeoutStatus;
  Init->DxgkDdiRestartFromTimeout = AdmissionDdiRestartFromTimeoutStatus;
  Init->DxgkDdiEscape = AdmissionDdiEscapeStatus;
  Init->DxgkDdiCollectDbgInfo = AdmissionDdiCollectDbgInfoStatus;
  Init->DxgkDdiQueryCurrentFence = AdmissionDdiQueryCurrentFenceStatus;
  Init->DxgkDdiControlInterrupt = AdmissionDdiControlInterruptStatus;
  Init->DxgkDdiCreateContext = AdmissionDdiCreateContextStatus;
  Init->DxgkDdiDestroyContext = AdmissionDdiDestroyContextStatus;
  Init->DxgkDdiRenderKm = AdmissionDdiRenderKmStatus;
  Init->DxgkDdiQueryDependentEngineGroup = AdmissionDdiQueryDependentEngineGroupStatus;
  Init->DxgkDdiQueryEngineStatus = AdmissionDdiQueryEngineStatusStatus;
  Init->DxgkDdiResetEngine = AdmissionDdiResetEngineStatus;
  Init->DxgkDdiCancelCommand = AdmissionDdiCancelCommandStatus;
  Init->DxgkDdiSetPowerComponentFState = AdmissionDdiSetPowerComponentFStateStatus;
  Init->DxgkDdiPowerRuntimeControlRequest = AdmissionDdiPowerRuntimeControlRequestStatus;
  Init->DxgkDdiGetNodeMetadata = AdmissionDdiGetNodeMetadataStatus;
  Init->DxgkDdiSubmitCommandVirtual = AdmissionDdiSubmitCommandVirtualStatus;
  Init->DxgkDdiCreateProcess = AdmissionDdiCreateProcessStatus;
  Init->DxgkDdiDestroyProcess = AdmissionDdiDestroyProcessStatus;
  Init->DxgkDdiCalibrateGpuClock = AdmissionDdiCalibrateGpuClockStatus;
}
