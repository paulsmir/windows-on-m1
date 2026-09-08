#include "render_qualification.h"

#include <assert.h>
#include <string.h>

static ADMISSION_PRESENT_VERIFICATION make_frame(
    unsigned int index, unsigned int fence, unsigned long long allocation,
    unsigned int color, unsigned long long sequence,
    unsigned long long offset, unsigned long long physical,
    unsigned long long hash) {
  ADMISSION_PRESENT_VERIFICATION frame;
  memset(&frame, 0, sizeof(frame));
  frame.CandidateBuild = 631u;
  frame.BootGeneration = 0x12345678u;
  frame.Index = index;
  frame.Purpose = AdmissionPresentPurposeRenderFrame;
  frame.Fence = fence;
  frame.DestinationIndex = index;
  frame.AllocationToken = allocation;
  frame.ExpectedColor = color;
  frame.PixelsExpected = 4096000u;
  frame.PixelsVerified = 4096000u;
  frame.Format = 21u;
  frame.Width = 2560u;
  frame.Height = 1600u;
  frame.Pitch = 10240u;
  frame.ContentHash = hash;
  frame.Sequence = sequence;
  frame.ActiveOffset = offset;
  frame.PhysicalAddress = physical;
  return frame;
}

int main(void) {
  ADMISSION_PRESENT_VERIFICATION first = make_frame(
      0u, 256u, 0xffff800012340000ULL, 0xff112233u, 9u,
      0x00fa0000ULL, 0x9bd000000ULL, 0x1122334455667788ULL);
  ADMISSION_PRESENT_VERIFICATION second = make_frame(
      1u, 257u, 0xffff800012350000ULL, 0xffcc8844u, 10u,
      0x01f40000ULL, 0x9bdfa0000ULL, 0x8877665544332211ULL);
  ADMISSION_PRESENT_EXPECTATION expected;
  ADMISSION_PRESENT_QUERY record;
  ADMISSION_PRESENT_PRODUCER_STATE producer;
  ADMISSION_RETIREMENT_QUERY retirement;
  ADMISSION_RETIREMENT_EXPECTATION retirementExpected;

  memset(&record, 0, sizeof(record));
  assert(AdmissionPresentQueryBuild(&record, &first));
  assert(record.Magic == ADMISSION_PRESENT_QUERY_MAGIC);
  assert(record.Version == ADMISSION_PRESENT_QUERY_VERSION);
  assert(record.CandidateBuild == 631u);
  assert(record.BootGeneration == 0x12345678u);
  assert(record.Index == 0u && record.Purpose == AdmissionPresentPurposeRenderFrame);
  assert(record.Fence == 256u && record.DestinationIndex == 0u);
  assert(record.AllocationToken == 0xffff800012340000ULL);
  assert(record.ExpectedColor == 0xff112233u);
  assert(record.PixelsExpected == 4096000u);
  assert(record.PixelsVerified == 4096000u);
  assert(record.Format == 21u && record.Width == 2560u);
  assert(record.Height == 1600u && record.Pitch == 10240u);
  assert(record.ContentHash == 0x1122334455667788ULL);
  assert(record.Sequence == 9u && record.ActiveOffset == 0x00fa0000ULL);
  assert(record.PhysicalAddress == 0x9bd000000ULL);
  assert(record.Captured == 1u && record.PublishedToQuery == 0u);
  assert(record.Exported == 0u && record.Durable == 0u);

  memset(&expected, 0, sizeof(expected));
  expected.CandidateBuild = 631u;
  expected.Index = 0u;
  expected.DestinationIndex = 0u;
  expected.ExpectedColor = 0xff112233u;
  expected.PixelsExpected = 4096000u;
  expected.Format = 21u;
  expected.Width = 2560u;
  expected.Height = 1600u;
  expected.Pitch = 10240u;
  record.PublishedToQuery = 1u;
  assert(AdmissionPresentQueryAccept(&record, &expected));

  record.PixelsVerified = 0u;
  assert(!AdmissionPresentQueryAccept(&record, &expected));
  record.PixelsVerified = 4096000u;
  record.ExpectedColor = 0u;
  assert(!AdmissionPresentQueryAccept(&record, &expected));
  record.ExpectedColor = 0xff112233u;
  record.Purpose = AdmissionPresentPurposeFallback;
  assert(!AdmissionPresentQueryAccept(&record, &expected));
  record.Purpose = AdmissionPresentPurposeRenderFrame;

  assert(AdmissionPresentQueryBuild(&record, &second));
  record.PublishedToQuery = 1u;
  expected.BootGeneration = 0x12345678u;
  expected.Index = 1u;
  expected.DestinationIndex = 1u;
  expected.ExpectedColor = 0xffcc8844u;
  expected.PreviousFence = 256u;
  expected.PreviousSequence = 9u;
  expected.PreviousAllocationToken = 0xffff800012340000ULL;
  expected.PreviousActiveOffset = 0x00fa0000ULL;
  expected.PreviousPhysicalAddress = 0x9bd000000ULL;
  expected.PreviousContentHash = 0x1122334455667788ULL;
  assert(AdmissionPresentQueryAccept(&record, &expected));
  record.Fence = 256u;
  assert(!AdmissionPresentQueryAccept(&record, &expected));
  record.Fence = 257u;
  record.Sequence = 9u;
  assert(!AdmissionPresentQueryAccept(&record, &expected));
  record.Sequence = 10u;
  record.AllocationToken = expected.PreviousAllocationToken;
  assert(!AdmissionPresentQueryAccept(&record, &expected));
  record.AllocationToken = second.AllocationToken;
  record.ActiveOffset = expected.PreviousActiveOffset;
  assert(!AdmissionPresentQueryAccept(&record, &expected));

  first.PixelsVerified = 4095999u;
  assert(!AdmissionPresentQueryBuild(&record, &first));
  first.PixelsVerified = 4096000u;
  first.ContentHash = 0ULL;
  assert(!AdmissionPresentQueryBuild(&record, &first));
  assert(AdmissionPresentWaitClassify(1, 1, 0, 0) ==
         AdmissionPresentWaitCompleted);
  assert(AdmissionPresentWaitClassify(0, 0, 0, 0) ==
         AdmissionPresentWaitQueryFailed);
  assert(AdmissionPresentWaitClassify(1, 0, 0, 1) ==
         AdmissionPresentWaitTimedOut);
  assert(AdmissionPresentWaitClassify(1, 0, 1, 1) ==
         AdmissionPresentWaitInvalidRecord);

  AdmissionPresentProducerInitialize(&producer, 1);
  assert(AdmissionPresentProducerAfterWait(
             &producer, AdmissionPresentWaitTimedOut) ==
         AdmissionPresentProducerPreserveForRecovery);
  assert(producer.CompletedFrames == 0u && producer.CleanupAllowed == 0u);
  assert(AdmissionPresentProducerAfterWait(
             &producer, AdmissionPresentWaitCompleted) ==
         AdmissionPresentProducerPreserveForRecovery);

  AdmissionPresentProducerInitialize(&producer, 1);
  assert(AdmissionPresentProducerAfterWait(
             &producer, AdmissionPresentWaitCompleted) ==
         AdmissionPresentProducerSubmitNextFrame);
  assert(producer.CompletedFrames == 1u && producer.CleanupAllowed == 0u);
  assert(AdmissionPresentProducerAfterWait(
             &producer, AdmissionPresentWaitInvalidRecord) ==
         AdmissionPresentProducerPreserveForRecovery);
  assert(producer.CompletedFrames == 1u && producer.CleanupAllowed == 0u);

  AdmissionPresentProducerInitialize(&producer, 1);
  assert(AdmissionPresentProducerAfterWait(
             &producer, AdmissionPresentWaitCompleted) ==
         AdmissionPresentProducerSubmitNextFrame);
  assert(AdmissionPresentProducerAfterWait(
             &producer, AdmissionPresentWaitCompleted) ==
         AdmissionPresentProducerBeginHold);
  assert(producer.CompletedFrames == 2u && producer.CleanupAllowed == 0u);
  assert(AdmissionPresentProducerRetirementComplete(&producer));
  assert(producer.CleanupAllowed == 1u);

  memset(&retirement, 0, sizeof(retirement));
  assert(AdmissionRetirementQueryBuild(
      &retirement, 632u, 0x12345678u, 5u,
      0xffff800099990000ULL, 0ULL, 0x9bbff0000ULL));
  memset(&retirementExpected, 0, sizeof(retirementExpected));
  retirementExpected.CandidateBuild = 632u;
  retirementExpected.BootGeneration = 0x12345678u;
  retirementExpected.PreviousSequence = 4u;
  retirementExpected.ExpectedPoolPhysical = 0x9bbff0000ULL;
  retirementExpected.RenderAllocation0 = first.AllocationToken;
  retirementExpected.RenderAllocation1 = second.AllocationToken;
  assert(AdmissionRetirementQueryAccept(&retirement, &retirementExpected));
  retirement.ActiveOffset = 0xfa0000ULL;
  assert(!AdmissionRetirementQueryAccept(&retirement, &retirementExpected));
  retirement.ActiveOffset = 0ULL;
  retirement.AllocationToken = first.AllocationToken;
  assert(!AdmissionRetirementQueryAccept(&retirement, &retirementExpected));
  return 0;
}
