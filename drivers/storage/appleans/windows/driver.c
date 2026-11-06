/* SPDX-License-Identifier: MIT */
#include "adapter.h"
#include "resources.h"

sp_DRIVER_INITIALIZE DriverEntry;
HW_FIND_ADAPTER AppleAnsFindAdapter;
HW_INITIALIZE AppleAnsInitialize;
HW_BUILDIO AppleAnsBuildIo;
HW_STARTIO AppleAnsStartIo;
HW_INTERRUPT AppleAnsInterrupt;
HW_RESET_BUS AppleAnsResetBus;
HW_ADAPTER_CONTROL AppleAnsAdapterControl;

_Use_decl_annotations_
ULONG DriverEntry(PVOID DriverObject, PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA init = {0};
    init.HwInitializationDataSize = sizeof(init);
    init.AdapterInterfaceType = InterfaceTypeUndefined;
    init.FeatureSupport = STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE |
                          STOR_FEATURE_ADAPTER_NOT_REQUIRE_IO_PORT;
    /* WDK 26100 stores physical/virtual FindAdapter alternatives in PVOID;
     * the HW_FIND_ADAPTER declaration verifies our physical signature. */
#pragma warning(suppress:4152)
    init.HwFindAdapter = AppleAnsFindAdapter;
    init.HwInitialize = AppleAnsInitialize;
    init.HwBuildIo = AppleAnsBuildIo;
    init.HwStartIo = AppleAnsStartIo;
    init.HwInterrupt = AppleAnsInterrupt;
    init.HwResetBus = AppleAnsResetBus;
    init.HwAdapterControl = AppleAnsAdapterControl;
    init.DeviceExtensionSize = sizeof(APPLE_ANS_ADAPTER);
    init.SrbExtensionSize = sizeof(APPLE_ANS_SRB);
    init.NumberOfAccessRanges = 1;
    init.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    init.NeedPhysicalAddresses = TRUE;
    init.TaggedQueuing = TRUE;
    init.AutoRequestSense = TRUE;
    init.MultipleRequestPerLu = TRUE;
    return StorPortInitialize(DriverObject, RegistryPath, &init, NULL);
}

_Use_decl_annotations_
ULONG AppleAnsFindAdapter(PVOID DeviceExtension, PVOID HwContext,
    PVOID BusInformation, PCHAR ArgumentString,
    PPORT_CONFIGURATION_INFORMATION ConfigInfo, PBOOLEAN Reserved3)
{
    PAPPLE_ANS_ADAPTER a = DeviceExtension;
    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    UNREFERENCED_PARAMETER(Reserved3);
    /* A PnP restart must never erase ownership after an unproven stop. */
    if (a->Signature == ANS_MAGIC && (a->MayOwn || a->Wire.faulted))
        return SP_RETURN_ERROR;
    RtlZeroMemory(a, sizeof(*a));
    a->Signature = ANS_MAGIC;a->Version = 2;a->Stage = AppleAnsStageFindAdapter;
    (void)ans_order_init(&a->Order, 1);
    ans_stats_init(&a->Stats);
    a->ShutdownTag=ANS_QUEUE_DEPTH;
    if (ConfigInfo->Length < sizeof(*ConfigInfo) ||
        ConfigInfo->NumberOfAccessRanges != 1 || ConfigInfo->AccessRanges == NULL) {
        a->Stage = AppleAnsStageResourcesRejected;return SP_RETURN_NOT_FOUND;
    }
    PACCESS_RANGE range = &(*ConfigInfo->AccessRanges)[0];
    struct ans_resources resources={
        .base=(ULONGLONG)range->RangeStart.QuadPart,.length=range->RangeLength,
        .range_count=ConfigInfo->NumberOfAccessRanges,
        .vector=ConfigInfo->BusInterruptVector,.level=ConfigInfo->BusInterruptLevel,
        .vector2=ConfigInfo->BusInterruptVector2,.level2=ConfigInfo->BusInterruptLevel2,
        .memory=range->RangeInMemory,.level_sensitive=ConfigInfo->InterruptMode==LevelSensitive,
        .virtual_device=ConfigInfo->VirtualDevice
    };
    if (!ans_resources_valid(&resources)) {
        a->Stage = AppleAnsStageResourcesRejected;return SP_RETURN_NOT_FOUND;
    }
    a->BrokerAddress = range->RangeStart;a->BrokerLength = range->RangeLength;
    a->AssignedInterruptVector = ConfigInfo->BusInterruptVector;
    a->AssignedInterruptLevel = ConfigInfo->BusInterruptLevel;
    ConfigInfo->MaximumTransferLength = ANS_MAX_TRANSFER;
    ConfigInfo->NumberOfPhysicalBreaks = ANS_MAX_DATA_PAGES;
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;
    ConfigInfo->MaxIOsPerLun = ANS_QUEUE_DEPTH - ANS_ADMIN_TAGS;
    ConfigInfo->ScatterGather = TRUE;ConfigInfo->Master = TRUE;
    ConfigInfo->TaggedQueuing = TRUE;ConfigInfo->MultipleRequestPerLu = TRUE;
    ConfigInfo->AutoRequestSense = TRUE;
    ConfigInfo->MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->AlignmentMask = 3;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
#if APPLE_ANS_NATIVE_READ_ONLY
    if (!AppleAnsNativeResources(a, ConfigInfo)) {
        a->Stage = AppleAnsStageResourcesRejected;return SP_RETURN_ERROR;
    }
#endif
    a->ResourcesAccepted = TRUE;a->Stage = AppleAnsStageResourcesAccepted;
    return SP_RETURN_FOUND;
}

