#ifndef ADMISSION_RENDER_DDI_STATUS_H
#define ADMISSION_RENDER_DDI_STATUS_H

/* Fixed v1 wire layout. WDK types only. Slots 0..79: registered NTSTATUS
 * DDIs; 80..111: outward BuildPagingBuffer by operation (31=unknown);
 * 112..127: pre-normalization paging status (15=unknown). Image lifetime.
 * Count includes expected retry statuses; a failure is not a removal verdict.
 * Ready: 0=empty, 1=writer owns, 2=immutable first record published. */
#define ADMISSION_DDI_STATUS_SLOTS 128u
typedef struct _ADMISSION_DDI_STATUS_SLOT {
  volatile LONG64 Count;
  ULONGLONG Qpc, Allocation, Object;
  ULONG Operation, Status;
  volatile LONG Ready;
  ULONG Reserved;
} ADMISSION_DDI_STATUS_SLOT;

static int AdmissionDdiStatusAccumulate(ADMISSION_DDI_STATUS_SLOT *Slot,
    NTSTATUS Status, ULONGLONG Qpc, ULONGLONG Allocation,
    ULONGLONG Object, ULONG Operation) {
  if (NT_SUCCESS(Status)) return 0;
  InterlockedIncrement64(&Slot->Count);
  if (InterlockedCompareExchange(&Slot->Ready, 1, 0) != 0) return 0;
  Slot->Qpc = Qpc;
  Slot->Allocation = Allocation;
  Slot->Object = Object;
  Slot->Operation = Operation;
  Slot->Status = (ULONG)Status;
  InterlockedExchange(&Slot->Ready, 2);
  return 1;
}
#endif
