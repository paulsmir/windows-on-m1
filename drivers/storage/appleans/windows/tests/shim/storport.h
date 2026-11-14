/* SPDX-License-Identifier: MIT */
/* Host stand-in for <storport.h>: the Storport surface the AppleAns miniport uses, with the
 * documented signatures. Implemented by tests/storport_shim_test.c. Test code. */
#ifndef SHIM_STORPORT_H
#define SHIM_STORPORT_H
#include "ntddk.h"
typedef PHYSICAL_ADDRESS STOR_PHYSICAL_ADDRESS;
typedef struct { STOR_PHYSICAL_ADDRESS RangeStart; ULONG RangeLength; BOOLEAN RangeInMemory; } ACCESS_RANGE, *PACCESS_RANGE;
typedef enum { InterfaceTypeUndefined = -1, Internal = 0, ACPIBus = 17 } INTERFACE_TYPE;
typedef enum { LevelSensitive = 0, Latched = 1 } KINTERRUPT_MODE;
typedef enum { StorSynchronizeHalfDuplex = 0, StorSynchronizeFullDuplex = 1 } STOR_SYNCHRONIZATION_MODEL;
typedef struct _PORT_CONFIGURATION_INFORMATION {
    ULONG Length, SystemIoBusNumber;
    INTERFACE_TYPE AdapterInterfaceType;
    ULONG BusInterruptLevel, BusInterruptVector, BusInterruptLevel2, BusInterruptVector2;
    KINTERRUPT_MODE InterruptMode;
    ULONG MaximumTransferLength, NumberOfPhysicalBreaks, NumberOfAccessRanges;
    ACCESS_RANGE (*AccessRanges)[];
    UCHAR NumberOfBuses, MaximumNumberOfTargets, MaximumNumberOfLogicalUnits;
    BOOLEAN ScatterGather, Master, AutoRequestSense, TaggedQueuing, MultipleRequestPerLu, VirtualDevice;
    ULONG MapBuffers, AlignmentMask, Dma64BitAddresses, MaxIOsPerLun, DumpMode;
    STOR_SYNCHRONIZATION_MODEL SynchronizationModel;
    PVOID MiniportDumpData;
} PORT_CONFIGURATION_INFORMATION, *PPORT_CONFIGURATION_INFORMATION;
typedef struct _SCSI_REQUEST_BLOCK {
    USHORT Length;
    UCHAR Function, SrbStatus, ScsiStatus, PathId, TargetId, Lun, QueueAction, CdbLength, SenseInfoBufferLength;
    ULONG SrbFlags, DataTransferLength;
    PVOID DataBuffer, SenseInfoBuffer, SrbExtension;
    UCHAR Cdb[16];
} SCSI_REQUEST_BLOCK, *PSCSI_REQUEST_BLOCK;
typedef struct { STOR_PHYSICAL_ADDRESS PhysicalAddress; ULONG Length; ULONG_PTR Reserved; } STOR_SCATTER_GATHER_ELEMENT;
typedef struct { ULONG NumberOfElements; ULONG_PTR Reserved; STOR_SCATTER_GATHER_ELEMENT List[64]; } STOR_SCATTER_GATHER_LIST, *PSTOR_SCATTER_GATHER_LIST;
typedef struct { int held; } STOR_LOCK_HANDLE, *PSTOR_LOCK_HANDLE;
typedef enum { DpcLock, StartIoLock, InterruptLock } STOR_SPINLOCK;
typedef struct { PVOID routine; } STOR_DPC, *PSTOR_DPC;
typedef struct { USHORT Version, Size; PVOID MiniportPrivateDumpData; } MINIPORT_DUMP_POINTERS, *PMINIPORT_DUMP_POINTERS;
#define DUMP_MINIPORT_VERSION_1 1
#define DUMP_MODE_CRASH 1
#define DUMP_MODE_HIBER 2
#define DUMP_MODE_MARK_MEMORY 3
#define DUMP_MODE_RESUME 4
typedef enum { ScsiQuerySupportedControlTypes = 0, ScsiStopAdapter, ScsiRestartAdapter, ScsiSetBootConfig,
               ScsiSetRunningConfig, ScsiAdapterControlMax } SCSI_ADAPTER_CONTROL_TYPE;
