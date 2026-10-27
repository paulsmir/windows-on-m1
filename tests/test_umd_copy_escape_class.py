"""Execute the production RC64 copy-escape attribution helpers.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1985 showed only the site
total (draw.copy_escape 29-37 ms). The per-Execute class receipt must put each
escape of a slot transfer under its operation and the slot's size class
(<=64 KiB, <=1 MiB, <=16 MiB, larger), restore the outer class after a nested
transfer, count escapes outside a slot transfer separately, and convert QPC
ticks to microseconds without overflow, or the next hardware run would assign
the copy escapes to the wrong BOs.
"""
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx"
SOURCE = DRIVER / "render-admission/umd/src/umd_gpuva_windows.c"

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include "umd_native_arena.h"
using UINT=unsigned; using ULONGLONG=unsigned long long; using LONG64=long long;
union LARGE_INTEGER {long long QuadPart;};
#define APPLE_AGX_G3_COPY_QUERY 0u
#define APPLE_AGX_G3_COPY_UPLOAD 1u
#define APPLE_AGX_G3_COPY_DOWNLOAD 2u
struct ADMISSION_UMD_DEVICE {UINT CopyEscapeClass; UINT CopyEscapeCount[13]; ULONGLONG CopyEscapeMicros[13];};
static long long hz=24000000;
static int QueryPerformanceFrequency(LARGE_INTEGER *v) {v->QuadPart=hz; return 1;}
@@HELPERS@@
int main() {
  ADMISSION_UMD_DEVICE d={};
  copy_escape_account(&d,APPLE_AGX_G3_COPY_UPLOAD,24000);          /* outside: 1 ms */
  assert(d.CopyEscapeCount[12]==1 && d.CopyEscapeMicros[12]==1000);
  {
    const copy_escape_scope query_bo(&d,65536);                     /* Asahi query BO */
    copy_escape_account(&d,APPLE_AGX_G3_COPY_QUERY,2400);
    copy_escape_account(&d,APPLE_AGX_G3_COPY_DOWNLOAD,12000);
    {
      const copy_escape_scope heap(&d,128ull<<20);                  /* EXP1985 upload heap */
      copy_escape_account(&d,APPLE_AGX_G3_COPY_UPLOAD,14400);
    }
    assert(d.CopyEscapeClass==1);
    copy_escape_account(&d,APPLE_AGX_G3_COPY_DOWNLOAD,12000);
  }
  assert(!d.CopyEscapeClass);
  { const copy_escape_scope encoder(&d,524288); copy_escape_account(&d,APPLE_AGX_G3_COPY_UPLOAD,1); }
  { const copy_escape_scope edge(&d,ADMISSION_UMD_NATIVE_SMALL_SLICE+65536ull);
    copy_escape_account(&d,APPLE_AGX_G3_COPY_UPLOAD,0); }
  { const copy_escape_scope edge(&d,ADMISSION_UMD_NATIVE_SLICE_LIMIT);
    copy_escape_account(&d,7u,0); }                                 /* unknown op */
  assert(d.CopyEscapeCount[0]==1 && d.CopyEscapeMicros[0]==100);
  assert(d.CopyEscapeCount[8]==2 && d.CopyEscapeMicros[8]==1000);
  assert(d.CopyEscapeCount[7]==1 && d.CopyEscapeMicros[7]==600);
  assert(d.CopyEscapeCount[5]==1 && d.CopyEscapeMicros[5]==0);
  assert(d.CopyEscapeCount[6]==1 && d.CopyEscapeCount[12]==2);
  /* Ten hours of ticks at 24 MHz: no 64-bit overflow in the conversion. */
  ADMISSION_UMD_DEVICE e={};
  copy_escape_account(&e,APPLE_AGX_G3_COPY_UPLOAD,36000ull*24000000ull+12);
  assert(e.CopyEscapeMicros[12]==36000ull*1000000ull);
  e.CopyEscapeCount[12]=~0u; copy_escape_account(&e,APPLE_AGX_G3_COPY_UPLOAD,0);
  assert(e.CopyEscapeCount[12]==~0u);
  puts("copy escape class PASS");
  return 0;
}
'''


class CopyEscapeClass(unittest.TestCase):
    def test_actual_attribution_helpers(self):
        text = SOURCE.read_text()
        helpers = [function(text, name) for name in ("copy_escape_size_class", "copy_escape_account")]
        self.assertTrue(all(helpers), "copy escape attribution helper missing")
        scope = re.search(r"(?ms)^struct copy_escape_scope \{.*?^\};", text)
        self.assertTrue(scope, "copy escape scope missing")
        escape = function(text, "copy_escape")
        self.assertIn("copy_escape_account(device, payload->Operation,", escape)
        self.assertLess(escape.index("pfnEscapeCb("), escape.index("copy_escape_account("))
        transfer = function(text, "transfer_slot")
        self.assertTrue(transfer.split("{", 1)[1].lstrip().startswith(
            "const copy_escape_scope escape_class(device, slot->Bytes);"),
            "the scope must cover every copy escape of the slot transfer")
        with tempfile.TemporaryDirectory(prefix="umd-copy-escape-class-") as path:
            tmp = Path(path)
            src = tmp / "class.cpp"
            exe = tmp / "class"
            src.write_text(HARNESS.replace("@@HELPERS@@", "\n".join(helpers[:1] + [scope.group(0)] + helpers[1:])))
            build = subprocess.run([
                "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(DRIVER / "render-admission/umd/include"), str(src), "-o", str(exe),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("copy escape class PASS", run.stdout)


if __name__ == "__main__":
    unittest.main()
