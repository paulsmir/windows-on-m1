from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/paging_windows.c"


class PresentFaultNotificationTests(unittest.TestCase):
    def test_worker_preserves_fault_address_and_failure_status(self):
        source = SOURCE.read_text()
        gpuva = (ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_windows.c").read_text()
        self.assertIn("context->PresentCopyFaultVa = 0ULL", source)
        self.assertIn("notification.FaultVa = context->PresentCopyBytes != 0u", source)
        self.assertIn("context->PagingCompletionStatus = status", source)
        self.assertIn("Adapter->PresentCopyFaultVa = copy.FaultVa", gpuva)

    def test_unresolved_present_page_reports_fault_not_completion(self):
        source = SOURCE.read_text()
        notification = re.search(
            r"typedef struct _ADMISSION_PAGING_NOTIFICATION \{.*?\} ADMISSION_PAGING_NOTIFICATION;",
            source, re.S,
        )
        callback = re.search(
            r"static BOOLEAN AdmissionPagingNotifyAtInterrupt\(.*?^}",
            source, re.S | re.M,
        )
        self.assertIsNotNone(notification)
        self.assertIsNotNone(callback)
        shim = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define FALSE 0
#define TRUE 1
#define STATUS_SUCCESS 0
#define STATUS_INVALID_ADDRESS (-3)
#define NT_SUCCESS(status) ((status)>=0)
#define DXGK_INTERRUPT_DMA_COMPLETED 1
#define DXGK_INTERRUPT_DMA_FAULTED 4
#define DXGK_INTERRUPT_DMA_PAGE_FAULTED 9
#define DXGK_PAGE_FAULT_WRITE 1
#define DXGK_RENDER_PIPELINE_STAGE_UNKNOWN 0
#define DXGK_BIND_TABLE_ENTRY_UNKNOWN 0xffffffffu
#define DXGK_PRIMITIVE_API_SEQUENCE_NUMBER_UNKNOWN (~0ULL)
#define RtlZeroMemory(pointer,bytes) memset(pointer,0,bytes)
typedef int NTSTATUS,BOOLEAN,LONG;
typedef unsigned int UINT,ULONG,DXGK_PAGE_FAULT_FLAGS;
typedef uint64_t ULONGLONG;
typedef void *PVOID;
typedef struct {
  unsigned InterruptType;
  struct {unsigned SubmissionFenceId,NodeOrdinal,EngineOrdinal;} DmaCompleted;
  struct {unsigned FaultedFenceId;NTSTATUS Status;unsigned NodeOrdinal,EngineOrdinal;} DmaFaulted;
  struct {unsigned FaultedFenceId;ULONGLONG FaultedPrimitiveAPISequenceNumber;
    unsigned FaultedPipelineStage,FaultedBindTableEntry,PageFaultFlags;
    ULONGLONG FaultedVirtualAddress;unsigned NodeOrdinal,EngineOrdinal,PageTableLevel;
    struct {unsigned IsDeviceSpecificCode:1,GeneralErrorCode:31;} FaultErrorCode;
    void *FaultedProcessHandle;} DmaPageFaulted;
} DXGKARGCB_NOTIFY_INTERRUPT_DATA;
typedef struct {unsigned Fence,NotifyInterrupt;} ADMISSION_PRESENT_TRANSFER_RECEIPT;
typedef struct _ADMISSION_CONTEXT {
  int InterfaceValid;
  struct {void *DeviceHandle;void (*DxgkCbNotifyInterrupt)(void*,DXGKARGCB_NOTIFY_INTERRUPT_DATA*);
    int (*DxgkCbQueueDpc)(void*);} Interface;
  LONG PresentTransferState,PagingDpcPending;
  ADMISSION_PRESENT_TRANSFER_RECEIPT PresentTransferReceipt;
} ADMISSION_CONTEXT;
static DXGKARGCB_NOTIFY_INTERRUPT_DATA observed;
static int notified,queued;
static void notify(void *device,DXGKARGCB_NOTIFY_INTERRUPT_DATA *data){
  assert(device==(void*)0x1234);observed=*data;++notified;}
static int queue(void *device){assert(device==(void*)0x1234);++queued;return 1;}
static LONG InterlockedCompareExchange(LONG *value,LONG exchange,LONG compare){
  LONG previous=*value;if(previous==compare)*value=exchange;return previous;}
static LONG InterlockedExchange(LONG *value,LONG exchange){LONG previous=*value;*value=exchange;return previous;}
'''
        test = r'''
int main(void){
  ADMISSION_CONTEXT context={0};ADMISSION_PAGING_NOTIFICATION notification={0};
  context.InterfaceValid=1;context.Interface.DeviceHandle=(void*)0x1234;
  context.Interface.DxgkCbNotifyInterrupt=notify;context.Interface.DxgkCbQueueDpc=queue;
  notification.Context=&context;notification.Fence=0x6c54;notification.Status=STATUS_INVALID_ADDRESS;
  notification.FaultVa=0x1054000ULL;
  assert(AdmissionPagingNotifyAtInterrupt(&notification));
  assert(notified==1 && queued==1 && observed.InterruptType==DXGK_INTERRUPT_DMA_PAGE_FAULTED);
  assert(observed.DmaPageFaulted.FaultedFenceId==0x6c54);
  assert(observed.DmaPageFaulted.FaultedVirtualAddress==0x1054000ULL);
  assert(observed.DmaPageFaulted.PageFaultFlags==0);
  notification.FaultWrite=1;notification.FaultVa=0xb4000ULL;
  assert(AdmissionPagingNotifyAtInterrupt(&notification));
  assert(observed.InterruptType==DXGK_INTERRUPT_DMA_PAGE_FAULTED);
  assert(observed.DmaPageFaulted.PageFaultFlags==DXGK_PAGE_FAULT_WRITE);
  notification.Status=STATUS_SUCCESS;notification.FaultVa=0;
  assert(AdmissionPagingNotifyAtInterrupt(&notification));
  assert(observed.InterruptType==DXGK_INTERRUPT_DMA_COMPLETED);
  assert(observed.DmaCompleted.SubmissionFenceId==0x6c54);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            program = Path(temp) / "fault.c"
            executable = Path(temp) / "fault"
            program.write_text(shim + notification.group(0) + callback.group(0) + test)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", str(program),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