_Use_decl_annotations_
BOOLEAN AppleAnsInitialize(PVOID DeviceExtension)
{
    PAPPLE_ANS_ADAPTER a = DeviceExtension;
#if APPLE_ANS_NATIVE_READ_ONLY
    if (a->ResourcesAccepted && AppleAnsNativeInitialize(a)) {
        /* Hardware and namespace are already ready; passive work only constructs
         * the preallocated DPC object required by the Storport DDI. */
        if (StorPortEnablePassiveInitialization(a,AppleAnsPassiveInitialize)) return TRUE;
        (void)AppleAnsNativeStop(a);
    }
#else
    ans_stats_add(&a->Stats,ANS_TELEM_STARTS,1);
    a->Stage = AppleAnsStageTransportUnavailable;
#endif
    a->Failure = ANS_RESULT_BAD_STATE;
    StorPortLogError(a,NULL,0,0,0,SP_INTERNAL_ADAPTER_ERROR,a->Stage);
    return FALSE;
}

_Use_decl_annotations_
BOOLEAN AppleAnsBuildIo(PVOID DeviceExtension, PSCSI_REQUEST_BLOCK Srb)
{
    PAPPLE_ANS_SRB x = Srb->SrbExtension;
    if (!x) return TRUE; /* StartIo returns failure. */
    RtlZeroMemory(x,sizeof(*x));
    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI) return TRUE;
    if (!Srb->CdbLength || Srb->CdbLength > sizeof(x->Cdb)) return TRUE;
    x->CdbLength = Srb->CdbLength;x->Length = Srb->DataTransferLength;
    RtlCopyMemory(x->Cdb,Srb->Cdb,x->CdbLength);
    if (ans_scsi_cdb_has_payload_prps(x->Cdb[0])) {
        struct ans_sg_span spans[ANS_MAX_DATA_PAGES];
        if (x->Length) {
            PSTOR_SCATTER_GATHER_LIST sg = StorPortGetScatterGatherList(DeviceExtension,Srb);
            if (!sg || !sg->NumberOfElements || sg->NumberOfElements > ANS_MAX_DATA_PAGES) {
                x->PrpFailure=TRUE;return TRUE;
            }
            for (unsigned i=0;i<sg->NumberOfElements;i++) {
                spans[i].address = (ULONGLONG)sg->List[i].PhysicalAddress.QuadPart;
                spans[i].length = sg->List[i].Length;
            }
            if (ans_sg_to_prps(spans,sg->NumberOfElements,x->Length,&x->Prps) != ANS_PRP_OK) {
                x->PrpFailure=TRUE;return TRUE;
            }
        }
    }
    x->Valid = TRUE;
    return TRUE;
}

_Use_decl_annotations_
BOOLEAN AppleAnsStartIo(PVOID DeviceExtension, PSCSI_REQUEST_BLOCK Srb)
{
    STOR_LOCK_HANDLE lock={0};
    StorPortAcquireSpinLock(DeviceExtension,InterruptLock,NULL,&lock);
    BOOLEAN result = AppleAnsNativeStart(DeviceExtension,Srb);
    StorPortReleaseSpinLock(DeviceExtension,&lock);
    return result;
}

_Use_decl_annotations_
BOOLEAN AppleAnsInterrupt(PVOID DeviceExtension)
{
    /* Storport already holds InterruptLock; never acquire it recursively. */
    return AppleAnsNativeInterrupt(DeviceExtension);
}

_Use_decl_annotations_
BOOLEAN AppleAnsResetBus(PVOID DeviceExtension, ULONG PathId)
{
    if (PathId) return FALSE;
    STOR_LOCK_HANDLE lock={0};
    StorPortAcquireSpinLock(DeviceExtension,InterruptLock,NULL,&lock);
    BOOLEAN result = AppleAnsNativeReset(DeviceExtension);
    StorPortReleaseSpinLock(DeviceExtension,&lock);
    return result;
}

_Use_decl_annotations_
SCSI_ADAPTER_CONTROL_STATUS AppleAnsAdapterControl(PVOID DeviceExtension,
    SCSI_ADAPTER_CONTROL_TYPE ControlType, PVOID Parameters)
{
    PAPPLE_ANS_ADAPTER a = DeviceExtension;
    if (ControlType == ScsiQuerySupportedControlTypes) {
        PSCSI_SUPPORTED_CONTROL_TYPE_LIST list = Parameters;
        if (!list) return ScsiAdapterControlUnsuccessful;
        if (list->MaxControlType > ScsiQuerySupportedControlTypes)
            list->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
        if (list->MaxControlType > ScsiStopAdapter)
            list->SupportedTypeList[ScsiStopAdapter] = TRUE;
        if (list->MaxControlType > ScsiRestartAdapter)
            list->SupportedTypeList[ScsiRestartAdapter] = TRUE;
        return ScsiAdapterControlSuccess;
    }
    /* The specific HwAdapterControl DDI table specifies DIRQL/InterruptLock
     * for Stop and Restart; no recursive lock and no resource frees here. */
    if (ControlType == ScsiStopAdapter) {
        return AppleAnsNativeStop(a) ? ScsiAdapterControlSuccess : ScsiAdapterControlUnsuccessful;
    }
    if (ControlType == ScsiRestartAdapter) {
#if APPLE_ANS_NATIVE_READ_ONLY
        return AppleAnsNativeInitialize(a) ? ScsiAdapterControlSuccess : ScsiAdapterControlUnsuccessful;
#else
        return ScsiAdapterControlUnsuccessful;
#endif
    }
    return ScsiAdapterControlUnsuccessful;
}
