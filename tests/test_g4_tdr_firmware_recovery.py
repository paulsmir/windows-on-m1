"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

The KMD omitted Asahi's pending-stamp acknowledgement before firmware resume.
Replay the real transport function: clearing must be bounded to valid records,
ordered before resume, and refused when the mapping or halt proof is absent.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class FirmwareRecovery(unittest.TestCase):
    def test_pending_stamps_cleared_before_resume(self):
        source = (RENDER / 'src/backend_platform_windows.c').read_text()
        header = (RENDER / 'include/render_admission.h').read_text()
        defines = '\n'.join(re.search(r'^#define ' + n + r' .+$', text, re.M)[0]
                            for n, text in (
                                ('ADMISSION_REGIONC_PENDING_STAMPS_OFFSET', source),
                                ('ADMISSION_REGIONC_PENDING_STAMPS_BYTES', header)))
        program = defines + '\n' + SHIM + re.search(r'static APPLE_AGX_BACKEND_BOOL AdmissionTransportRecover\(.*?^}', source, re.S | re.M)[0] + CASES
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'recover.c'
            path.write_text(program)
            binary = Path(tmp) / 'recover'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-fsanitize=address,undefined',
                            '-I', str(RENDER / 'include'), str(path), str(RENDER / 'src/render_gpu_fault.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)




SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_gpu_fault.h"
typedef unsigned int ULONG, APPLE_AGX_BACKEND_U32;
typedef unsigned long long ULONGLONG;
typedef int APPLE_AGX_BACKEND_BOOL, BOOLEAN;
typedef unsigned char UCHAR;
#define TRUE 1
#define APPLE_AGX_BACKEND_TRUE 1
#define APPLE_AGX_BACKEND_FALSE 0
#define PASSIVE_LEVEL 0
#define AppleAgxInitdataMemoryFirmwareStatus 0
#define AppleAgxInitdataMemoryRegionC 1
typedef struct { void *CpuAddress; ULONGLONG Length; } APPLE_AGX_MEMORY_OBJECT;
typedef struct {
  struct { struct { unsigned PendingFence; } QueueProvider; } Provider;
  struct { APPLE_AGX_MEMORY_OBJECT DataObjects[2]; } Initdata;
  ADMISSION_GPU_FAULT_RECEIPT GpuFault;
  unsigned GpuFaultUnpublished, FirmwareRecoveries;
  unsigned LastFirmwareRecoveryKind, LastFirmwareRecoveryFence;
  unsigned LastFirmwareRecoveryMs, MaxFirmwareRecoveryMs, FirmwareRecoveryUnpublished;
  ULONGLONG JobSubmitMs;
} ADMISSION_PLATFORM_RUNTIME;
static unsigned irql, now, barriers, observed_resume;
static ULONG status_words[0x80/4];
static ULONG region_words[(ADMISSION_REGIONC_PENDING_STAMPS_OFFSET +
                           ADMISSION_REGIONC_PENDING_STAMPS_BYTES + 16)/4];
static ULONG *pending = region_words + ADMISSION_REGIONC_PENDING_STAMPS_OFFSET/4;
static unsigned KeGetCurrentIrql(void) { return irql; }
static ULONGLONG AdmissionPlatformNowMs(void) { return now++; }
static void KeStallExecutionProcessor(unsigned us) { (void)us; }
static void KeMemoryBarrier(void) {
  ++barriers;
  if (status_words[0x30/4]) {
    /* Test at the publication boundary, not only after return. */
    assert(pending[0] == 0 && pending[1] == 0);
    assert(pending[510] == 0 && pending[511] == 0);
    ++observed_resume;
  }
}
static void InterlockedIncrement(unsigned *p) { ++*p; }
static void AdmissionGpuFaultCapture(ADMISSION_PLATFORM_RUNTIME *r,
    ULONG f, ULONG k, const volatile ULONG *s, ADMISSION_GPU_FAULT_EVENT *e) {
  (void)r; (void)s; memset(e, 0, sizeof(*e)); e->Fence=f; e->EventKind=k;
}
static ADMISSION_PLATFORM_RUNTIME init(void) {
  ADMISSION_PLATFORM_RUNTIME r = {0};
  memset(status_words, 0, sizeof(status_words));
  memset(region_words, 0, sizeof(region_words));
  pending[-1] = 0xdeadbeef; pending[512] = 0xdeadbeef;
  pending[0] = (7u<<4)|1u; pending[1] = 0x200u;
  pending[510] = (9u<<4)|3u; pending[511] = 0x300u;
  pending[4] = 0x22u; pending[5] = 0xaabbccdd; /* not pending */
  status_words[0x20/4] = 1;
  r.Provider.QueueProvider.PendingFence = 37;
  r.Initdata.DataObjects[0].CpuAddress = status_words;
  r.Initdata.DataObjects[0].Length = sizeof(status_words);
  r.Initdata.DataObjects[1].CpuAddress = region_words;
  r.Initdata.DataObjects[1].Length = sizeof(region_words)-16;
  irql=now=barriers=observed_resume=0;
  return r;
}
'''
CASES = r'''
int main(void) {
  ADMISSION_PLATFORM_RUNTIME r = init();
  assert(AdmissionTransportRecover(&r, 37, 4));
  assert(observed_resume && status_words[0x20/4] == 0);
  assert(pending[4] == 0x22 && pending[5] == 0xaabbccdd);
  assert(pending[-1] == 0xdeadbeef && pending[512] == 0xdeadbeef);
  assert(r.FirmwareRecoveries == 1 && r.LastFirmwareRecoveryFence == 37);
  r=init(); r.Initdata.DataObjects[1].Length--;
  assert(!AdmissionTransportRecover(&r, 37, 4));
  assert(!status_words[0x30/4] && pending[1] == 0x200);
  r=init(); r.Initdata.DataObjects[1].CpuAddress = 0;
  assert(!AdmissionTransportRecover(&r, 37, 4));
  assert(!status_words[0x30/4]);
  r=init(); status_words[0x20/4]=0;
  assert(!AdmissionTransportRecover(&r, 37, 4));
  assert(!status_words[0x30/4] && pending[1] == 0x200);
  r=init();
  assert(!AdmissionTransportRecover(&r, 38, 4));
  assert(!status_words[0x30/4] && pending[1] == 0x200);
  r=init(); irql=2;
  assert(!AdmissionTransportRecover(&r, 37, 4));
  return 0;
}
'''
