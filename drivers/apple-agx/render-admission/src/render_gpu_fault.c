#include "render_gpu_fault.h"

static void AdmissionGpuFaultZero(void *Bytes, unsigned int Count) {
  unsigned char *bytes = (unsigned char *)Bytes;
  unsigned int index;
  for (index = 0u; index < Count; ++index)
    bytes[index] = 0u;
}

void AdmissionGpuFaultReceiptInitialize(ADMISSION_GPU_FAULT_RECEIPT *Receipt) {
  if (Receipt == (ADMISSION_GPU_FAULT_RECEIPT *)0)
    return;
  AdmissionGpuFaultZero(Receipt, (unsigned int)sizeof(*Receipt));
  Receipt->Version = ADMISSION_GPU_FAULT_RECEIPT_VERSION;
  Receipt->Bytes = (unsigned int)sizeof(*Receipt);
}

void AdmissionGpuFaultReceiptRecord(ADMISSION_GPU_FAULT_RECEIPT *Receipt,
    ADMISSION_GPU_FAULT_EVENT *Event) {
  if (Receipt == (ADMISSION_GPU_FAULT_RECEIPT *)0 ||
      Event == (ADMISSION_GPU_FAULT_EVENT *)0 ||
      Receipt->Version != ADMISSION_GPU_FAULT_RECEIPT_VERSION ||
      Receipt->Events == 0xffffffffu)
    return;
  Event->Sequence = ++Receipt->Events;
  if (Event->Flags & ADMISSION_GPU_FAULT_RESUMED)
    ++Receipt->Resumed;
  else
    ++Receipt->NotHalted;
  if (Event->Flags & ADMISSION_GPU_FAULT_INFO_VALID) {
    if (Receipt->ValidFaults++ == 0u)
      Receipt->FirstValid = *Event;
  }
  if (Event->Sequence == 1u)
    Receipt->First = *Event;
  Receipt->Last = *Event;
}
