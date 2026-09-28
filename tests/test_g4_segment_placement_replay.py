"""Run the actual KMD allocation body for G4 and legacy CPU allocations."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/allocation_windows.c"


def allocation_body():
    source = SOURCE.read_text()
    start = source.index("static NTSTATUS AdmissionCreateAllocationImpl(")
    opening = source.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    return source[start:position]


SHIM = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ADMISSION_GPUVA_G1B_PAGE_PROFILE 0
#define ADMISSION_GPUVA_G1B_ALLOCATION_HINT 0
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#define ADMISSION_LOCAL_SEGMENT_SET 2u
#define ADMISSION_CPU_VISIBLE_SEGMENT_SET 3u
#define ADMISSION_APERTURE_SEGMENT_SET 1u
#define ADMISSION_MEMORY_APERTURE_SEGMENT 1u
#define ADMISSION_ALLOCATION_ALIGNMENT 0x10000u
#define ADMISSION_POOL_TAG 0u
#define POOL_FLAG_NON_PAGED 0u
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0u
#define MAXSIZE_T SIZE_MAX
typedef void *HANDLE;
typedef uint64_t ULONGLONG;
typedef size_t SIZE_T;
typedef unsigned UINT;
typedef unsigned APPLE_AGX_U32;
typedef long NTSTATUS;
enum { STATUS_SUCCESS=0, STATUS_INVALID_PARAMETER=-1,
       STATUS_NOT_SUPPORTED=-2, STATUS_INSUFFICIENT_RESOURCES=-3 };
typedef struct { uint64_t Size; unsigned CpuVisible, Reserved; } ADMISSION_ALLOCATION_DESCRIPTION;
typedef struct { ADMISSION_ALLOCATION_DESCRIPTION Description; } ALLOCATION_OBJECT;
typedef struct { ALLOCATION_OBJECT Object; unsigned Win32ClassId, Win32Flags; }
    ADMISSION_ALLOCATION_HANDLE;
typedef struct { int Memory; void *PhysicalDeviceObject; } ADMISSION_CONTEXT;
typedef struct { unsigned Value, SegmentId0; } SEGMENT_HINT;
typedef struct { unsigned Value, CpuVisible, AccessedPhysically; } WDDM_FLAGS;
typedef struct {
  void *pPrivateDriverData;
  unsigned PrivateDriverDataSize;
  union { unsigned Alignment; struct { uint16_t MinimumPageSize,
                                     RecommendedPageSize; }; };
  size_t Size, PitchAlignedSize;
  SEGMENT_HINT HintedBank, PreferredSegment;
  union { unsigned SupportedReadSegmentSet, MmuSet; };
  unsigned SupportedWriteSegmentSet;
  unsigned EvictionSegmentSet;
  void *hAllocation;
  WDDM_FLAGS FlagsWddm2;
  void *pAllocationUsageHint;
  unsigned AllocationPriority;
  struct { unsigned Value; } Flags2;
  unsigned PhysicalAdapterIndex;
} DXGK_ALLOCATIONINFO;
typedef struct {
  unsigned NumAllocations;
  DXGK_ALLOCATIONINFO *pAllocationInfo;
  unsigned PrivateDriverDataSize;
  void *pPrivateDriverData, *hResource;
} DXGKARG_CREATEALLOCATION;
typedef enum { AdmissionWin32TransportSuccess=0 } ADMISSION_WIN32_TRANSPORT_RESULT;
typedef struct {
  unsigned ClassId, CpuVisible;
  uint64_t Size;
} INPUT;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
typedef struct { unsigned Token, ReadMode, AccessedPhysically,
                 PageMode, CloneClass0; } ADMISSION_R105_OVERRIDE;
typedef struct { unsigned Magic, Version, Bytes, ClassId, Flags;
                 unsigned Reserved[2];
                 ADMISSION_ALLOCATION_DESCRIPTION Allocation;
} ADMISSION_WIN32_ALLOCATION_CREATE;
static void AdmissionR105Apply(DXGK_ALLOCATIONINFO *info,
                               const ADMISSION_R105_OVERRIDE *override) {
  (void)info; (void)override;
}
static void AdmissionR105RecordEcho(void *device,
                                    const ADMISSION_R105_OVERRIDE *override,
                                    unsigned class_id, unsigned bits,
                                    const DXGK_ALLOCATIONINFO *info) {
  (void)device; (void)override; (void)class_id; (void)bits; (void)info;
}
static int AdmissionR105ParsePrivate(const void *data, unsigned bytes,
                                     ADMISSION_R105_OVERRIDE *override,
                                     ADMISSION_ALLOCATION_DESCRIPTION *desc,
                                     unsigned *class_id, unsigned *flags,
                                     ADMISSION_WIN32_TRANSPORT_RESULT *result) {
  (void)data; (void)bytes; (void)override; (void)desc; (void)class_id;
  (void)flags; (void)result; return 0;
}
static int AdmissionMemoryReady(int *memory) { return *memory != 0; }
static ADMISSION_WIN32_TRANSPORT_RESULT AdmissionWin32AllocationCreateValidate(
    const void *data, unsigned bytes, ADMISSION_ALLOCATION_DESCRIPTION *desc,
    unsigned *class_id, unsigned *flags) {
  const INPUT *input = data;
  assert(bytes == sizeof(*input));
  desc->Size = input->Size;
  desc->CpuVisible = input->CpuVisible;
  desc->Reserved = 0;
  *class_id = input->ClassId;
  *flags = 0;
  return AdmissionWin32TransportSuccess;
}
static int AdmissionAllocationAlign64K(uint64_t size, uint64_t *aligned) {
  *aligned = (size + 0xffffu) & ~0xffffULL;
  return size && *aligned;
}
static uint64_t AdmissionAllocationPitchAlignedSize(uint64_t size,
                                                    unsigned supported) {
  (void)size;
  assert(!supported);
  return 0;
}
#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
static void AdmissionRecordG1bAllocationInput(void *device,
                                               unsigned minimum,
                                               unsigned recommended) {
  (void)device; (void)minimum; (void)recommended;
}
#endif
static void *ExAllocatePool2(unsigned flags, size_t bytes, unsigned tag) {
  (void)flags; (void)tag;
  return malloc(bytes);
}
static void ExFreePoolWithTag(void *data, unsigned tag) {
  (void)tag; free(data);
}
static void RtlZeroMemory(void *data, size_t bytes) { memset(data, 0, bytes); }
static int AdmissionAllocationCreate(const ADMISSION_ALLOCATION_DESCRIPTION *desc,
                                     ALLOCATION_OBJECT *object) {
  object->Description = *desc;
  return 1;
}
"""


