"""The EXP959 failure receipt must survive saturated startup diagnostics.

The first DWM Flush failure occurred after both DWM processes had exhausted
the 128-record normal diagnostic budget, making EXP959 inconclusive.
"""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c"


class UmdDiagnosticQuotaTests(unittest.TestCase):
    def test_late_deallocate_failure_has_bounded_separate_budget(self):
        source = SOURCE.read_text()
        start = source.index("static BOOL AdmissionUmdDiagnosticPermit(")
        end = source.index("\n}\n", start) + 2
        function = source[start:end]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef const char *PCSTR;
typedef int32_t HRESULT;
typedef int32_t LONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define FAILED(x) ((x) < 0)
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG c) {
  LONG old=*p; if(old==c)*p=v; return old;
}
typedef uint32_t DWORD;
#include <stddef.h>
typedef wchar_t WCHAR;
static LONG InterlockedExchange(volatile LONG *p,LONG v) { LONG old=*p; *p=v; return old; }
static DWORD GetLastError(void) { return 0; }
static void SetLastError(DWORD e) { (void)e; }
static DWORD GetEnvironmentVariableW(const WCHAR *n, WCHAR *b, DWORD c) { (void)n; (void)b; (void)c; return 0; }
''' + function + r'''
int main(void) {
  volatile LONG normal=0, failures=0, retirement_failures=0;
  for(int i=0;i<128;i++)
    assert(AdmissionUmdDiagnosticPermit("g4-open-adapter-enter",0,&normal,&failures,&retirement_failures));
  assert(!AdmissionUmdDiagnosticPermit("g4-open-adapter-enter",0,&normal,&failures,&retirement_failures));
  assert(normal==128 && failures==0);
  assert(AdmissionUmdDiagnosticPermit("umd-deallocate-failure",(HRESULT)0x80070057u,&normal,&failures,&retirement_failures));
  assert(normal==128 && failures==1);
  for(int i=1;i<16;i++)
    assert(AdmissionUmdDiagnosticPermit("umd-deallocate-failure",(HRESULT)0x80070057u,&normal,&failures,&retirement_failures));
  assert(!AdmissionUmdDiagnosticPermit("umd-deallocate-failure",(HRESULT)0x80070057u,&normal,&failures,&retirement_failures));
  assert(failures==17);
  assert(AdmissionUmdDiagnosticPermit("umd-retirement-failure",(HRESULT)0x80070057u,&normal,&failures,&retirement_failures));
  assert(retirement_failures==1 && normal==128);
  assert(AdmissionUmdDiagnosticPermit("measure-native-flush-stage",0,&normal,&failures,&retirement_failures));
  assert(AdmissionUmdDiagnosticPermit("reject-seterror",(HRESULT)0x80070057u,&normal,&failures,&retirement_failures));
  assert(!AdmissionUmdDiagnosticPermit("umd-deallocate-failure",0,&normal,&failures,&retirement_failures));
  /* EXP1131: runtime errors and device lifetime survive a saturated normal
   * budget, on their own bound of 64. */
  for(int i=0;i<32;i++) {
    assert(AdmissionUmdDiagnosticPermit("runtime-set-error",(HRESULT)0x887a0005u,&normal,&failures,&retirement_failures));
    assert(AdmissionUmdDiagnosticPermit(i&1?"device-destroy":"device-create",0,&normal,&failures,&retirement_failures));
  }
  assert(!AdmissionUmdDiagnosticPermit("device-create",0,&normal,&failures,&retirement_failures));
  assert(!AdmissionUmdDiagnosticPermit("runtime-set-error",(HRESULT)0x887a0005u,&normal,&failures,&retirement_failures));
  assert(normal==128);
  assert(!AdmissionUmdDiagnosticPermit("ddi-submit",0,&normal,&failures,&retirement_failures));
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.c"
            binary = Path(tmp) / "test"
            path.write_text(program)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", str(path), "-o",
                str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
