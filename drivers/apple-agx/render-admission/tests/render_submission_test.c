#include "render_submission.h"
#include "render_gdi_receipt.h"

#include <assert.h>
#include <string.h>

typedef struct _OUTPUT_PROGRESS_TEST {
  unsigned int Calls;
  unsigned int FailAt;
} OUTPUT_PROGRESS_TEST;

static int output_progress(void *Context) {
  OUTPUT_PROGRESS_TEST *progress = (OUTPUT_PROGRESS_TEST *)Context;
  ++progress->Calls;
  return progress->FailAt == 0u || progress->Calls != progress->FailAt;
}

static ADMISSION_RENDER_PACKET_DESCRIPTION packet_description(
    unsigned int fence) {
  ADMISSION_RENDER_PACKET_DESCRIPTION description = {0};
  description.Fence = fence;
  description.ContextToken = 0x1000ULL;
  description.AllocationToken = 0x2000ULL;
  description.PrivateDataToken = 0x3000ULL;
  description.PrivateDataBytes = 8192u;
  description.PrivateDataStart = 0u;
  description.PrivateDataEnd = 256u;
  description.DmaStart = 0u;
  description.DmaEnd = 160u;
  description.PatchOffset = 8u;
  description.DestinationCpuToken = 0x4000ULL;
  description.DestinationGpuVa = 0x1500010000ULL;
  description.DestinationPhysical = 0x9d0010000ULL;
  description.DestinationBytes = 0x10000u;
  description.DestinationIndex = 1u;
  description.VisibleDestinationCpuToken = 0x5000ULL;
  description.VisibleDestinationGpuVa = 0x1500100000ULL;
  description.VisibleDestinationPhysical = 0x9d0100000ULL;
  description.VisibleDestinationAllocationToken = 0x6000ULL;
  description.VisibleDestinationBytes = 0xfa0000u;
  return description;
}

static void test_exact_packet_moves_prepared_queued_active_completed(void) {
  ADMISSION_RENDER_PACKET packet;
  ADMISSION_RENDER_PACKET_DESCRIPTION description =
      packet_description(11u);

  AdmissionRenderPacketInitialize(&packet);
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketEmpty);
  assert(AdmissionRenderPacketPrepare(&packet, &description));
  assert(AdmissionRenderPacketMatches(&packet, &description,
                                      AdmissionRenderPacketPrepared));
  description.DmaEnd++;
  assert(!AdmissionRenderPacketMatches(&packet, &description,
                                       AdmissionRenderPacketPrepared));
  description.DmaEnd--;
  description.VisibleDestinationAllocationToken++;
  assert(!AdmissionRenderPacketMatches(&packet, &description,
                                       AdmissionRenderPacketPrepared));
  description.VisibleDestinationAllocationToken--;
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketPrepared);
  assert(!AdmissionRenderPacketComplete(&packet, 11u));
  assert(!AdmissionRenderPacketQueue(&packet, 12u, 0x1000ULL,
                                     0x3000ULL, 0u, 160u));
  assert(AdmissionRenderPacketQueue(&packet, 11u, 0x1000ULL,
                                    0x3000ULL, 0u, 160u));
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketQueued);
  assert(!AdmissionRenderPacketActivate(&packet, 12u));
  assert(AdmissionRenderPacketActivate(&packet, 11u));
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketActive);
  assert(!AdmissionRenderPacketComplete(&packet, 12u));
  assert(AdmissionRenderPacketComplete(&packet, 11u));
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketEmpty);
}