MAIN = r"""
int main(void) {
  ADMISSION_CONTEXT context = {1, NULL};
  INPUT native = {1, 1, 0x10000};
  DXGK_ALLOCATIONINFO info = {0};
  DXGKARG_CREATEALLOCATION args = {0};
  args.NumAllocations = 1;
  args.pAllocationInfo = &info;
  info.pPrivateDriverData = &native;
  info.PrivateDriverDataSize = sizeof(native);
  assert(AdmissionCreateAllocationImpl(&context, &args) == STATUS_SUCCESS);
  assert(info.PreferredSegment.SegmentId0 == 2);
  assert(info.Size == 0x10000 && info.Alignment == 0x10000);
  assert(info.FlagsWddm2.CpuVisible == 1);
  /* EXP836: a local-only write set is refused by dxgkrnl; the admitted
   * shape for Mesa class BOs is the class0 shape. */
  assert(info.SupportedReadSegmentSet == ADMISSION_CPU_VISIBLE_SEGMENT_SET);
  assert(info.SupportedWriteSegmentSet == ADMISSION_CPU_VISIBLE_SEGMENT_SET);
  assert(info.FlagsWddm2.AccessedPhysically == 1u);
  ExFreePoolWithTag(info.hAllocation, ADMISSION_POOL_TAG);
  native.Size = 0x4000;
  memset(&info, 0, sizeof(info));
  info.pPrivateDriverData = &native;
  info.PrivateDriverDataSize = sizeof(native);
  assert(AdmissionCreateAllocationImpl(&context, &args) == STATUS_INVALID_PARAMETER);
  assert(info.hAllocation == NULL);
  /* R145 decoder v2 supplies CpuVisible0. Check the real KMD output for
   * every native class: CPU staging is separate, local execution is mandatory. */
  for (unsigned cls = 1; cls <= 3; ++cls) {
    native.ClassId = cls;
    native.CpuVisible = 0;
    native.Size = (1u + 3u * cls) * 0x10000u;
    memset(&info, 0, sizeof(info));
    info.pPrivateDriverData = &native;
    info.PrivateDriverDataSize = sizeof(native);
    assert(AdmissionCreateAllocationImpl(&context, &args) == STATUS_SUCCESS);
    assert(info.FlagsWddm2.CpuVisible == 0);
    assert(info.FlagsWddm2.AccessedPhysically == 1);
    assert(info.SupportedWriteSegmentSet == ADMISSION_LOCAL_SEGMENT_SET);
    assert(info.PreferredSegment.SegmentId0 == ADMISSION_MEMORY_LOCAL_SEGMENT);
    assert(info.EvictionSegmentSet == 0);
    ExFreePoolWithTag(info.hAllocation, ADMISSION_POOL_TAG);
  }
  INPUT legacy = {0, 1, 0x10000};
  memset(&info, 0, sizeof(info));
  info.pPrivateDriverData = &legacy;
  info.PrivateDriverDataSize = sizeof(legacy);
  assert(AdmissionCreateAllocationImpl(&context, &args) == STATUS_SUCCESS);
  /* EXP839: section-backed CPU-visible class0 surfaces stay in the aperture. */
  assert(info.SupportedReadSegmentSet == ADMISSION_APERTURE_SEGMENT_SET);
  assert(info.SupportedWriteSegmentSet == ADMISSION_APERTURE_SEGMENT_SET);
  assert(info.PreferredSegment.SegmentId0 == ADMISSION_MEMORY_APERTURE_SEGMENT);
  ExFreePoolWithTag(info.hAllocation, ADMISSION_POOL_TAG);
  return 0;
}
"""


class G4SegmentPlacementReplay(unittest.TestCase):
    def test_gpu_readable_g4_class_stays_in_local_segment(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "allocation.c"
            binary = Path(directory) / "allocation"
            source.write_text(SHIM + allocation_body() + MAIN)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11",
                            "-Wall", "-Wextra", "-Werror", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_g1b_16kb_profile_keeps_the_exp836_admitted_shape(self):
        """EXP836: page-size hints were not what dxgkrnl required; the admitted
        Mesa class shape is identical in the 16-KiB G1b profile."""
        shim = SHIM.replace("#define ADMISSION_GPUVA_G1B_PAGE_PROFILE 0",
                            "#define ADMISSION_GPUVA_G1B_PAGE_PROFILE 16")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "allocation.c"
            binary = Path(directory) / "allocation"
            source.write_text(shim + allocation_body() + MAIN)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11",
                            "-Wall", "-Wextra", "-Werror", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
