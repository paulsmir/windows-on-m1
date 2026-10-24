#ifndef APPLE_AGX_RENDER_GPU_RECOVERY_SNAPSHOT_H
#define APPLE_AGX_RENDER_GPU_RECOVERY_SNAPSHOT_H

#include "render_gpu_fault.h"
#include "apple_agx_g13_queue_runtime.h"

/* Caller owns the runtime (Resetting set, WorkIdle observed). Never call from
 * the initial reset-state capture while a worker may still be updating it.
 * Reuse the runtime's checked shared-memory read; no new MMIO or ownership
 * changes. A failed read remains unknown, independently of the other words. */
static void AdmissionGpuRecoverySnapshot(
    const APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    ADMISSION_GPU_RECOVERY_SNAPSHOT *Snapshot) {
  ADMISSION_GPU_RECOVERY_SNAPSHOT empty = {0};
  const APPLE_AGX_G13_QUEUE_BINDING *bindings[3];
  const APPLE_AGX_G13_QUEUE_PENDING *pending[3];
  unsigned int index;
  *Snapshot = empty;
  Snapshot->Captured = 1u;
  Snapshot->Fence = Runtime->PendingFence;
  Snapshot->Phase = (unsigned int)Runtime->Phase;
  Snapshot->RecoveryPending = Runtime->RecoveryPending ? 1u : 0u;
  if (Runtime->Phase != AppleAgxG13QueueRuntimeSubmitted &&
      Runtime->Phase != AppleAgxG13QueueRuntimeComputeSubmitted)
    return;
  Snapshot->QueueMask = Runtime->ComputeTerminal ? 4u : 3u;
  if (Runtime->ComputeInRender)
    Snapshot->QueueMask |= 4u;
  bindings[0] = &Runtime->Config.Ta;
  bindings[1] = &Runtime->Config.D3;
  bindings[2] = &Runtime->Config.Compute;
  pending[0] = &Runtime->TaPending;
  pending[1] = &Runtime->D3Pending;
  pending[2] = &Runtime->ComputePending;
  for (index = 0u; index < 3u; ++index) {
    ADMISSION_GPU_RECOVERY_QUEUE *queue = &Snapshot->Queues[index];
    const APPLE_AGX_G13_QUEUE_BINDING *binding = bindings[index];
    APPLE_AGX_BACKEND_U32 value;
    if ((Snapshot->QueueMask & (1u << index)) == 0u)
      continue;
    queue->EventNumber = pending[index]->EventNumber;
    queue->Flags = (pending[index]->EventSeen ? 1u : 0u) |
                   (pending[index]->Complete ? 2u : 0u);
    queue->ExpectedStamp = pending[index]->ExpectedStamp;
    queue->ExpectedDone = pending[index]->ExpectedDonePointer;
    queue->Capacity = binding->RingCapacity;
    if (Runtime->Io.ReadU32 == 0)
      continue;
    if (binding->Stamp != 0 && Runtime->Io.ReadU32(
            Runtime->Io.Context, binding->Stamp, &value)) {
      queue->Stamp = value;
      queue->ReadMask |= 1u;
    }
    if (binding->GpuDonePointer != 0 && Runtime->Io.ReadU32(
            Runtime->Io.Context, binding->GpuDonePointer, &value)) {
      queue->Done = value;
      queue->ReadMask |= 2u;
    }
    if (binding->CpuWritePointer != 0 && Runtime->Io.ReadU32(
            Runtime->Io.Context, binding->CpuWritePointer, &value)) {
      queue->Write = value;
      queue->ReadMask |= 4u;
    }
  }
}

#endif /* APPLE_AGX_RENDER_GPU_RECOVERY_SNAPSHOT_H */