static void test_prepatched_capture_adopt_and_worker_copy_are_exact(void) {
  ADMISSION_PREPATCHED_RENDER pending;
  ADMISSION_RENDER_PACKET packet;
  ADMISSION_RENDER_PACKET_DESCRIPTION captured = packet_description(0u);
  ADMISSION_RENDER_PACKET_DESCRIPTION adopted;
  ADMISSION_RENDER_PACKET_DESCRIPTION worker;

  AdmissionPrepatchedInitialize(&pending);
  AdmissionRenderPacketInitialize(&packet);
  assert(AdmissionPrepatchedCapture(&pending, &captured));
  assert(AdmissionPrepatchedActive(&pending));
  assert(AdmissionPrepatchedAdopt(
      &pending, 41u, captured.ContextToken, captured.PrivateDataToken,
      captured.DmaStart, captured.DmaEnd, &adopted));
  assert(!AdmissionPrepatchedActive(&pending));
  assert(adopted.Fence == 41u);
  assert(adopted.AllocationToken == 0x2000ULL);
  assert(adopted.DestinationCpuToken == 0x4000ULL);
  assert(adopted.DestinationGpuVa == 0x1500010000ULL);
  assert(adopted.DestinationPhysical == 0x9d0010000ULL);
  assert(adopted.DestinationBytes == 0x10000u);
  assert(adopted.DestinationIndex == 1u);
  assert(adopted.VisibleDestinationCpuToken == 0x5000ULL);
  assert(adopted.VisibleDestinationGpuVa == 0x1500100000ULL);
  assert(adopted.VisibleDestinationPhysical == 0x9d0100000ULL);
  assert(adopted.VisibleDestinationAllocationToken == 0x6000ULL);
  assert(adopted.VisibleDestinationBytes == 0xfa0000u);
  assert(AdmissionRenderPacketPrepare(&packet, &adopted));
  assert(AdmissionRenderPacketQueue(
      &packet, 41u, captured.ContextToken, captured.PrivateDataToken,
      captured.DmaStart, captured.DmaEnd));
  assert(AdmissionRenderPacketActivate(&packet, 41u));
  worker = packet.Description;
  assert(AdmissionRenderPacketComplete(&packet, 41u));
  assert(worker.VisibleDestinationAllocationToken == 0x6000ULL);
  assert(worker.VisibleDestinationCpuToken == 0x5000ULL);
  assert(worker.VisibleDestinationGpuVa == 0x1500100000ULL);
  assert(worker.VisibleDestinationPhysical == 0x9d0100000ULL);
  assert(worker.VisibleDestinationBytes == 0xfa0000u);
  assert(worker.DestinationIndex == 1u);
}

static void test_prepatched_rejects_incomplete_or_cross_context_state(void) {
  ADMISSION_PREPATCHED_RENDER pending;
  ADMISSION_RENDER_PACKET_DESCRIPTION captured = packet_description(0u);
  ADMISSION_RENDER_PACKET_DESCRIPTION adopted;

  AdmissionPrepatchedInitialize(&pending);
  captured.VisibleDestinationPhysical = 0ULL;
  assert(!AdmissionPrepatchedCapture(&pending, &captured));
  assert(!AdmissionPrepatchedActive(&pending));
  captured = packet_description(0u);
  assert(AdmissionPrepatchedCapture(&pending, &captured));
  assert(!AdmissionPrepatchedAdopt(
      &pending, 42u, 0x9999ULL, captured.PrivateDataToken,
      captured.DmaStart, captured.DmaEnd, &adopted));
  assert(!AdmissionPrepatchedActive(&pending));
  assert(!AdmissionPrepatchedAdopt(
      &pending, 42u, captured.ContextToken, captured.PrivateDataToken,
      captured.DmaStart, captured.DmaEnd, &adopted));
}

static void test_prepare_rejects_missing_identity_and_bad_intervals(void) {
  ADMISSION_RENDER_PACKET packet;
  ADMISSION_RENDER_PACKET_DESCRIPTION description =
      packet_description(13u);

  AdmissionRenderPacketInitialize(&packet);
  description.ContextToken = 0ULL;
  assert(!AdmissionRenderPacketPrepare(&packet, &description));
  description = packet_description(13u);
  description.DestinationPhysical = 0ULL;
  assert(!AdmissionRenderPacketPrepare(&packet, &description));
  description = packet_description(13u);
  description.PrivateDataEnd = description.PrivateDataBytes + 1u;
  assert(!AdmissionRenderPacketPrepare(&packet, &description));
  description = packet_description(13u);
  description.PrivateDataStart = description.PrivateDataEnd;
  assert(!AdmissionRenderPacketPrepare(&packet, &description));
  description = packet_description(13u);
  description.DmaStart = description.DmaEnd;
  assert(!AdmissionRenderPacketPrepare(&packet, &description));
}

