#include "render_paging.h"

#include <assert.h>
#include <string.h>

static void test_fence_order_and_resubmission(void) {
  assert(AdmissionPagingFenceCanSubmit(0u, 1u, 0u));
  assert(AdmissionPagingFenceCanSubmit(1u, 2u, 0u));
  assert(!AdmissionPagingFenceCanSubmit(2u, 2u, 0u));
  assert(!AdmissionPagingFenceCanSubmit(2u, 1u, 0u));
  assert(AdmissionPagingFenceCanSubmit(0xffffffffu, 1u, 0u));
  assert(AdmissionPagingFenceCanSubmit(7u, 7u, 1u));
  assert(!AdmissionPagingFenceCanSubmit(7u, 8u, 1u));
  assert(!AdmissionPagingFenceCanSubmit(0u, 1u, 1u));
}

static void test_record_set_validation(void) {
  ADMISSION_PAGING_RECORD records[2];
  unsigned int index;
  memset(records, 0, sizeof(records));
  for (index = 0u; index < 2u; ++index) {
    records[index].Header.Magic = ADMISSION_PAGING_MAGIC;
    records[index].Header.Version = ADMISSION_PAGING_VERSION;
    records[index].Header.RecordBytes = sizeof(records[index]);
    records[index].Plan.Kind = AppleAgxPhysicalPagingFill;
  }
  assert(AdmissionPagingRecordsValid(
      records, 2u, 64u, 2u * sizeof(ADMISSION_PAGING_MARKER)));
  records[1].Header.Reserved = 1u;
  assert(!AdmissionPagingRecordsValid(
      records, 2u, 64u, 2u * sizeof(ADMISSION_PAGING_MARKER)));
  records[1].Header.Reserved = 0u;
  records[1].Plan.Kind = (APPLE_AGX_PHYSICAL_PAGING_KIND)0;
  assert(!AdmissionPagingRecordsValid(
      records, 2u, 64u, 2u * sizeof(ADMISSION_PAGING_MARKER)));
  assert(!AdmissionPagingRecordsValid(
      records, 2u, 1u, 2u * sizeof(ADMISSION_PAGING_MARKER)));
}

/* M2-f2: segment 3 (the KMD-populated pool) is a memory segment like the
 * local one: its virtual records are valid and may span a contiguous run;
 * segment 4 does not exist. */
static void test_pool_segment_runs(void) {
  ADMISSION_PAGING_RECORD r;
  memset(&r, 0, sizeof(r));
  r.Header.Magic = ADMISSION_PAGING_MAGIC;
  r.Header.Version = ADMISSION_PAGING_VERSION;
  r.Header.RecordBytes = sizeof(r);
  r.Kind = AdmissionPagingVirtualTransfer;
  r.SourceSegment = ADMISSION_PAGING_POOL_SEGMENT;
  r.DestinationSegment = ADMISSION_PAGING_POOL_SEGMENT;
  r.SourceIpa = 0x980000000ULL;
  r.DestinationIpa = 0x980100000ULL;
  r.Bytes = 0x10000u;
  assert(AdmissionPagingLocalRun(&r));
  assert(AdmissionPagingRecordsValid(&r, 1u, 64u, sizeof(ADMISSION_PAGING_MARKER)));
  /* Pool <-> local runs (both memory segments). */
  r.SourceSegment = ADMISSION_PAGING_LOCAL_SEGMENT;
  assert(AdmissionPagingLocalRun(&r));
  /* Pool <-> system memory stays page-sized. */
  r.SourceSegment = 0u;
  assert(!AdmissionPagingLocalRun(&r));
  assert(!AdmissionPagingRecordsValid(&r, 1u, 64u, sizeof(ADMISSION_PAGING_MARKER)));
  r.Bytes = 0x1000u;
  assert(AdmissionPagingRecordsValid(&r, 1u, 64u, sizeof(ADMISSION_PAGING_MARKER)));
  r.DestinationSegment = 4u;
  assert(!AdmissionPagingRecordsValid(&r, 1u, 64u, sizeof(ADMISSION_PAGING_MARKER)));
  r.Kind = AdmissionPagingVirtualFill;
  r.DestinationSegment = ADMISSION_PAGING_POOL_SEGMENT;
  r.Bytes = 0x40000u;
  assert(AdmissionPagingLocalRun(&r));
  assert(AdmissionPagingRecordsValid(&r, 1u, 64u, sizeof(ADMISSION_PAGING_MARKER)));
}

int main(void) {
  test_fence_order_and_resubmission();
  test_record_set_validation();
  test_pool_segment_runs();
  return 0;
}
