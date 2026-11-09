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

unsigned int AdmissionGpuResetFailureClass(unsigned int Quiesce,
    unsigned int RtkitCrashed, unsigned int Halted,
    unsigned int RecoveryPending) {
  /* Only a wait this thread ran to a failed end has a class: the outcomes
   * None/Ended/Unsubmitted succeeded, Worker and State never owned the
   * runtime, so its firmware words were not this thread's to read. */
  if (Quiesce != (unsigned int)AdmissionGpuResetQuiesceTimeout &&
      Quiesce != (unsigned int)AdmissionGpuResetQuiescePoll &&
      Quiesce != (unsigned int)AdmissionGpuResetQuiesceBackend)
    return ADMISSION_GPU_RESET_CLASS_NONE;
  if (RtkitCrashed != 0u)
    return ADMISSION_GPU_RESET_CLASS_F2;
  if ((Halted != 0u && Halted != 0xffffffffu) || RecoveryPending != 0u)
    return ADMISSION_GPU_RESET_CLASS_F1;
  return ADMISSION_GPU_RESET_CLASS_F3;
}
