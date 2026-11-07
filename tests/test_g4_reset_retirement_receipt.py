"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1937's reset receipt could not distinguish an unretired firmware stamp from
an unretired queue consumer. Capture must preserve that distinction without
mutating the runtime or its shared words, inventing zero for unreadable data,
or reading queues that do not participate in the submission.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_wom1_gpu_fault_receipt import DECODER

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class ResetRetirementReceipt(unittest.TestCase):
    def test_v4_unknown_and_legacy_versions(self):
        data = bytearray(792)
        struct.pack_into('<2I', data, 0, 4, 792)
        result = DECODER.decode_reset(data)
        self.assertEqual(result['retirement_before'], {'captured': False})
        self.assertEqual(result['retirement_after'], {'captured': False})
        for version, size in ((1, 96), (2, 496), (3, 536)):
            old = bytearray(size)
            struct.pack_into('<2I', old, 0, version, size)
            self.assertNotIn('retirement_before', DECODER.decode_reset(old))
        for length in (536, 788, 791):
            with self.assertRaises(ValueError):
                DECODER.decode_reset(data[:length])
        struct.pack_into('<I', data, 0, 3)
        with self.assertRaises(ValueError):
            DECODER.decode_reset(data)

    def test_real_capture_is_read_only_and_decodes_exact_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = tmp / 'capture.c'
            source.write_text(PROGRAM)
            binary = tmp / 'capture'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(RENDER / 'include'),
                            '-I', str(SHARED / 'include'), '-I', str(SHARED / 'tests'),
                            str(source), str(SHARED / 'src/apple_agx_g13_queue_runtime.c'),
                            str(SHARED / 'src/apple_agx_g13_codec.c'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True)
        reset = DECODER.decode_reset(result.stdout)
        before, after = reset['retirement_before'], reset['retirement_after']
        self.assertEqual((before['fence'], before['recovery_pending']), ('0x65', True))
        self.assertEqual(set(before['queues']), {'ta', '3d', 'compute'})
        self.assertEqual(before['queues']['compute']['stamp'], '0xffffff00')
        self.assertEqual(before['queues']['compute']['expected_stamp'], '0xffffff00')
        self.assertEqual(before['queues']['compute']['done'], 0)
        self.assertEqual(before['queues']['compute']['write'], 1)
        self.assertEqual(after['queues']['compute']['done'], 1)
        for queue in after['queues'].values():
            self.assertIsNone(queue['stamp'])
            self.assertIsNotNone(queue['done'])
            self.assertEqual(queue['read_mask'], 6)


