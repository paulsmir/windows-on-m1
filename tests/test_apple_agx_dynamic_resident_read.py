"""The dynamic materializer reads either supported resident WDDM segment."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "drivers/apple-agx/render-admission/src/memory_runtime_windows.c"
SHARED = ROOT / "drivers/apple-agx/shared"


class DynamicResidentReadTests(unittest.TestCase):
    def test_local_and_aperture_reads_share_bounds_lock_and_short_copy_contract(self):
        source = RUNTIME.read_text()
        start = source.index(
            "_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeReadResident"
        )
        end = source.index(
            "_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeBorrowIo", start
        )
        function = source[start:end]
        shim = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "apple_agx_software_aperture.h"
#define _Use_decl_annotations_
#define PAGE_SIZE 4096u
#define PASSIVE_LEVEL 0u
#define ADMISSION_MEMORY_APERTURE_SEGMENT 1u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_INVALID_ADDRESS (-2)
#define STATUS_PARTIAL_COPY (-3)
#define STATUS_INSUFFICIENT_RESOURCES (-4)
#define STATUS_INVALID_DEVICE_STATE (-5)
#define NT_SUCCESS(x) ((x) >= 0)
#define MM_COPY_MEMORY_PHYSICAL 1u
#define RtlCopyMemory memcpy
#define KeMemoryBarrier() ((void)0)
typedef unsigned char KIRQL;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef size_t SIZE_T;
typedef int NTSTATUS;
typedef void *PVOID;
typedef unsigned char *PUCHAR;
typedef int FAST_MUTEX;
typedef struct {void *CpuAddress; ULONGLONG Bytes;} ADMISSION_LOCAL_MEMORY_VIEW;
typedef struct {
  struct {
    struct {ULONGLONG Base, Size;} Aperture;
  } Topology;
  APPLE_AGX_SOFTWARE_APERTURE Aperture;
} ADMISSION_MEMORY_CONTRACT;
typedef struct {ADMISSION_MEMORY_CONTRACT Memory;} ADMISSION_CONTEXT;
typedef struct {FAST_MUTEX PagingLock;} ADMISSION_MEMORY_RUNTIME;
typedef union {struct {LONGLONG QuadPart;} PhysicalAddress;} MM_COPY_ADDRESS;
static ADMISSION_MEMORY_RUNTIME runtime;
static unsigned char local_bytes[8192];
static unsigned char pages[2][4096];
static unsigned lock_count, unlock_count, copy_count, short_copy;
static ADMISSION_MEMORY_RUNTIME *AdmissionMemoryGetRuntime(ADMISSION_CONTEXT *context) {
  return context == NULL ? NULL : &runtime;
}
static KIRQL KeGetCurrentIrql(void) { return PASSIVE_LEVEL; }
static void ExAcquireFastMutex(FAST_MUTEX *lock) {(void)lock; ++lock_count;}
static void ExReleaseFastMutex(FAST_MUTEX *lock) {(void)lock; ++unlock_count;}
static NTSTATUS AdmissionMemoryRuntimeResolveLocal(
    ADMISSION_CONTEXT *context, ULONGLONG address, ULONGLONG size,
    ULONGLONG offset, ADMISSION_LOCAL_MEMORY_VIEW *view) {
  (void)context;
  if (address != 0x20000ULL || size > sizeof(local_bytes) ||
      offset > size) return STATUS_INVALID_ADDRESS;
  view->CpuAddress = local_bytes + offset;
  view->Bytes = size - offset;
  return STATUS_SUCCESS;
}
static NTSTATUS MmCopyMemory(void *destination, MM_COPY_ADDRESS source,
                            SIZE_T bytes, unsigned flags, SIZE_T *copied) {
  ULONGLONG physical = (ULONGLONG)source.PhysicalAddress.QuadPart;
  unsigned page;
  SIZE_T offset;
  assert(flags == MM_COPY_MEMORY_PHYSICAL);
  if (physical >= 0x210000ULL && physical < 0x211000ULL) {
    page = 0u; offset = (SIZE_T)(physical - 0x210000ULL);
  } else {
    assert(physical >= 0x930000ULL && physical < 0x931000ULL);
    page = 1u; offset = (SIZE_T)(physical - 0x930000ULL);
  }
  ++copy_count;
  *copied = short_copy && copy_count == 2u ? bytes - 1u : bytes;
  memcpy(destination, pages[page] + offset, *copied);
  return STATUS_SUCCESS;
}
'''
        cases = r'''
int main(void) {
  ADMISSION_CONTEXT context = {0};
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY entries[2];
  APPLE_AGX_U64 physical[2] = {0x210000ULL, 0x930000ULL};
  unsigned char output[16];
  context.Memory.Topology.Aperture.Base = 0x1600000000ULL;
  context.Memory.Topology.Aperture.Size = 0x2000ULL;
  assert(AppleAgxSoftwareApertureInitialize(
      &context.Memory.Aperture, entries, 2u) == AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureMap(
      &context.Memory.Aperture, 0u, physical, 2u) ==
      AppleAgxSoftwareApertureOk);
  memset(local_bytes, 0x5a, sizeof(local_bytes));
  assert(AdmissionMemoryRuntimeReadResident(
      &context, 2u, 0x20000ULL, sizeof(local_bytes), 7u,
      output, 9u) == STATUS_SUCCESS);
  for (unsigned i = 0u; i < 9u; ++i) assert(output[i] == 0x5a);
  pages[0][4093] = 0xab; pages[0][4094] = 0xbc; pages[0][4095] = 0xcd;
  for (unsigned i = 0u; i < 6u; ++i) pages[1][i] = (unsigned char)(i + 1u);
  memset(output, 0, sizeof(output));
  assert(AdmissionMemoryRuntimeReadResident(
      &context, 1u, 0x1600000ff0ULL, 0x1010ULL, 13u,
      output, 9u) == STATUS_SUCCESS);
  const unsigned char expected[9] = {0xab,0xbc,0xcd,1,2,3,4,5,6};
  assert(memcmp(output, expected, sizeof(expected)) == 0);
  assert(lock_count == 2u && unlock_count == 2u && copy_count == 2u);
  assert(AdmissionMemoryRuntimeReadResident(
      &context, 0u, 0x20000ULL, 0x100u, 0u, output, 4u) ==
      STATUS_INVALID_PARAMETER);
  assert(AdmissionMemoryRuntimeReadResident(
      &context, 1u, 0x15ffffffffULL, 0x100u, 0u, output, 4u) ==
      STATUS_INVALID_ADDRESS);
  assert(AdmissionMemoryRuntimeReadResident(
      &context, 1u, 0x1600000000ULL, 0x2001u, 0u, output, 4u) ==
      STATUS_INVALID_ADDRESS);
  assert(AdmissionMemoryRuntimeReadResident(
      &context, 2u, 0x20000ULL, 8u, 7u, output, 2u) ==
      STATUS_INVALID_ADDRESS);
  copy_count = 0u; short_copy = 1u;
  assert(AdmissionMemoryRuntimeReadResident(
      &context, 1u, 0x1600000ff0ULL, 0x1010ULL, 13u,
      output, 9u) == STATUS_PARTIAL_COPY);
  assert(lock_count == 3u && unlock_count == 3u);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / "resident.c"
            binary = Path(tmp) / "resident"
            program.write_text(shim + function + cases)
            subprocess.run(
                [
                    os.environ.get("CC", "clang"),
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-I",
                    str(SHARED / "include"),
                    str(program),
                    str(SHARED / "src/apple_agx_software_aperture.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
