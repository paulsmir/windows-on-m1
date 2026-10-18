"""Receipt export must not sit on the job path (EXP1117-EXP1119).

The job-timing export ran every 16th job inside the platform worker, before
the worker released the render slot, and flushed the device key: the
receipt itself recorded 0.76-1.13 ms per export, ~37 exports/s at ~600
jobs/s, so a submission arriving meanwhile waited for the registry. Invariant,
with the real function compiled against stub kernel calls: over a burst of
jobs 650 us apart the export writes at most once per two seconds of QPC time
(plus the first) and never flushes the key.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c'


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start); depth = 1; end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
typedef unsigned long ULONG; typedef unsigned long long ULONGLONG; typedef long NTSTATUS;
typedef unsigned char KIRQL; typedef void *HANDLE; typedef void VOID; typedef void *PDEVICE_OBJECT;
typedef struct { unsigned short Length, MaximumLength; const wchar_t *Buffer; } UNICODE_STRING;
#define NULL 0
#define PASSIVE_LEVEL 0
#define NT_SUCCESS(s) ((s) >= 0)
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_SET_VALUE 2
#define REG_BINARY 3
#define RtlCopyMemory memcpy
typedef struct { ULONGLONG QpcFrequency, LastExportQpcTicks; int Slot[4]; } JOB_TIMING;
typedef struct { ULONG Version, Bytes, Count, Reserved; } FW_TIMING;
typedef struct { PDEVICE_OBJECT PhysicalDeviceObject; } ADMISSION_CONTEXT;
typedef struct {
  ADMISSION_CONTEXT *Adapter; int JobTimingLock; ULONG JobTimingWorkers;
  ULONGLONG JobTimingExportQpc;
  JOB_TIMING JobTiming, JobTimingSnapshot; FW_TIMING FwTiming, FwTimingSnapshot;
} ADMISSION_PLATFORM_RUNTIME;
static ULONGLONG qpc = 1; static int sets, flushes, opens;
static ULONGLONG AdmissionJobQpc(void) { return qpc; }
static KIRQL KeGetCurrentIrql(void) { return PASSIVE_LEVEL; }
static void KeAcquireSpinLock(int *, KIRQL *) {}
static void KeReleaseSpinLock(int *, KIRQL) {}
static NTSTATUS IoOpenDeviceRegistryKey(PDEVICE_OBJECT, ULONG, ULONG, HANDLE *k) { ++opens; *k = (HANDLE)1; return 0; }
static void RtlInitUnicodeString(UNICODE_STRING *s, const wchar_t *w) { s->Buffer = w; }
static NTSTATUS ZwSetValueKey(HANDLE, UNICODE_STRING *, ULONG, ULONG, const void *, ULONG) { ++sets; return 0; }
static NTSTATUS ZwFlushKey(HANDLE) { ++flushes; return 0; }
static void ZwClose(HANDLE) {}
@@FUNC@@
int main() {
  ADMISSION_CONTEXT adapter = {(PDEVICE_OBJECT)1};
  ADMISSION_PLATFORM_RUNTIME r; memset(&r, 0, sizeof(r));
  r.Adapter = &adapter; r.JobTiming.QpcFrequency = 24000000ULL; r.FwTiming.Count = 1;
  /* 10 s of jobs 650 us apart. */
  for (int i = 0; i < 15385; ++i) { AdmissionJobTimingExportWindows(&r); qpc += 15600ULL; }
  printf("opens %d sets %d flushes %d\n", opens, sets, flushes);
  assert(opens >= 1 && opens <= 6);
  assert(flushes == 0);
  puts("job timing export rate: PASS");
}
'''


class JobTimingExportRate(unittest.TestCase):
    def test_export_is_rate_limited_and_unflushed(self):
        text = SRC.read_text()
        func = function(text, 'static VOID AdmissionJobTimingExportWindows(')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'export.cpp'; exe = Path(tmp) / 'export'
            src.write_text(HARNESS.replace('@@FUNC@@', func))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-Wno-unused-variable', '-fsanitize=address,undefined',
                                    str(src), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
