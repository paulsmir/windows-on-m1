"""The reset's post-resume receipt must not read SGX MMIO.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2027 (rc92): the G4-r3 TDR drill wedged the guest. The faulting
instruction (ELR RVA 0x44264 in the EXP2027 sys, inside
AdmissionResetEngineInternal with AdmissionPlatformRuntimeQuiesceActive and
AdmissionCapturePostResume inlined) is the 64-bit READ_REGISTER of SGX
FAULT_INFO (PA 0x204017030) at the end of the reset's quiescence wait, taken
because Wom1GpuFaultInfoRead = 1. The GPU had been idle for about TdrDelay
(the firmware powers the SGX block down 20 ms after its last work,
apple_agx_regionc.c idle_to_off_delay_ms), the fabric answered with an
external abort (L2C_ERR_ADR 0x...204017030), m1n1 hv_handle_dabort could not
translate FAR and the guest re-executed the load for ever (4030 aborts).
EXP592 had the same loop for a late FAULT_INFO sample after completion.

INVARIANT: SGX GPU registers are touched only while the GPU is known to be
powered: the KMD's own power-on before the firmware starts
(AdmissionFirmwarePowerOn), the firmware's Timeout/Fault event before resume
(AdmissionGpuFaultCapture, Asahi gpu.rs handle_timeout/handle_fault), the
submit-qualification snapshot of a job still Submitted (EXP593 rule), and the
opt-in start-time soft-fault receipt. AdmissionCapturePostResume runs after
the resume, at the end of a wait that can last a whole TdrDelay with the GPU
idle, so it reads firmware-shared memory only.

Q13 also requires the reset receipt to retain RTKit crash/message metadata and
both published job identities, without treating a GPU address as a CPU pointer.

The real AdmissionCapturePostResume is compiled with both opt-in switches on
and MMIO accessors that abort, as the hardware did.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'
BACKEND = RENDER / 'src/backend_platform_windows.c'

# Functions allowed to issue SGX register reads or writes, and why.
SGX_MMIO_OWNERS = {
    # KMD-owned power-on (broker acquired), before the firmware starts.
    'AdmissionFirmwarePowerOn',
    # Firmware Timeout/Fault event, before resume (Asahi order).
    'AdmissionGpuFaultCapture',
    # APPLE_AGX_SUBMIT_QUALIFICATION only; job still Submitted (EXP593).
    'AdmissionCaptureQueueFaultSnapshot',
    # Opt-in Wom1GpuFaultControlRead at runtime start (rc82, EXP2011).
    'AdmissionPublishSoftFaults',
}


def define(source, name):
    match = re.search(r'#define ' + name + r' (0x[0-9a-fA-F]+u)', source)
    if not match:
        raise AssertionError(f'missing {name}')
    return f'#define {name} {match.group(1)}\n'


def sgx_mmio_functions(source):
    """Enclosing top-level function of every READ/WRITE_REGISTER on SgxBase."""
    owners = {}
    current = None
    pending = ''
    for number, line in enumerate(source.splitlines(), 1):
        header = re.match(r'^(?:_Use_decl_annotations_\s+)?(?:static\s+)?'
                          r'(?:__declspec\(\w+\)\s+)?[A-Za-z_][\w \*]*?\b'
                          r'([A-Za-z_]\w*)\s*\(', line)
        if header and not line.startswith((' ', '\t', '#')) and \
                not line.rstrip().endswith(';'):
            current = header.group(1)
        pending = (pending + line) if pending else line
        if 'REGISTER_' in pending or 'SgxBase' in pending:
            if re.search(r'(READ|WRITE)_REGISTER_\w+\s*\(\s*\(volatile \w+ \*\)\s*\(\s*'
                         r'\w+->SgxBase', re.sub(r'\s+', ' ', pending)):
                owners.setdefault(current, []).append(number)
                pending = ''
            elif not line.rstrip().endswith(('(', '+', ',')) and \
                    'REGISTER_' not in line:
                pending = ''
        else:
            pending = ''
    return owners


SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_memory.h"
#include "apple_agx_initdata_memory.h"
#include "apple_agx_g13_queue_runtime.h"
#include "render_gpu_fault.h"
#include "apple_agx_rtkit_session.h"
#include "render_job_window.h"
#define VOID void
typedef uint32_t ULONG;
typedef uint64_t ULONG64;
typedef unsigned char UCHAR;
typedef int BOOLEAN;
typedef struct {
  APPLE_AGX_BACKEND_BOOL (*ReadU32)(void *Context,
      const volatile APPLE_AGX_BACKEND_U32 *Address, APPLE_AGX_BACKEND_U32 *Value);
} MODEL_TRANSPORT_IO;
typedef struct {
  struct { struct { APPLE_AGX_G13_QUEUE_RUNTIME Runtime; } QueueProvider; } Provider;
  struct { APPLE_AGX_MEMORY_OBJECT DataObjects[APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT]; } Initdata;
  APPLE_AGX_RTKIT_SESSION Rtkit;
  BOOLEAN ReadFaultInfo, ReadFaultControl;
  volatile UCHAR *SgxBase;
  MODEL_TRANSPORT_IO TransportIo;
} ADMISSION_PLATFORM_RUNTIME;
static int barriers;
static void KeMemoryBarrier(void) { ++barriers; }
/* The GPU may be power-gated here: the hardware answered an external abort
 * that m1n1 cannot emulate (EXP2027). Any SGX access is the bug. */
static ULONG64 READ_REGISTER_ULONG64(volatile ULONG64 *a) {
  fprintf(stderr, "SGX MMIO read64 at +%#lx after resume\n", (unsigned long)
          ((volatile UCHAR *)a - (volatile UCHAR *)0x204000000ull));
  abort();
}
static ULONG READ_REGISTER_ULONG(volatile ULONG *a) {
  fprintf(stderr, "SGX MMIO read32 at +%#lx after resume\n", (unsigned long)
          ((volatile UCHAR *)a - (volatile UCHAR *)0x204000000ull));
  abort();
}
static APPLE_AGX_BACKEND_BOOL read_u32(void *c, const volatile APPLE_AGX_BACKEND_U32 *a,
                                       APPLE_AGX_BACKEND_U32 *v) {
  (void)c; *v = *a; return 1;
}
'''

CASE = r'''
static ULONG status_words[16];
static unsigned char regionc[0x12000];
static APPLE_AGX_BACKEND_U32 ring_state[32];
int main(void) {
  static ADMISSION_PLATFORM_RUNTIME runtime;
  ADMISSION_GPU_POST_RESUME_RECEIPT receipt;
  APPLE_AGX_G13_QUEUE_RUNTIME *queue = &runtime.Provider.QueueProvider.Runtime;
  unsigned i;
  memset(&receipt, 0x5a, sizeof(receipt));
  for (i = 0; i < 16; ++i) status_words[i] = 0x100u + i;
  status_words[4] = 1u; status_words[8] = 0u; status_words[12] = 1u;
  *(ULONG *)(regionc + ADMISSION_REGIONC_FAULT_CONTROL_OFFSET) = 0xbu;
  for (i = 0; i < 6; ++i)
    ((ULONG *)(regionc + ADMISSION_REGIONC_FAULT_INFO_OFFSET))[i] = 0x200u + i;
  ((ULONG *)(regionc + ADMISSION_REGIONC_PENDING_STAMPS_OFFSET))[0] = 1u;
  ((ULONG *)(regionc + ADMISSION_REGIONC_PENDING_STAMPS_OFFSET))[4] = 1u;
  ((ULONG *)(regionc + ADMISSION_REGIONC_PENDING_STAMPS_OFFSET))[3] = 1u; /* a wait_value */
  runtime.Initdata.DataObjects[AppleAgxInitdataMemoryFirmwareStatus].CpuAddress = status_words;
  runtime.Initdata.DataObjects[AppleAgxInitdataMemoryFirmwareStatus].Length = sizeof(status_words);
  runtime.Initdata.DataObjects[AppleAgxInitdataMemoryRegionC].CpuAddress = regionc;
  runtime.Initdata.DataObjects[AppleAgxInitdataMemoryRegionC].Length = sizeof(regionc);
  /* Asahi RingState: done +0, rptr +0x30, cpu wptr +0x40. */
  ring_state[0] = 930u; ring_state[12] = 931u; ring_state[16] = 931u;
  queue->Config.HasCompute = 1;
  queue->Config.Compute.GpuDonePointer = &ring_state[0];
  queue->Config.Compute.CpuWritePointer = &ring_state[16];
  queue->PostRecoveryEvents = 1u; queue->PostRecoveryKindMask = 1u << 4;
  queue->LastPostRecoveryKind = 4u;
  runtime.ReadFaultInfo = 1; runtime.ReadFaultControl = 1;
  runtime.SgxBase = (volatile UCHAR *)0x204000000ull;
  runtime.TransportIo.ReadU32 = read_u32;
  runtime.Rtkit.Running = runtime.Rtkit.CpuReady = 1;
  runtime.Rtkit.CrashlogReplySent = runtime.Rtkit.CrashlogCrashed = 1;
  runtime.Rtkit.ReceivedCount = 73; runtime.Rtkit.LastRxEndpoint = 1;
  runtime.Rtkit.LastRxPayload = 0x10000000004000ULL;
  runtime.Rtkit.InboxAtFailureValid = 1;
  runtime.Rtkit.InboxControlAtFailure = 0x220;
  runtime.Rtkit.CrashlogGpuAddress = 0xa0001234000ULL;
  runtime.Rtkit.CrashlogCapacityBytes = 0x4000;
  runtime.Rtkit.CrashlogRequestedBytes = 0x1000;
  queue->PendingFence = 0x182a; queue->ComputeInRender = 1;
  queue->Successor.Fence = 0x182b; queue->Successor.ComputeTerminal = 1;
  memset(&receipt.Rtkit, 0xa5, sizeof(receipt.Rtkit));
  AdmissionCapturePostResume(&runtime, 0x4f2u, 2003u, &receipt);
  assert(receipt.Rtkit.Captured == 1 && receipt.Rtkit.Crashed == 1);
  assert(receipt.Rtkit.LastEndpoint == 1 && receipt.Rtkit.ReceivedCount == 73);
  assert(receipt.Rtkit.LastPayload == runtime.Rtkit.LastRxPayload);
  assert(receipt.Rtkit.Running && receipt.Rtkit.CpuReady && receipt.Rtkit.CrashlogReplySent);
  assert(receipt.Rtkit.MailboxValid == 1 && receipt.Rtkit.Inbox == 0x220);
  assert(receipt.Rtkit.Outbox == 0 && receipt.Rtkit.CrashlogCapacity == 0x4000 &&
         receipt.Rtkit.CrashlogRequested == 0x1000 &&
         receipt.Rtkit.CrashlogGpuAddress == runtime.Rtkit.CrashlogGpuAddress);
  assert(receipt.Rtkit.HeadFence == 0x182a && receipt.Rtkit.NextFence == 0x182b &&
         receipt.Rtkit.HeadKind == 2 && receipt.Rtkit.NextKind == 3);
  assert(receipt.Version == 4u); /* Q13: RTKit/pair evidence must be exported. */
  assert(receipt.Bytes == sizeof(receipt));
  assert(receipt.Fence == 0x4f2u && receipt.WaitMs == 2003u);
  assert(receipt.FaultInfo == ADMISSION_GPU_FAULT_INFO_NOT_READ);
  assert(receipt.SgxRead == 0u && receipt.Sgx17620 == 0u && receipt.Sgx17630 == 0u);
  /* Firmware-shared memory is still captured. */
  assert(receipt.RegionCFaultControl == 0xbu);
  assert(receipt.FirmwareStatus[4] == 1u && receipt.FirmwareStatus[12] == 1u);
  assert(receipt.RegionCFaultInfo[5] == 0x205u);
  assert(receipt.PendingStampsActive == 2u);
  assert(receipt.ComputeRptrRead == 1u && receipt.ComputeRptr == 931u);
  assert(receipt.ComputeDone == 930u && receipt.ComputeWrite == 931u);
  assert(receipt.PostEvents == 1u && receipt.LastPostEventKind == 4u);
  assert(barriers > 0);
  puts("POST_RESUME_NO_SGX_OK");
  return 0;
}
'''


class PostResumeNoSgxMmio(unittest.TestCase):
    def test_post_resume_capture_reads_only_shared_memory(self):
        backend = BACKEND.read_text()
        header = (RENDER / 'include/render_admission.h').read_text()
        body = (SHIM +
                define(backend, 'ADMISSION_REGIONC_FAULT_INFO_OFFSET') +
                define(backend, 'ADMISSION_REGIONC_PENDING_STAMPS_OFFSET') +
                define(backend, 'ADMISSION_REGIONC_FAULT_CONTROL_OFFSET') +
                define(header, 'ADMISSION_REGIONC_PENDING_STAMPS_BYTES') +
                define(backend, 'ADMISSION_PLATFORM_SGX_FAULT_INFO_OFFSET') +
                define(backend, 'ADMISSION_PLATFORM_SGX_FAULT_CONTROL_0') +
                define(backend, 'ADMISSION_PLATFORM_SGX_FAULT_CONTROL_1') +
                function_body(backend, 'AdmissionCapturePostResume') + CASE)
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'post_resume.c'
            program.write_text(body)
            binary = Path(tmp) / 'post_resume'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-function', '-ftrivial-auto-var-init=pattern',
                            '-fsanitize=address,undefined',
                            '-I', str(RENDER / 'include'), '-I', str(SHARED / 'include'),
                            str(program), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn('POST_RESUME_NO_SGX_OK', run.stdout)

    def test_sgx_mmio_only_where_the_gpu_is_powered(self):
        owners = sgx_mmio_functions(BACKEND.read_text())
        # The scanner must still see the sanctioned sites.
        self.assertIn('AdmissionFirmwarePowerOn', owners)
        self.assertIn('AdmissionGpuFaultCapture', owners)
        self.assertNotIn('AdmissionCapturePostResume', owners)
        self.assertNotIn('AdmissionPlatformRuntimeQuiesceActive', owners)
        self.assertEqual(set(owners) - SGX_MMIO_OWNERS, set(), owners)


if __name__ == '__main__':
    unittest.main()
