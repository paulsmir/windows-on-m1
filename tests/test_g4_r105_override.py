"""Exercise the real R105 request decoder and its default-off gate."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/render-admission/include"
SHARED = ROOT / "drivers/apple-agx/shared/include"
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/allocation_windows.c"


def function_body(name):
    source = SOURCE.read_text()
    start = source.index("static VOID " + name + "(")
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class R105Override(unittest.TestCase):
    def test_default_off_and_exact_request_validation(self):
        program = r'''
#include <assert.h>
#include <string.h>
#include "render_allocation_probe.h"
int main(void) {
  ADMISSION_WIN32_ALLOCATION_CREATE request = {0};
  ADMISSION_R105_OVERRIDE result = {0};
  request.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  request.Version = ADMISSION_WIN32_ALLOCATION_VERSION;
  request.Bytes = sizeof(request);
  request.ClassId = 1;
  request.Reserved[0] = ADMISSION_R105_MAGIC;
  request.Reserved[1] = (7u << 8) | (2u << 3) | (1u << 2) | 2u;
  assert(!AdmissionR105Decode(&request, sizeof(request), 0, &result));
  assert(AdmissionR105Decode(&request, sizeof(request), 1, &result));
  assert(result.Token == 7 && result.ReadMode == 2 &&
         result.PageMode == 2 && result.AccessedPhysically == 1);
  request.Reserved[1] |= 0x10000u;
  assert(!AdmissionR105Decode(&request, sizeof(request), 1, &result));
  request.Reserved[1] = (1u << 5) | (9u << 8);
  assert(AdmissionR105Decode(&request, sizeof(request), 1, &result));
  assert(result.CloneClass0 == 1 && result.Token == 9);
  request.Reserved[0] = 0;
  assert(!AdmissionR105Decode(&request, sizeof(request), 1, &result));
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "probe.c"
            exe = Path(directory) / "probe"
            path.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                            "-Wextra", "-Werror", "-I", str(INCLUDE),
                            "-I", str(SHARED),
                            str(path), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_kmd_applies_override_only_in_qualification(self):
        source = SOURCE.read_text()
        self.assertIn("#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)", source)
        self.assertIn("AdmissionR105Decode", source)
        self.assertIn("AdmissionR105Apply", source)
        self.assertIn("AdmissionR105RecordEcho", source)

    def test_actual_kmd_output_policy_matrix_and_class0_clone(self):
        program = r'''
#include <assert.h>
#include <string.h>
#define VOID void
#define UINT unsigned
#define ADMISSION_ALLOCATION_ALIGNMENT 0x10000u
#define ADMISSION_LOCAL_SEGMENT_SET 2u
#define ADMISSION_CPU_VISIBLE_SEGMENT_SET 3u
#define DXGK_PAGESIZE_4KB 0u
#define DXGK_PAGESIZE_16KB 2u
typedef struct { unsigned AccessedPhysically; } FLAGS;
typedef struct { union { unsigned Alignment; struct { unsigned short MinimumPageSize,
  RecommendedPageSize; }; }; union { unsigned SupportedReadSegmentSet, MmuSet; };
  unsigned SupportedWriteSegmentSet; FLAGS FlagsWddm2; } DXGK_ALLOCATIONINFO;
typedef struct { unsigned Token, ReadMode, AccessedPhysically,
  PageMode, CloneClass0; } ADMISSION_R105_OVERRIDE;
''' + function_body("AdmissionR105Apply") + r'''
int main(void) {
  DXGK_ALLOCATIONINFO info;
  ADMISSION_R105_OVERRIDE request = {0};
  for (unsigned read=0; read<3; ++read)
    for (unsigned physical=0; physical<2; ++physical)
      for (unsigned page=0; page<3; ++page) {
        memset(&info, 0, sizeof(info));
        info.SupportedWriteSegmentSet = 2;
        request.ReadMode=read; request.AccessedPhysically=physical;
        request.PageMode=page; request.CloneClass0=0;
        AdmissionR105Apply(&info, &request);
        assert(info.SupportedReadSegmentSet == (read==0 ? 2u : read==1 ? 3u : 1u));
        assert(info.FlagsWddm2.AccessedPhysically == physical);
        assert(info.SupportedWriteSegmentSet == 2u);
        if (page==0) assert(info.Alignment==0x10000u);
        else assert(info.MinimumPageSize == (page==1 ? 2u : 0u) &&
                    info.RecommendedPageSize == info.MinimumPageSize);
      }
  memset(&info, 0, sizeof(info)); request.CloneClass0=1;
  AdmissionR105Apply(&info, &request);
  assert(info.Alignment==0x10000u && info.SupportedReadSegmentSet==3u &&
         info.SupportedWriteSegmentSet==3u && info.FlagsWddm2.AccessedPhysically==1u);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "apply.c"
            exe = Path(directory) / "apply"
            path.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                            "-Wextra", "-Werror", str(path), "-o", str(exe)],
                           check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
