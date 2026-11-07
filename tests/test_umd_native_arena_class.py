"""Execute the production measure-native-arena-class receipt.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? RC66 (EXP1986): when the
native arenas refuse a slice, the receipt must name the native bytes and slot
counts held by each class (General, Shader, Encoder), counting only live slots
that own a native slice, 64 KiB-rounded as the slice is, and must not count
inactive, private-staging or unknown-class slots. A wrong class index would
blame the wrong BOs for the exhausted budget; the scan must release its lock
before the diagnostic.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx"
SOURCE = DRIVER / "render-admission/umd/src/umd_win32_screen.c"

HARNESS = r'''
#include <cassert>
#include <cstring>
#include "apple_agx_win32_device_info.h"
using VOID=void; using BOOL=int; using BYTE=unsigned char; using UINT=unsigned;
using ULONGLONG=unsigned long long; using HRESULT=int;
#define S_OK 0
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) 8u
struct ADMISSION_UMD_SCREEN_BUFFER {
  BOOL Active; UINT ClassId; ULONGLONG Bytes; BYTE *NativeStagingAddress;
};
struct ADMISSION_UMD_DEVICE {
  int ScreenBufferLock; ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[8];
};
static int depth;
static void AcquireSRWLockShared(int *l) {assert(!*l && !depth); *l=1; ++depth;}
static void ReleaseSRWLockShared(int *l) {assert(*l && depth==1); *l=0; --depth;}
static UINT got[9], receipts;
static void AdmissionUmdDiagnostic(const char *name, HRESULT status, const UINT *v, UINT count) {
  assert(!depth && status==S_OK && count==9 && !strcmp(name,"measure-native-arena-class"));
  memcpy(got,v,sizeof(got)); ++receipts;
}
@@FUNCTION@@
int main() {
  static BYTE slice[1];
  ADMISSION_UMD_DEVICE d = {};
  d.ScreenBuffers[0] = {1, AgxWin32BufferClassShader, 16384, slice};    /* 64 KiB */
  d.ScreenBuffers[1] = {1, AgxWin32BufferClassShader, 65536, slice};    /* 64 KiB */
  d.ScreenBuffers[2] = {1, AgxWin32BufferClassEncoder, 524288, slice};  /* 512 KiB */
  d.ScreenBuffers[3] = {1, AgxWin32BufferClassGeneral, 81920, slice};   /* 128 KiB */
  d.ScreenBuffers[4] = {0, AgxWin32BufferClassGeneral, 65536, slice};   /* inactive */
  d.ScreenBuffers[5] = {1, AgxWin32BufferClassGeneral, 65536, nullptr}; /* private */
  d.ScreenBuffers[6] = {1, 0u, 65536, slice};                           /* unknown */
  d.ScreenBuffers[7] = {1, 4u, 65536, slice};                           /* unknown */
  AdmissionUmdNativeStagingClassReceipt(&d, 1u, AgxWin32BufferClassGeneral);
  const UINT expect[9] = {1u, 1u, 1u, 131072u, 131072u, 524288u, 1u, 2u, 1u};
  assert(receipts==1 && !memcmp(got, expect, sizeof(expect)) && !d.ScreenBufferLock);
  return 0;
}
'''


class NativeArenaClassReceipt(unittest.TestCase):
    def test_actual_receipt_counts_native_slices_by_class(self):
        body = function(SOURCE.read_text(), "AdmissionUmdNativeStagingClassReceipt")
        self.assertTrue(body, "production class receipt missing")
        with tempfile.TemporaryDirectory(prefix="umd-arena-class-") as path:
            src = Path(path) / "receipt.cpp"
            exe = Path(path) / "receipt"
            src.write_text(HARNESS.replace("@@FUNCTION@@", body))
            build = subprocess.run([
                "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(DRIVER / "shared/include"),
                str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