typedef enum { ScsiAdapterControlSuccess = 0, ScsiAdapterControlUnsuccessful } SCSI_ADAPTER_CONTROL_STATUS;
typedef struct { ULONG MaxControlType; BOOLEAN SupportedTypeList[16]; } SCSI_SUPPORTED_CONTROL_TYPE_LIST, *PSCSI_SUPPORTED_CONTROL_TYPE_LIST;
typedef ULONG sp_DRIVER_INITIALIZE(PVOID, PVOID);
typedef ULONG HW_FIND_ADAPTER(PVOID, PVOID, PVOID, PCHAR, PPORT_CONFIGURATION_INFORMATION, PBOOLEAN);
typedef BOOLEAN HW_INITIALIZE(PVOID);
typedef BOOLEAN HW_PASSIVE_INITIALIZE_ROUTINE(PVOID);
typedef BOOLEAN HW_BUILDIO(PVOID, PSCSI_REQUEST_BLOCK);
typedef BOOLEAN HW_STARTIO(PVOID, PSCSI_REQUEST_BLOCK);
typedef BOOLEAN HW_INTERRUPT(PVOID);
typedef BOOLEAN HW_RESET_BUS(PVOID, ULONG);
typedef SCSI_ADAPTER_CONTROL_STATUS HW_ADAPTER_CONTROL(PVOID, SCSI_ADAPTER_CONTROL_TYPE, PVOID);
typedef VOID HW_TIMER(PVOID);
typedef VOID HW_DPC_ROUTINE(PSTOR_DPC, PVOID, PVOID, PVOID);
typedef struct _HW_INITIALIZATION_DATA {
    ULONG HwInitializationDataSize;
    INTERFACE_TYPE AdapterInterfaceType;
    ULONG FeatureSupport;
    PVOID HwFindAdapter;
    HW_INITIALIZE *HwInitialize;
    HW_BUILDIO *HwBuildIo;
    HW_STARTIO *HwStartIo;
    HW_INTERRUPT *HwInterrupt;
    HW_RESET_BUS *HwResetBus;
    HW_ADAPTER_CONTROL *HwAdapterControl;
    ULONG DeviceExtensionSize, SrbExtensionSize, NumberOfAccessRanges, MapBuffers;
    BOOLEAN NeedPhysicalAddresses, TaggedQueuing, AutoRequestSense, MultipleRequestPerLu;
} HW_INITIALIZATION_DATA, *PHW_INITIALIZATION_DATA;
#define STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE 0x1
#define STOR_FEATURE_ADAPTER_NOT_REQUIRE_IO_PORT 0x2
#define STOR_FEATURE_DUMP_POINTERS 0x4
#define STOR_MAP_NON_READ_WRITE_BUFFERS 1
#define SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED 0x20
#define SP_RETURN_NOT_FOUND 0
#define SP_RETURN_FOUND 1
#define SP_RETURN_ERROR 2
#define SP_INTERNAL_ADAPTER_ERROR 0x0006
#define STOR_STATUS_SUCCESS 0u
#define STOR_STATUS_INSUFFICIENT_RESOURCES 0xC1000002u
#define MINIPORT_REG_DWORD 4
#define REG_DWORD 4
#define SRB_FUNCTION_EXECUTE_SCSI 0x00
#define SRB_FUNCTION_IO_CONTROL 0x02
#define SRB_FUNCTION_SHUTDOWN 0x07
#define SRB_FUNCTION_FLUSH 0x08
#define SRB_FUNCTION_DUMP_POINTERS 0x26
#define SRB_FUNCTION_FREE_DUMP_POINTERS 0x27
#define SRB_STATUS_PENDING 0x00
#define SRB_STATUS_SUCCESS 0x01
#define SRB_STATUS_ERROR 0x04
#define SRB_STATUS_BUSY 0x05
#define SRB_STATUS_INVALID_REQUEST 0x06
#define SRB_STATUS_NO_DEVICE 0x08
#define SRB_STATUS_BUS_RESET 0x0e
#define SRB_STATUS_AUTOSENSE_VALID 0x80
#define SRB_FLAGS_QUEUE_ACTION_ENABLE 0x00000002
#define SRB_FLAGS_DISABLE_AUTOSENSE 0x00000020
#define SRB_FLAGS_DATA_IN 0x00000040
#define SRB_FLAGS_DATA_OUT 0x00000080
#define SRB_SIMPLE_TAG_REQUEST 0x20
#define SRB_HEAD_OF_QUEUE_TAG_REQUEST 0x21
#define SRB_ORDERED_QUEUE_TAG_REQUEST 0x22
#define SCSISTAT_GOOD 0x00
#define SCSISTAT_CHECK_CONDITION 0x02
typedef enum { RequestComplete = 0, RequestTimerCall = 3 } SCSI_NOTIFICATION_TYPE;
typedef ULONG NODE_REQUIREMENT;
#define MM_ANY_NODE_OK 0x80000000u
ULONG StorPortInitialize(PVOID, PVOID, PHW_INITIALIZATION_DATA, PVOID);
PVOID StorPortGetDeviceBase(PVOID, INTERFACE_TYPE, ULONG, STOR_PHYSICAL_ADDRESS, ULONG, BOOLEAN);
VOID StorPortFreeDeviceBase(PVOID, PVOID);
PVOID StorPortGetUncachedExtension(PVOID, PPORT_CONFIGURATION_INFORMATION, ULONG);
STOR_PHYSICAL_ADDRESS StorPortGetPhysicalAddress(PVOID, PSCSI_REQUEST_BLOCK, PVOID, ULONG *);
ULONG StorPortAllocateContiguousMemorySpecifyCacheNode(PVOID, SIZE_T, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS,
    PHYSICAL_ADDRESS, MEMORY_CACHING_TYPE, NODE_REQUIREMENT, PVOID *);
