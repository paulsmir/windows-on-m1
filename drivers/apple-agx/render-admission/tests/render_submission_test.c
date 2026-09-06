#include "render_submission.h"
#include "render_gdi_receipt.h"

#include <assert.h>

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
  description.DestinationCpuToken = 0x4000ULL;
  description.DestinationGpuVa = 0x1500010000ULL;
  description.DestinationPhysical = 0x9d0010000ULL;
  description.DestinationBytes = 0x10000u;
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
      3u, 4u, 5u, 6u, 1u, 1u));
  assert(!AdmissionGdiReceiptComplete(&receipt, 8u, 0u, 1u));
  assert(AdmissionGdiReceiptComplete(&receipt, 7u, 0u, 1u));
  assert(AdmissionGdiReceiptDpc(&receipt, 7u));
  assert(AdmissionGdiReceiptProgress(&receipt, 7u,
      1u, 5u, 1u, 1u, 1u, 6u, 1u, 1u, 2u));
  assert(receipt.Stage == AdmissionGdiReceiptStageDpc);
  assert(receipt.NotifyInterrupt == 1u && receipt.NotifyDpc == 1u);
  assert(receipt.TaComplete == 1u && receipt.D3Complete == 1u);
  assert(receipt.Fence == 7u && receipt.CompletionFence == 7u);
}

int main(void) {
  test_exact_packet_moves_prepared_queued_active_completed();
  test_prepare_rejects_missing_identity_and_bad_intervals();
  test_cancel_and_preemption_never_synthesize_completion();
  test_active_reset_requires_backend_quiesce();
  test_nonpaging_private_range_uses_full_buffer_when_subrange_empty();
  test_gdi_receipt_requires_one_context_fence_and_physical_completion();
  return 0;
}