static void test_cancel_and_preemption_never_synthesize_completion(void) {
  ADMISSION_RENDER_PACKET packet;
  ADMISSION_RENDER_PACKET_DESCRIPTION description =
      packet_description(21u);

  AdmissionRenderPacketInitialize(&packet);
  assert(AdmissionRenderPacketPrepare(&packet, &description));
  assert(AdmissionRenderPacketCancelPrepared(&packet, 0x1000ULL));
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketEmpty);
  assert(AdmissionRenderPacketPrepare(&packet, &description));
  assert(AdmissionRenderPacketQueue(&packet, 21u, 0x1000ULL,
                                    0x3000ULL, 0u, 160u));
  assert(!AdmissionRenderPacketCancelPrepared(&packet, 0x1000ULL));
  assert(!AdmissionRenderPacketDiscardQueued(&packet, 20u));
  assert(AdmissionRenderPacketDiscardQueued(&packet, 21u));
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketEmpty);
}

static void test_active_reset_requires_backend_quiesce(void) {
  ADMISSION_RENDER_PACKET packet;
  ADMISSION_RENDER_PACKET_DESCRIPTION description =
      packet_description(31u);

  AdmissionRenderPacketInitialize(&packet);
  assert(AdmissionRenderPacketPrepare(&packet, &description));
  assert(AdmissionRenderPacketQueue(&packet, 31u, 0x1000ULL,
                                    0x3000ULL, 0u, 160u));
  assert(AdmissionRenderPacketActivate(&packet, 31u));
  assert(!AdmissionRenderPacketReset(&packet, 31u, 0u));
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketActive);
  assert(AdmissionRenderPacketReset(&packet, 31u, 1u));
  assert(AdmissionRenderPacketState(&packet) ==
         AdmissionRenderPacketEmpty);
}

static void test_nonpaging_private_range_uses_full_buffer_when_subrange_empty(void) {
  assert(AdmissionNonPagingPrivateRangeCovers(256u, 0u, 0u, 8192u));
  assert(AdmissionNonPagingPrivateRangeCovers(256u, 0u, 256u, 8192u));
  assert(!AdmissionNonPagingPrivateRangeCovers(256u, 1u, 256u, 8192u));
  assert(!AdmissionNonPagingPrivateRangeCovers(256u, 0u, 255u, 8192u));
  assert(!AdmissionNonPagingPrivateRangeCovers(256u, 0u, 8193u, 8192u));
}

static void test_gdi_receipt_requires_one_context_fence_and_physical_completion(void) {
  ADMISSION_GDI_HW_RECEIPT receipt;
  assert(sizeof(receipt) == 160u);
  AdmissionGdiReceiptInitialize(&receipt);
  assert(AdmissionGdiReceiptBegin(&receipt, 0x1000ULL, 1u, 0x00332211u, 1u, 96u));
  assert(!AdmissionGdiReceiptPatch(&receipt, 0x2000ULL, 7u,
      0x1500000000ULL, 0x9d0000000ULL, 0x10000u));
  assert(AdmissionGdiReceiptPatch(&receipt, 0x1000ULL, 7u,
      0x1500000000ULL, 0x9d0000000ULL, 0x10000u));
  assert(!AdmissionGdiReceiptSubmit(&receipt, 0x1000ULL, 8u, 0u));
  assert(AdmissionGdiReceiptSubmit(&receipt, 0x1000ULL, 7u, 0u));
  assert(AdmissionGdiReceiptBackend(&receipt, 7u, 0u,
      0u, 1u, 5u, 6u, 1u, 1u));
  assert(AdmissionGdiReceiptProgress(&receipt, 7u,
      0u, 4u, 0u, 0u, 0u, 6u, 0u, 0u, 1u));
  assert(receipt.Stage == AdmissionGdiReceiptStageBackend);
  assert(AdmissionGdiReceiptDpc(&receipt, 7u));
  assert(receipt.Stage == AdmissionGdiReceiptStageBackend);
  assert(!AdmissionGdiReceiptComplete(&receipt, 8u, 0u, 1u));
  assert(AdmissionGdiReceiptComplete(&receipt, 7u, 0u, 1u));
  assert(receipt.Stage == AdmissionGdiReceiptStageDpc);
  assert(AdmissionGdiReceiptProgress(&receipt, 7u,
      1u, 5u, 1u, 1u, 1u, 6u, 1u, 1u, 2u));
  assert(receipt.Stage == AdmissionGdiReceiptStageDpc);
  assert(receipt.NotifyInterrupt == 1u && receipt.NotifyDpc == 1u);
  assert(receipt.TaComplete == 1u && receipt.D3Complete == 1u);
  assert(receipt.Fence == 7u && receipt.CompletionFence == 7u);
}