ULONG StorPortFreeContiguousMemorySpecifyCache(PVOID, PVOID, SIZE_T, MEMORY_CACHING_TYPE);
ULONG StorPortReadRegisterUlong(PVOID, PULONG);
ULONG64 StorPortReadRegisterUlong64(PVOID, PULONG64);
VOID StorPortWriteRegisterUlong(PVOID, PULONG, ULONG);
VOID StorPortWriteRegisterUlong64(PVOID, PULONG64, ULONG64);
VOID StorPortStallExecution(ULONG);
VOID StorPortAcquireSpinLock(PVOID, STOR_SPINLOCK, PVOID, PSTOR_LOCK_HANDLE);
VOID StorPortReleaseSpinLock(PVOID, PSTOR_LOCK_HANDLE);
VOID StorPortNotification(SCSI_NOTIFICATION_TYPE, PVOID, ...);
VOID StorPortLogError(PVOID, PSCSI_REQUEST_BLOCK, UCHAR, UCHAR, UCHAR, ULONG, ULONG);
BOOLEAN StorPortIssueDpc(PVOID, PSTOR_DPC, PVOID, PVOID);
VOID StorPortInitializeDpc(PVOID, PSTOR_DPC, HW_DPC_ROUTINE *);
BOOLEAN StorPortEnablePassiveInitialization(PVOID, HW_PASSIVE_INITIALIZE_ROUTINE *);
ULONG StorPortQueryPerformanceCounter(PVOID, PLARGE_INTEGER, PLARGE_INTEGER);
PSTOR_SCATTER_GATHER_LIST StorPortGetScatterGatherList(PVOID, PSCSI_REQUEST_BLOCK);
ULONG StorPortGetSystemAddress(PVOID, PSCSI_REQUEST_BLOCK, PVOID *);
BOOLEAN StorPortDeviceBusy(PVOID, UCHAR, UCHAR, UCHAR, ULONG);
ULONG StorPortMarkDumpMemory(PVOID, PVOID, ULONG, ULONG);
PUCHAR StorPortAllocateRegistryBuffer(PVOID, PULONG);
VOID StorPortFreeRegistryBuffer(PVOID, PUCHAR);
BOOLEAN StorPortRegistryRead(PVOID, PUCHAR, ULONG, ULONG, PUCHAR, PULONG);
#endif
