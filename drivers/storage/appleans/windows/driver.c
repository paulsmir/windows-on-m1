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
    /* DUMP_POINTERS: the live instance answers SRB_FUNCTION_DUMP_POINTERS (native.c). */
    init.FeatureSupport = STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE |
                          STOR_FEATURE_ADAPTER_NOT_REQUIRE_IO_PORT |
                          STOR_FEATURE_DUMP_POINTERS;
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
    init.NumberOfAccessRanges = 2; /* broker: 1, direct: controller + mailbox */
    init.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    init.NeedPhysicalAddresses = TRUE;
    init.TaggedQueuing = TRUE;
    init.AutoRequestSense = TRUE;
    init.MultipleRequestPerLu = TRUE;
    return StorPortInitialize(DriverObject, RegistryPath, &init, NULL);
}

static BOOLEAN dump_ranges(PPORT_CONFIGURATION_INFORMATION config)
{
    if (config->NumberOfAccessRanges != 2 || config->AccessRanges == NULL) return FALSE;
    PACCESS_RANGE r = *config->AccessRanges;
    return (ULONGLONG)r[0].RangeStart.QuadPart == ANSD_CTRL_BASE &&
        r[0].RangeLength == ANSD_CTRL_SIZE && r[0].RangeInMemory &&
        (ULONGLONG)r[1].RangeStart.QuadPart == ANSD_MBOX_BASE &&
        r[1].RangeLength == ANSD_MBOX_SIZE && r[1].RangeInMemory;
}
static VOID adapter_limits(PPORT_CONFIGURATION_INFORMATION config, ULONG depth)
{
    config->MaximumTransferLength = ANS_MAX_TRANSFER;
    config->NumberOfPhysicalBreaks = ANS_MAX_DATA_PAGES;
    config->NumberOfBuses = 1;
    config->MaximumNumberOfTargets = 1;
    config->MaximumNumberOfLogicalUnits = 1;
    config->MaxIOsPerLun = depth;
    config->ScatterGather = TRUE;config->Master = TRUE;
    config->TaggedQueuing = TRUE;config->MultipleRequestPerLu = TRUE;
    config->AutoRequestSense = TRUE;
    config->MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    config->AlignmentMask = 3;
    config->SynchronizationModel = StorSynchronizeFullDuplex;
}
/* Dump image (dump_AppleAns.sys), possibly at HIGH_LEVEL: no registry, pool, DPC, timer, lock
 * or log. ANS-DUMP-MODE.md, TRANSLATION 2. Nothing is dereferenced, mapped or marked before
 * the live instance's record passed validation; without it there is no dump, never I/O.
 *   CRASH        map the two register ranges, adopt the lent buffer, one request at a time;
 *                the controller itself is first touched in HwInitialize.
 *   MARK_MEMORY  only names the memory the crash instance will use; no mapping, no hardware.
 *   HIBER/RESUME refused: hibernation on this adapter stays unsupported. */
static ULONG dump_find(PAPPLE_ANS_ADAPTER a, PPORT_CONFIGURATION_INFORMATION config)
{
    struct ans_dump_context *c = config->MiniportDumpData;
    ULONG mode = config->DumpMode;
    a->Stage = AppleAnsStageResourcesRejected;
    if (!ans_dump_context_valid(c, APPLE_ANS_DIRECT_BYTES)) return SP_RETURN_NOT_FOUND;
    ULONG flags = ANS_DUMP_SEEN_VALID | (dump_ranges(config) ? ANS_DUMP_SEEN_RANGES : 0);
    BOOLEAN found = FALSE;
#if APPLE_ANS_NATIVE_READ_ONLY
    a->DumpContext = *c;
    if (mode == DUMP_MODE_MARK_MEMORY) found = TRUE;
    else if (mode == DUMP_MODE_CRASH && (flags & ANS_DUMP_SEEN_RANGES)) {
        a->DumpMode = mode; /* AppleAnsDirectResources: map only, adopt the lent buffer */
        found = AppleAnsDirectResources(a, config);
        if (found) flags |= ANS_DUMP_SEEN_CTRL_MAPPED | ANS_DUMP_SEEN_MBOX_MAPPED;
    }
    if (found) {
        a->DumpMode = mode;a->Direct = TRUE;
        adapter_limits(config, 1);
        /* storahci marks its context in every dump mode; the return value is advisory. */
        (void)StorPortMarkDumpMemory(a, c, sizeof(*c), 0);
        (void)StorPortMarkDumpMemory(a, (PVOID)(ULONG_PTR)c->common_va, c->common_bytes, 0);
        a->ResourcesAccepted = TRUE;a->Stage = AppleAnsStageResourcesAccepted;
    }
#endif
    ans_dump_context_receipt(c, mode, flags, config->NumberOfAccessRanges);
    return found ? SP_RETURN_FOUND : SP_RETURN_NOT_FOUND;
}
/* Dump image: interrupts are off and nothing else runs, so no InterruptLock; the request is
 * finished by polling before StartIo returns (TRANSLATION 4). SHUTDOWN is the ordered FLUSH
 * of the live path; the controller is left running for the reset that follows, exactly as
 * after a bugcheck without a dump. */
