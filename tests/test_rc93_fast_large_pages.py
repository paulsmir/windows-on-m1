"""rc93: a 2 MiB slab comes only from Windows' large-page cache, never from
large-page construction, on the creation path.

EXP2028 arm a (GravityMark D3D12 HD 50k, the first app after boot):
Wom1M2PoolDemand v12 shows 1402 MmAllocatePagesForMdlEx slab calls taking
29.2 s (20.8 ms per 2 MiB), against 0.4 ms per slab for DWM's 98 slabs at
boot and 1.7 ms per slab for the second app (arm b, 958 calls, 1.65 s), whose
slabs reuse the 2 MiB runs arm a returned. Per-BO cost rose from 2 to 20
ms/MiB as the first app's memory grew. The slab's first attempt asks for one
2 MiB-aligned 2 MiB chunk (the ARM64 large page). Microsoft documents for
MmAllocatePagesForMdlEx that without MM_ALLOCATE_FAST_LARGE_PAGES, once the
large page cache is exhausted the call "attempts to construct additional
large pages, which may take a long time"; with the flag it fails at once and
the slab falls back to its existing 32 x 64 KiB chunked attempt.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_apple_agx_pool_headroom import function
import test_g4_pool_residency_limit as residency_limit

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
POOL = RENDER / 'src/pool_windows.c'
WDM = ROOT / '.local/reference/reference/winsdk26100/km/wdm.h'

FLAGS_HARNESS = r'''
#include <assert.h>
typedef unsigned long long ULONGLONG;
typedef unsigned int ULONG;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
/* wdm.h (WDK 26100) values */
#define MM_DONT_ZERO_ALLOCATION                 0x00000001
#define MM_ALLOCATE_FULLY_REQUIRED              0x00000004
#define MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS   0x00000020
#define MM_ALLOCATE_FAST_LARGE_PAGES            0x00000040
@@FLAGS@@
int main(void) {
  const ULONG base = MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED;
  /* Default: the 2 MiB attempt never constructs large pages. */
  assert(AdmissionPoolSlabAttemptFlags(0u, FALSE) == (base | MM_ALLOCATE_FAST_LARGE_PAGES));
  /* Kill switch: the pre-rc93 call exactly. */
  assert(AdmissionPoolSlabAttemptFlags(0u, TRUE) == base);
  /* The 64 KiB chunked fallback is not a large-page request: the flag is
   * documented only with SkipBytes a multiple of the large page size. */
  assert(AdmissionPoolSlabAttemptFlags(1u, FALSE) == base);
  assert(AdmissionPoolSlabAttemptFlags(1u, TRUE) == base);
  /* Pages always arrive zeroed: they reach user mode. */
  for (ULONG a = 0u; a < 2u; ++a)
    for (int c = 0; c < 2; ++c)
      assert(!(AdmissionPoolSlabAttemptFlags(a, c) & MM_DONT_ZERO_ALLOCATION));
  return 0;
}
'''


def build_and_run(source):
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / 't.c', Path(tmp) / 't'
        src.write_text(source)
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)


class FastLargePages(unittest.TestCase):
    def test_slab_attempt_flags(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        EXP2028: the first app's 2 MiB slab attempts cost 20.8 ms each
        (29.2 s per run, 3 s inside GravityMark's scored window) because
        Windows constructs large pages when its large-page cache is empty.
        The 2 MiB attempt must carry MM_ALLOCATE_FAST_LARGE_PAGES by default,
        only that attempt (the flag requires SkipBytes a multiple of the
        large page size), never MM_DONT_ZERO_ALLOCATION, and the kill switch
        must restore the previous flags exactly.
        """
        flags = function(POOL.read_text(), 'AdmissionPoolSlabAttemptFlags')
        build_and_run(FLAGS_HARNESS.replace('@@FLAGS@@', flags))

    def test_both_slab_callers_use_the_attempt_flags(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The creation path (AdmissionPoolNewSlab) and the opt-in prefill
        (AdmissionPoolPrefillWork) share AdmissionPoolAllocSlab; a literal
        flag set left in it would silently keep large-page construction.
        """
        source = POOL.read_text()
        alloc = function(source, 'AdmissionPoolAllocSlab')
        self.assertIn('AdmissionPoolSlabAttemptFlags(attempt', alloc)
        self.assertNotIn('MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS', alloc)
        self.assertIn('pool->ConstructLargePages', function(source, 'AdmissionPoolNewSlab'))
        self.assertIn('pool->ConstructLargePages', function(source, 'AdmissionPoolPrefillWork'))
        start = function(source, 'AdmissionPoolDemandStart')
        self.assertIn('pool->ConstructLargePages = AdmissionPoolConstructLargePagesEnabled(Context);',
                      start)

    def test_kill_switch_reader(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Only REG_DWORD Wom1PoolConstructLargePages=1 restores large-page
        construction; absent, mistyped or other values keep the fast path,
        and every opened key is closed.
        """
        residency_limit.PoolResidencyLimit.check_registry_reader(
            self, 'AdmissionPoolConstructLargePagesEnabled', 'Wom1PoolConstructLargePages', 1)

    def test_flag_value_matches_wdk(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The harness above mirrors wdm.h; the KMD uses the WDK macro. A
        mismatch would make the test prove the wrong bit.
        """
        if not WDM.exists():
            self.skipTest('WDK reference headers not linked')
        text = WDM.read_text(errors='replace')
        self.assertRegex(text, r'#define MM_ALLOCATE_FAST_LARGE_PAGES\s+0x00000040')
        self.assertRegex(text, r'#define MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS\s+0x00000020')
        self.assertRegex(text, r'#define MM_ALLOCATE_FULLY_REQUIRED\s+0x00000004')


if __name__ == '__main__':
    unittest.main()
