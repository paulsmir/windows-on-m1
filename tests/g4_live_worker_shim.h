/* WDK boundary services for the real queue/worker/interrupt/DPC closure. */
#define _In_
#define _In_opt_
#define PVOID void *
#define PDEVICE_OBJECT void *
#define NT_SUCCESS(x) ((x)>=0)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xc0000184)
#define MAXULONGLONG (~0ULL)
#define MAXUINT (~0u)
#define DXGK_INTERRUPT_DMA_COMPLETED 1
#define DXGK_INTERRUPT_DMA_PAGE_FAULTED 2
#define DXGK_RENDER_PIPELINE_STAGE_UNKNOWN 0
#define DXGK_PAGE_FAULT_WRITE 1
#define DXGK_PAGE_FAULT_FLAGS unsigned
#define IO_NO_INCREMENT 0
#define RtlZeroMemory(p,n) memset(p,0,n)
#define UNREFERENCED_PARAMETER(x) ((void)x)
#define ADMISSION_SCHEDULER_NODE 0u
#define ADMISSION_SCHEDULER_ENGINE 0u
typedef struct {
 unsigned InterruptType;
 struct {unsigned SubmissionFenceId,NodeOrdinal,EngineOrdinal;} DmaCompleted;
 struct {unsigned FaultedFenceId;ULONGLONG FaultedPrimitiveAPISequenceNumber;
 unsigned FaultedPipelineStage,FaultedBindTableEntry,PageFaultFlags;
 ULONGLONG FaultedVirtualAddress;unsigned NodeOrdinal,EngineOrdinal,PageTableLevel;
 struct {unsigned IsDeviceSpecificCode,DeviceSpecificCode;} FaultErrorCode;} DmaPageFaulted;
} DXGKARGCB_NOTIFY_INTERRUPT_DATA;
typedef struct {
 void *DeviceHandle;
 NTSTATUS (*DxgkCbSynchronizeExecution)(void*,BOOLEAN(*)(void*),void*,unsigned,BOOLEAN*);
 void (*DxgkCbNotifyInterrupt)(void*,DXGKARGCB_NOTIFY_INTERRUPT_DATA*);
 BOOLEAN (*DxgkCbQueueDpc)(void*);
} WORKER_INTERFACE;