static BOOLEAN dump_start(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb)
{
    if (a->DumpMode != DUMP_MODE_CRASH ||
        (srb->Function != SRB_FUNCTION_EXECUTE_SCSI && srb->Function != SRB_FUNCTION_FLUSH &&
         srb->Function != SRB_FUNCTION_SHUTDOWN)) {
        AppleAnsComplete(a, srb, SRB_STATUS_INVALID_REQUEST, 0);return TRUE;
    }
    BOOLEAN result = AppleAnsNativeStart(a, srb);
    AppleAnsDirectDumpDrain(a);
    return result;
}

/* PASSIVE_LEVEL (FindAdapter). Services\AppleAns\Parameters\Device, REG_DWORD each:
 * UnmapMode 0 off / 1 observe / 2 deallocate, UnmapFenceStart and UnmapFenceEnd in namespace
 * LBAs (end exclusive). Anything missing or unreadable resolves to the safe side
 * (ans_trim_config_make): observe, and never deallocate without a fence. */
static VOID unmap_config(PAPPLE_ANS_ADAPTER a)
{
    static const char *const names[3] = {"UnmapMode", "UnmapFenceStart", "UnmapFenceEnd"};
    ULONG value[3] = {0}, have = 0, length = sizeof(ULONG);
    PUCHAR buffer = StorPortAllocateRegistryBuffer(a, &length);
    if (buffer) {
        for (unsigned i = 0; i < 3 && length >= sizeof(ULONG); i++) {
            ULONG bytes = sizeof(ULONG);
            RtlZeroMemory(buffer, sizeof(ULONG));
            if (StorPortRegistryRead(a, (PUCHAR)names[i], 1, MINIPORT_REG_DWORD, buffer, &bytes) &&
                bytes == sizeof(ULONG)) {
                RtlCopyMemory(&value[i], buffer, sizeof(ULONG));
                have |= 1u << i;
            }
        }
        StorPortFreeRegistryBuffer(a, buffer);
    }
    a->Trim.config = ans_trim_config_make(have, value[0], value[1], value[2]);
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
    if (ConfigInfo->Length < sizeof(*ConfigInfo)) return SP_RETURN_NOT_FOUND;
    /* The dump image gets its own, never-started extension: decided before anything in it
     * is read. */
    BOOLEAN dump = ConfigInfo->DumpMode != 0;
    /* A PnP restart must never erase ownership after an unproven stop. */
    if (!dump && a->Signature == ANS_MAGIC && (a->MayOwn || a->Wire.faulted || a->Ansd.faulted))
        return SP_RETURN_ERROR;
    RtlZeroMemory(a, sizeof(*a));
    a->Signature = ANS_MAGIC;a->Version = 2;a->Stage = AppleAnsStageFindAdapter;
    (void)ans_order_init(&a->Order, 1);
    ans_stats_init(&a->Stats);
    a->ShutdownTag=ANS_QUEUE_DEPTH;
    if (dump) return dump_find(a, ConfigInfo);
    if (ConfigInfo->NumberOfAccessRanges < 1 || ConfigInfo->NumberOfAccessRanges > 2 ||
        ConfigInfo->AccessRanges == NULL) {
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
    BOOLEAN direct=FALSE;
    if (ConfigInfo->NumberOfAccessRanges == 2) {
        PACCESS_RANGE range2 = &(*ConfigInfo->AccessRanges)[1];
        resources.base2=(ULONGLONG)range2->RangeStart.QuadPart;
        resources.length2=range2->RangeLength;resources.memory2=range2->RangeInMemory;
        direct=ans_direct_resources_valid(&resources) != 0;
    }
    if (!direct && !ans_resources_valid(&resources)) {
        a->Stage = AppleAnsStageResourcesRejected;return SP_RETURN_NOT_FOUND;
    }
    a->BrokerAddress = range->RangeStart;a->BrokerLength = range->RangeLength;
    a->AssignedInterruptVector = ConfigInfo->BusInterruptVector;
    a->AssignedInterruptLevel = ConfigInfo->BusInterruptLevel;
    adapter_limits(ConfigInfo, ANS_QUEUE_DEPTH - ANS_ADMIN_TAGS);
#if APPLE_ANS_NATIVE_READ_ONLY
    if (direct ? !AppleAnsDirectResources(a, ConfigInfo) : !AppleAnsNativeResources(a, ConfigInfo)) {
        a->Stage = AppleAnsStageResourcesRejected;return SP_RETURN_ERROR;
    }
#endif
    unmap_config(a);
    a->ResourcesAccepted = TRUE;a->Stage = AppleAnsStageResourcesAccepted;
    return SP_RETURN_FOUND;
}

_Use_decl_annotations_
BOOLEAN AppleAnsInitialize(PVOID DeviceExtension)
{
    PAPPLE_ANS_ADAPTER a = DeviceExtension;
    /* Dump image: no passive initialisation, DPC or log. MARK_MEMORY never starts hardware. */
    if (a->DumpMode)
        return a->ResourcesAccepted &&
            (a->DumpMode == DUMP_MODE_MARK_MEMORY || AppleAnsDirectDumpInitialize(a));
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
                x->DiagValue=(0xFu<<20);x->DiagCount=sg?sg->NumberOfElements:0;
                x->PrpFailure=TRUE;return TRUE;
            }
            for (unsigned i=0;i<sg->NumberOfElements;i++) {
                spans[i].address = (ULONGLONG)sg->List[i].PhysicalAddress.QuadPart;
                spans[i].length = sg->List[i].Length;
            }
            /* A first address inside a page is never handed to the controller (ans_bounce.h): the
             * transfer goes through a bounce buffer that StartIo claims. Here, at DISPATCH_LEVEL
             * without locks, only the SRB's data is mapped; no PRPs are built from the SG list.
             * A buffer that cannot be mapped is retried (BUSY), never refused. The dump instance
             * (page-sized dump buffers, IRQL above DISPATCH) keeps the direct path. */
            PAPPLE_ANS_ADAPTER a = DeviceExtension;
            if (a->Direct && !a->DumpMode && ans_bounce_needed((ULONGLONG)sg->List[0].PhysicalAddress.QuadPart)) {
                x->Misaligned=TRUE;x->MisalignedLow=(UCHAR)(sg->List[0].PhysicalAddress.QuadPart & 0xf);
                if (ans_bounce_size_ok(x->Length)) {
                    PVOID va=NULL;
                    if (StorPortGetSystemAddress(DeviceExtension,Srb,&va)==STOR_STATUS_SUCCESS && va) {
                        /* Self-check: the mapping must be THIS request's data, not the start of a
                         * larger MDL it was split from (the documentation does not say which).
                         * The first mapped byte and the first SG byte must be the same memory. */
                        if ((ULONGLONG)MmGetPhysicalAddress(va).QuadPart ==
                            (ULONGLONG)sg->List[0].PhysicalAddress.QuadPart) {
                            x->NeedBounce=TRUE;x->BounceVa=va;
                        } else x->BounceMismatch=TRUE;
                    } else x->BounceDefer=TRUE;
                    x->Valid=TRUE;
                    return TRUE;
                }
            }
            /* Direct mode accepts the memory manager's repeated dummy page; the broker never did. */
            int prp_error=ans_sg_to_prps_ex(spans,sg->NumberOfElements,x->Length,&x->Prps,
                                            ((PAPPLE_ANS_ADAPTER)DeviceExtension)->Direct);
            if (prp_error != ANS_PRP_OK) {
                x->DiagValue=((ULONG)prp_error<<20)|(ULONG)((spans[0].address>>12)&0xfffff);
                x->DiagCount=sg->NumberOfElements;
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
    if (((PAPPLE_ANS_ADAPTER)DeviceExtension)->DumpMode) return dump_start(DeviceExtension,Srb);
    StorPortAcquireSpinLock(DeviceExtension,InterruptLock,NULL,&lock);
    BOOLEAN result = AppleAnsNativeStart(DeviceExtension,Srb);
    StorPortReleaseSpinLock(DeviceExtension,&lock);
    return result;
}

_Use_decl_annotations_
BOOLEAN AppleAnsInterrupt(PVOID DeviceExtension)
{
    /* Storport already holds InterruptLock; never acquire it recursively. */
    /* Dump image: StartIo already drained the queue; a polled call finds nothing to do. */
    if (((PAPPLE_ANS_ADAPTER)DeviceExtension)->DumpMode) return FALSE;
    return AppleAnsNativeInterrupt(DeviceExtension);
}

_Use_decl_annotations_
BOOLEAN AppleAnsResetBus(PVOID DeviceExtension, ULONG PathId)
{
    if (PathId) return FALSE;
    /* Dump image: a controller reset here could only lose the dump; report success untouched. */
    if (((PAPPLE_ANS_ADAPTER)DeviceExtension)->DumpMode) return TRUE;
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
        if (a->DumpMode) return ScsiAdapterControlUnsuccessful;
#if APPLE_ANS_NATIVE_READ_ONLY
        return AppleAnsNativeInitialize(a) ? ScsiAdapterControlSuccess : ScsiAdapterControlUnsuccessful;
#else
        return ScsiAdapterControlUnsuccessful;
#endif
    }
    return ScsiAdapterControlUnsuccessful;
}