static void test_terminal_receipt_preserves_preclear_completion(void) {
  ADMISSION_TERMINAL_RECEIPT receipt;
  unsigned char event[ADMISSION_TERMINAL_RAW_EVENT_BYTES];
  unsigned char output[0x4000];
  unsigned int index;
  for (index = 0u; index < sizeof(event); ++index)
    event[index] = (unsigned char)(0x80u + index);
  memset(output, 0xa5, sizeof(output));
  for (index = 0u; index < 256u; ++index)
    ((unsigned int *)output)[index] = 0xff112233u;
  AdmissionTerminalReceiptInitialize(&receipt);
  assert(!AdmissionTerminalReceiptBegin(
      &receipt, 1u, 9u, 0x9fff78000ULL, 255u, 0x1000ULL, 0x2000ULL,
      0xffffffa000400000ULL, 0x8f0000000ULL, 0x10000u,
      0u, 0u, 0x7a000100u, 0x3d000100u, 2u, 2u,
      0xffffffa000304004ULL, 0xffffffa000304004ULL,
      0xffffffa00030c008ULL, 0xffffffa00030c008ULL));
  assert(AdmissionTerminalReceiptBegin(
      &receipt, 1u, 9u, 0x9fff78000ULL, 255u, 0x1000ULL, 0x2000ULL,
      0xffffffa000400000ULL, 0x8f0000000ULL, 0x10000u,
      0u, 1u, 0x7a000100u, 0x3d000100u, 2u, 2u,
      0xffffffa000304004ULL, 0xffffffa000304004ULL,
      0xffffffa00030c008ULL, 0xffffffa00030c008ULL));
  assert(AdmissionTerminalReceiptObserve(
      &receipt, 255u, 0u, 0u, AdmissionTerminalSourcePollingEvent,
      event, sizeof(event), 1u, 0x7a000100u, 2u, 0x3d000100u, 2u));
  assert(AdmissionTerminalReceiptNotifyInterrupt(&receipt, 255u));
  assert(AdmissionTerminalReceiptNotifyDpc(&receipt, 255u));
  assert(AdmissionTerminalReceiptCaptureOutput(
      &receipt, 255u, output, 1024u, sizeof(output), 0xff112233u, 0xa5u));
  assert(AdmissionTerminalReceiptExit(
      &receipt, AdmissionTerminalExitCompleted, 3u, 1u, 0u, 0u, 0u));
  assert((receipt.ValidMask & ADMISSION_TERMINAL_VALID_ALL) ==
         ADMISSION_TERMINAL_VALID_ALL);
  assert(receipt.TaEvent == 0u && receipt.D3Event == 1u);
  assert(receipt.TaObservedStamp == 0x7a000100u &&
         receipt.D3ObservedStamp == 0x3d000100u);
  assert(receipt.TaObservedDone == 2u && receipt.D3ObservedDone == 2u);
  assert(receipt.CompletedFence == 255u && receipt.NotifyInterrupt == 1u &&
         receipt.NotifyDpc == 1u);
  assert(memcmp(receipt.RawEvent, event, sizeof(event)) == 0);
  assert(receipt.OutputFirstPixelActual == 0xff112233u);
  assert(receipt.OutputFirstMismatchIndex == 0xffffffffu);
  assert(receipt.OutputPixelsExpected == 256u);
  assert(receipt.OutputPixelsPoison == 0u);
  assert(receipt.OutputChangedBytes == 1024u);
  assert(receipt.OutputGuardCorrupt == 0u);
  assert(memcmp(receipt.OutputPrefix, output,
                ADMISSION_TERMINAL_OUTPUT_PREFIX_BYTES) == 0);
}

