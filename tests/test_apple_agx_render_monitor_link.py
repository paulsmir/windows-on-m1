"""Execute the production metadata callback with a minimal host ABI shim.

The WDK build verifies the actual ABI; this test catches clobbered input hints,
false capability bits, and mutation on rejected target/state inputs.
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class MonitorLinkTests(unittest.TestCase):
    def test_actual_callback_preserves_hints_and_reports_no_optional_features(self):
        source = (ROOT / "drivers/apple-agx/render-admission/src/display.c").read_text()
        match = re.search(
            r"_Use_decl_annotations_ NTSTATUS AdmissionDdiUpdateMonitorLinkInfo\(.*?^}",
            source, re.S | re.M)
        self.assertIsNotNone(match, "Production monitor-link callback is missing")
        shim = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define _Use_decl_annotations_
#define CONST const
#define STATUS_SUCCESS ((int32_t)0)
#define STATUS_INVALID_PARAMETER ((int32_t)0xc000000d)
typedef int32_t NTSTATUS;
typedef void *HANDLE;
typedef struct { unsigned char Started; } ADMISSION_CONTEXT;
typedef union { uint32_t Value; } WORD;
typedef struct { WORD UsageHints, Capabilities, DitheringSupport; } LINK;
typedef struct { uint32_t VideoPresentTargetId; LINK MonitorLinkInfo; }
    DXGKARG_UPDATEMONITORLINKINFO;
_Static_assert(sizeof(DXGKARG_UPDATEMONITORLINKINFO) == 16, "WDK ABI size");
'''
        cases = r'''
int main(void) {
  ADMISSION_CONTEXT adapter = {1};
  DXGKARG_UPDATEMONITORLINKINFO args, before;
  const uint32_t hints[] = {0, 1, 3, 0xffffffffu};
  for (unsigned int i = 0; i < sizeof(hints) / sizeof(hints[0]); ++i) {
    memset(&args, 0xa5, sizeof(args));
    args.VideoPresentTargetId = 0;
    args.MonitorLinkInfo.UsageHints.Value = hints[i];
    assert(AdmissionDdiUpdateMonitorLinkInfo(&adapter, &args) == STATUS_SUCCESS);
    assert(args.VideoPresentTargetId == 0);
    assert(args.MonitorLinkInfo.UsageHints.Value == hints[i]);
    assert(args.MonitorLinkInfo.Capabilities.Value == 0);
    assert(args.MonitorLinkInfo.DitheringSupport.Value == 0);
  }
  memset(&args, 0xa5, sizeof(args));
  before = args;
  assert(AdmissionDdiUpdateMonitorLinkInfo(&adapter, &args) == STATUS_INVALID_PARAMETER);
  assert(memcmp(&args, &before, sizeof(args)) == 0);
  args.VideoPresentTargetId = 0;
  before = args;
  adapter.Started = 0;
  assert(AdmissionDdiUpdateMonitorLinkInfo(&adapter, &args) == STATUS_INVALID_PARAMETER);
  assert(memcmp(&args, &before, sizeof(args)) == 0);
  assert(AdmissionDdiUpdateMonitorLinkInfo(0, &args) == STATUS_INVALID_PARAMETER);
  assert(AdmissionDdiUpdateMonitorLinkInfo(&adapter, 0) == STATUS_INVALID_PARAMETER);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            # Generated compiler input, not a separate implementation.
            test = Path(tmp) / "monitor_link.c"
            test.write_text(shim + match.group(0) + cases)
            binary = Path(tmp) / "monitor_link"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11",
                            "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", str(test), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