PROGRAM = r'''
#include "apple_agx_g13_queue_runtime_test.c"
#undef assert
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "render_gpu_recovery_snapshot.h"
static unsigned reads;
static unsigned fail_stamp;
static APPLE_AGX_BACKEND_BOOL CaptureRead(void *ctx,
    const volatile APPLE_AGX_BACKEND_U32 *p, APPLE_AGX_BACKEND_U32 *v) {
  TEST_FIXTURE *f = ctx;
  ++reads;
  if (fail_stamp && (p == &f->TaStamp || p == &f->D3Stamp || p == &f->ComputeStamp))
    return APPLE_AGX_BACKEND_FALSE;
  *v = *p;
  return APPLE_AGX_BACKEND_TRUE;
}
int main(void) {
  TEST_FIXTURE f, saved;
  ADMISSION_GPU_RESET_RECEIPT receipt = {0};
  ADMISSION_GPU_RECOVERY_SNAPSHOT snapshot;
  unsigned mode;
  for (mode=0; mode<3; ++mode) {
    APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s;
    TestInitialize(&f); TestEnableCompute(&f);
    s=TestSubmission(&f,101u);
    s.Compute.Source=f.ComputeSource; s.Compute.Destination=f.ComputeDestination;
    s.Compute.Bytes=sizeof(f.ComputeSource); s.Compute.GpuAddresses[0]=0x1500300000ULL;
    s.Compute.GpuAddressCount=1u; s.Compute.ExpectedStamp=0xffffff00u;
    if (mode==0) assert(AppleAgxG13QueueRuntimeSubmit(&f.Runtime,&s)==0);
    else if (mode==1) {
      s.ComputeOnly=1;
      assert(AppleAgxG13QueueRuntimeSubmitCompute(&f.Runtime,&s)==0);
    } else {
      s.Ta.GpuAddresses[2]=s.Ta.GpuAddresses[1];
      s.Ta.GpuAddresses[1]=s.Ta.GpuAddresses[0];
      s.Ta.GpuAddresses[0]=0x1500308800ULL;
      s.Ta.GpuAddressCount=3; s.TaLeadingBarrier=1;
      assert(AppleAgxG13QueueRuntimeSubmitRenderWithCompute(&f.Runtime,&s)==0);
    }
    f.Runtime.RecoveryPending=1;
    f.Runtime.Io.ReadU32=CaptureRead; f.Runtime.Io.Context=&f;
    f.TaStamp=s.Ta.ExpectedStamp; f.D3Stamp=s.D3.ExpectedStamp;
    f.ComputeStamp=0xffffff00u;
    saved=f; reads=0;
    AdmissionGpuRecoverySnapshot(&f.Runtime,&snapshot);
    assert(!memcmp(&f,&saved,sizeof(f)));
    assert(snapshot.Captured==1 && snapshot.Fence==101u && snapshot.RecoveryPending);
    assert(snapshot.QueueMask==(mode==0 ? 3u : mode==1 ? 4u : 7u));
    assert(reads==(mode==0 ? 6u : mode==1 ? 3u : 9u));
    if (mode==2) receipt.RetirementBefore=snapshot;
    /* Successful stamp read equal to zero is still known, not unreadable. */
    f.TaStamp=0; f.D3Stamp=0; f.ComputeStamp=0;
    AdmissionGpuRecoverySnapshot(&f.Runtime,&snapshot);
    assert(snapshot.Queues[mode==1 ? 2 : 0].ReadMask==7u);
    assert(snapshot.Queues[mode==1 ? 2 : 0].Stamp==0u);
    /* A failed read must not prevent independent consumer/write reads. */
    fail_stamp=1; f.ComputeDone=f.ComputeWrite;
    saved=f; reads=0;
    AdmissionGpuRecoverySnapshot(&f.Runtime,&snapshot);
    assert(!memcmp(&f,&saved,sizeof(f)));
    assert(snapshot.Queues[mode==1 ? 2 : 0].ReadMask==6u);
    if (mode==2) receipt.RetirementAfter=snapshot;
    fail_stamp=0;
    /* An idle runtime must not expose stale queue membership or read old addresses. */
    f.Runtime.Phase=AppleAgxG13QueueRuntimeReady; reads=0;
    AdmissionGpuRecoverySnapshot(&f.Runtime,&snapshot);
    assert(snapshot.Captured && snapshot.QueueMask==0 && reads==0);
    /* An unavailable callback/pointer is unknown; it must not be dereferenced. */
    f.Runtime.Phase=AppleAgxG13QueueRuntimeSubmitted; f.Runtime.Io.ReadU32=0;
    AdmissionGpuRecoverySnapshot(&f.Runtime,&snapshot);
    assert(snapshot.Queues[0].ReadMask==0u && reads==0);
    f.Runtime.Io.ReadU32=CaptureRead;
    f.Runtime.Config.Compute.Stamp=0;
    AdmissionGpuRecoverySnapshot(&f.Runtime,&snapshot);
    if (mode) assert(snapshot.Queues[2].ReadMask==6u);
  }
  receipt.Version=ADMISSION_GPU_RESET_RECEIPT_VERSION; receipt.Bytes=sizeof(receipt);
  assert(sizeof(receipt)==792);
  assert(fwrite(&receipt,1,sizeof(receipt),stdout)==sizeof(receipt));
  return 0;
}
'''