static void test_terminal_output_progress_bounds_reads_and_aborts_cleanly(void) {
  ADMISSION_TERMINAL_RECEIPT receipt;
  OUTPUT_PROGRESS_TEST progress = {0};
  unsigned char event[ADMISSION_TERMINAL_RAW_EVENT_BYTES] = {0};
  unsigned int output[4] = {
      0xff112233u, 0xff112233u, 0xff112233u, 0xff112233u};
  AdmissionTerminalReceiptInitialize(&receipt);
  assert(AdmissionTerminalReceiptBegin(
      &receipt, 1u, 9u, 0x9fff78000ULL, 17u, 0x1000ULL, 0x2000ULL,
      0x1500000000ULL, 0x9bc000000ULL, sizeof(output),
      0u, 1u, 0x7a000100u, 0x3d000100u, 2u, 2u,
      0x3000ULL, 0x3000ULL, 0x4000ULL, 0x4000ULL));
  assert(AdmissionTerminalReceiptObserve(
      &receipt, 17u, 0u, 0u, AdmissionTerminalSourcePollingEvent,
      event, sizeof(event), 1u, 0x7a000100u, 2u, 0x3d000100u, 2u));
  assert(AdmissionTerminalReceiptCaptureOutputProgress(
      &receipt, 17u, (const unsigned char *)output, sizeof(output),
      sizeof(output), 0xff112233u, 0xa5u, 8u,
      output_progress, &progress));
  /* One boundary in the pixel pass and one in the byte-hash pass. */
  assert(progress.Calls == 2u);
  assert(receipt.OutputPixelsExpected == 4u);
  assert(receipt.OutputBytesExamined == sizeof(output));
  assert(receipt.OutputTargetFnv1a == 0x38093307db8f7f0dULL);

  AdmissionTerminalReceiptInitialize(&receipt);
  assert(AdmissionTerminalReceiptBegin(
      &receipt, 1u, 9u, 0x9fff78000ULL, 18u, 0x1000ULL, 0x2000ULL,
      0x1500000000ULL, 0x9bc000000ULL, sizeof(output),
      0u, 1u, 0x7a000100u, 0x3d000100u, 2u, 2u,
      0x3000ULL, 0x3000ULL, 0x4000ULL, 0x4000ULL));
  assert(AdmissionTerminalReceiptObserve(
      &receipt, 18u, 0u, 0u, AdmissionTerminalSourcePollingEvent,
      event, sizeof(event), 1u, 0x7a000100u, 2u, 0x3d000100u, 2u));
  progress.Calls = 0u;
  progress.FailAt = 1u;
  assert(!AdmissionTerminalReceiptCaptureOutputProgress(
      &receipt, 18u, (const unsigned char *)output, sizeof(output),
      sizeof(output), 0xff112233u, 0xa5u, 8u,
      output_progress, &progress));
  assert(progress.Calls == 1u);
  assert(!(receipt.ValidMask & ADMISSION_TERMINAL_VALID_OUTPUT));
  assert(receipt.OutputPixelsExpected == 0u);
}

int main(void) {
  test_exact_packet_moves_prepared_queued_active_completed();
  test_prepatched_capture_adopt_and_worker_copy_are_exact();
  test_prepatched_rejects_incomplete_or_cross_context_state();
  test_prepare_rejects_missing_identity_and_bad_intervals();
  test_cancel_and_preemption_never_synthesize_completion();
  test_active_reset_requires_backend_quiesce();
  test_nonpaging_private_range_uses_full_buffer_when_subrange_empty();
  test_gdi_receipt_requires_one_context_fence_and_physical_completion();
  test_terminal_receipt_preserves_preclear_completion();
  test_terminal_output_progress_bounds_reads_and_aborts_cleanly();
  return 0;
}
