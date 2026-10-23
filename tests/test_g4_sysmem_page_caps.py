"""T7e: execute the advertised page-size contract using pinned WDK types."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WDK = ROOT / ".local/reference/wdk26100"
LIFECYCLE = ROOT / "drivers/apple-agx/render-admission/src/lifecycle.c"


def declaration(text, name):
    match = re.search(r"typedef (?:struct|enum) _" + name +
                      r"\s*\{.*?\}\s*" + name + r";", text, re.S)
    if match is None:
        raise AssertionError("missing pinned WDK declaration: " + name)
    return match.group()


def replay_source():
    wdk = (WDK / "d3dkmddi.h").read_text()
    shared = (WDK / "d3dukmdt.h").read_text()
    versions = "\n".join(line for line in shared.splitlines()
                         if line.startswith("#define DXGKDDI_INTERFACE_VERSION_"))
    lifecycle = LIFECYCLE.read_text()
    case = lifecycle[lifecycle.index("case DXGKQAITYPE_GPUMMUCAPS:"):
                     lifecycle.index("case DXGKQAITYPE_PAGETABLELEVELDESC:")]
    return r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_gpuva_g3_caps.h"
typedef uint32_t UINT;
typedef uint64_t ULONGLONG;
''' + versions + r'''
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM3_0
#endif
''' + declaration(shared, "DXGK_PTE") + "\n" + "\n".join(declaration(wdk, name) for name in (
        "DXGK_PAGETABLEUPDATEMODE", "DXGK_QUERYGPUMMUCAPSIN", "DXGK_GPUMMUCAPS")) + r'''
/* The query's OS boundary only: readiness and caller-owned buffers. */
typedef struct { int Ready; } CONTEXT;
typedef struct {
  void *pInputData, *pOutputData;
  UINT InputDataSize, OutputDataSize;
} QUERY;
static UINT page_profile;
#define ADMISSION_GPUVA_G1B_PAGE_PROFILE page_profile
#define STATUS_SUCCESS 0
#define STATUS_INVALID_DEVICE_STATE (-1)
#define STATUS_BUFFER_TOO_SMALL (-2)
#define STATUS_INVALID_PARAMETER (-3)
#define DXGKQAITYPE_GPUMMUCAPS 1
#define RtlZeroMemory(p, n) memset(p, 0, n)
static int AdmissionGpuvaG3DeclarationReady(CONTEXT *c) { return c->Ready; }
static int query(CONTEXT *context, QUERY *QueryAdapterInfo) {
  int status = STATUS_INVALID_PARAMETER;
  switch (DXGKQAITYPE_GPUMMUCAPS) {
''' + case + r'''
  }
  return status;
}
int main(void) {
  CONTEXT context = {1};
  DXGK_QUERYGPUMMUCAPSIN input = {0};
  DXGK_GPUMMUCAPS output;
  QUERY q = {&input, &output, sizeof(input), sizeof(output)};
  const UINT profiles[] = {0, 16, 64};
  for (UINT i = 0; i < sizeof(profiles)/sizeof(profiles[0]); ++i) {
    page_profile = profiles[i];
    /* Catch omitted zeroing as well as an unsupported capability toggle. */
    memset(&output, 0xff, sizeof(output));
    assert(query(&context, &q) == STATUS_SUCCESS);
    assert(output.SysMem64KBPageSupported == 1);
    assert(output.LeafPageTableSizeFor64KPagesInBytes == 0x4000);
    assert(output.LargePageSupported == 0);
    assert(output.SysMemLargePageSupported == 0);
    assert(output.AllowNonAlignedLargePageAddress == 0);
    assert(output.DualPteSupported == 0);
    assert(output.Reserved == 0);
    assert(output.PageTableUpdateMode == DXGK_PAGETABLEUPDATE_GPU_PHYSICAL);
  }
  puts("T7e page caps: PASS (64K leaves supported; large/dual PTE modes absent)");
  return 0;
}
'''


class SysmemPageCaps(unittest.TestCase):
    def test_query_does_not_advertise_unimplemented_large_page_modes(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Enabling SysMemLargePageSupported/LargePageSupported to seek
        contiguous backing claims unimplemented large-page support. The
        actual query must retain 64K leaf support while leaving unsupported
        modes and reserved bits clear, even in a dirty caller output buffer.
        This guards an existing invariant; it cannot prove VidMm placement.
        """
        with tempfile.TemporaryDirectory(prefix="t7e-caps-") as tmp:
            tmp = Path(tmp)
            source, binary = tmp / "caps.c", tmp / "caps"
            source.write_text(replay_source())
            for version in ("WDDM3_0", "WDDM3_2"):
                with self.subTest(version=version):
                    build = subprocess.run([
                        os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined",
                        "-DDXGKDDI_INTERFACE_VERSION=DXGKDDI_INTERFACE_VERSION_" + version,
                        "-I", str(ROOT / "drivers/apple-agx/shared/include"), str(source),
                        "-o", str(binary),
                    ], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                    run = subprocess.run([str(binary)], capture_output=True, text=True,
                                         timeout=10)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertIn("T7e page caps: PASS", run.stdout)
