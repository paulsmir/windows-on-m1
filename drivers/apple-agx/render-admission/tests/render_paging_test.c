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

int main(void) {
  test_fence_order_and_resubmission();
  test_record_set_validation();
  return 0;
}
