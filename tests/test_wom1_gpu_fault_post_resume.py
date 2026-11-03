"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
rc77: Wom1GpuFaultPostResume is the only evidence of what the firmware did
after it was resumed from an MMU fault (EXP2005/EXP2007 left no such record).
The KMD struct (render_gpu_fault.h, C_ASSERT 312 bytes) and the offline
decoder must agree field by field; a silent offset drift would turn the next
hardware run's receipt into wrong conclusions (for example a halted firmware
read as running, or a RecoverChannel reported as never sent).
"""
from pathlib import Path
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
sys.path.insert(0, str(ROOT / 'tools'))
import decode_wom1_gpu_fault as decoder  # noqa: E402

PROGRAM = r'''
#include <stdio.h>
#include <string.h>
#include "render_gpu_fault.h"
int main(void) {
  ADMISSION_GPU_POST_RESUME_RECEIPT r;
  unsigned i;
  memset(&r, 0, sizeof(r));
  if (sizeof(r) != 312) return 1;
  r.Version = ADMISSION_GPU_POST_RESUME_VERSION; r.Bytes = sizeof(r);
  r.Fence = 0x16160u; r.WaitMs = 1001u; r.RegionCFaultControl = 0xbu;
  r.SgxRead = 1u; r.Sgx17620 = 0x11u; r.Sgx17630 = 0x22u;
  r.FaultInfo = 0x000001001303cc11ULL;
  for (i = 0; i < 16; ++i) r.FirmwareStatus[i] = 0x100u + i;
  r.FirmwareStatus[4] = 2u; r.FirmwareStatus[8] = 1u; r.FirmwareStatus[12] = 0u;
  r.PendingStampsActive = 3u;
  for (i = 0; i < 6; ++i) r.RegionCFaultInfo[i] = 0x200u + i;
  r.ComputeRptrRead = 1u; r.ComputeRptr = 931u; r.ComputeDone = 930u; r.ComputeWrite = 931u;
  r.PostEvents = 2u; r.PostEventKindMask = (1u << 4) | (1u << 8); r.LastPostEventKind = 8u;
  r.ForceState = ADMISSION_GPU_POST_RESUME_FORCE_SENT; r.ForceResult = 0u;
  r.ForceMask = 4u; r.ForceAtMs = 100u;
  r.PollFailure = (ADMISSION_GPU_POST_RESUME_POLL_PROVIDER << 24) | (0xffu << 16) | (3u << 8) | 2u;
  r.Rtkit.Captured = 1; r.Rtkit.Running = r.Rtkit.CpuReady = 1;
  r.Rtkit.CrashlogReplySent = r.Rtkit.Crashed = 1;
  r.Rtkit.ReceivedCount = 73; r.Rtkit.LastEndpoint = 1;
  r.Rtkit.LastPayload = 1ULL << 52; r.Rtkit.MailboxValid = 1;
  r.Rtkit.Inbox = 0x220; r.Rtkit.CrashlogGpuAddress = 0xa0001234000ULL;
  r.Rtkit.CrashlogCapacity = 0x4000; r.Rtkit.CrashlogRequested = 0x1000;
  r.Rtkit.HeadFence = 0x182a; r.Rtkit.NextFence = 0x182b;
  r.Rtkit.HeadKind = 2; r.Rtkit.NextKind = 3;
  for(i=0;i<6;++i) {r.ExpectedStamps[i]=0x182a+i;r.EventSlots[i]=i%3;}
  fwrite(&r, sizeof(r), 1, stdout);
  return 0;
}
'''


class PostResumeReceipt(unittest.TestCase):
    def test_kmd_layout_matches_decoder(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'layout.c'
            source.write_text(PROGRAM)
            binary = Path(tmp) / 'layout'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-I', str(RENDER / 'include'), str(source),
                            '-o', str(binary)], check=True)
            raw = subprocess.run([str(binary)], check=True, capture_output=True).stdout
            path = Path(tmp) / 'Wom1GpuFaultPostResume.bin'
            path.write_bytes(raw)
            decoded = decoder.DECODERS[decoder.POST_RESUME_NAME](raw)
            self.assertEqual(decoder.read_receipts(path), {decoder.POST_RESUME_NAME: raw})
        self.assertEqual(decoded['fence'], '0x16160')
        self.assertEqual(decoded['wait_ms'], 1001)
        self.assertEqual(decoded['regionc_fault_control'], '0xb')
        self.assertEqual(decoded['sgx_fault_control'], {'0x17620': '0x11', '0x17630': '0x22'})
        self.assertEqual(decoded['fault_info']['address'], '0x10000')
        self.assertEqual(decoded['fault_info']['unit'], 'gCDM_CS_KS0_SP(0)')
        self.assertEqual((decoded['firmware_status']['halt_count'],
                          decoded['firmware_status']['halted'],
                          decoded['firmware_status']['resume']), (2, 1, 0))
        self.assertEqual(decoded['pending_stamps_active'], 3)
        self.assertEqual(decoded['regionc_fault_info'][5], '0x205')
        self.assertEqual(decoded['compute'], {'rptr': 931, 'done': 930, 'write': 931})
        self.assertEqual(decoded['events_after_resume'],
                         {'count': 2, 'kinds': ['timeout', 'channel_error'],
                          'last': 'channel_error'})
        self.assertEqual(decoded['recover_channel'],
                         {'state': 'sent', 'result': 'ok', 'pipes': ['compute'], 'at_ms': 100})
        self.assertEqual(decoded['version'], 4)
        for i, name in enumerate(('head_ta','head_3d','head_compute','next_ta','next_3d','next_compute')):
            self.assertEqual(decoded['queue_stamps'][name], {'expected':0x182a+i,'event':i%3})
        self.assertEqual(decoded['rtkit'], {
            'running': True, 'cpu_ready': True, 'crashlog_reply_sent': True,
            'crashed': True, 'received_count': 73, 'last_endpoint': 1,
            'last_payload': '0x10000000000000', 'inbox': '0x220', 'outbox': None,
            'crashlog_gpu_address': '0xa0001234000', 'crashlog_capacity': 0x4000,
            'crashlog_requested': 0x1000, 'head_fence': '0x182a',
            'next_fence': '0x182b', 'head_kind': 'render_prelude', 'next_kind': 'compute'})
        self.assertEqual(decoded['poll_failure'],
                         {'source': 'provider_poll', 'poll_guard': 'drain_events',
                          'drain_guard': 'flush_state', 'ingest_guard': 0xff})
        json.dumps(decoded)
        for version in (1, 2):
            legacy = bytearray(raw[:184])
            legacy[:8] = version.to_bytes(4, 'little') + (184).to_bytes(4, 'little')
            self.assertIsNone(decoder.decode_post_resume(legacy)['rtkit'])
        legacy = bytearray(raw[:264])
        legacy[:8] = (3).to_bytes(4, 'little') + (264).to_bytes(4, 'little')
        self.assertEqual(decoder.decode_post_resume(legacy)['rtkit'], decoded['rtkit'])
        self.assertIsNone(decoder.decode_post_resume(legacy)['queue_stamps'])
        missing = bytearray(raw); missing[184:188] = bytes(4)
        self.assertIsNone(decoder.decode_post_resume(missing)['rtkit'])
        for bad in (raw[:-1], raw + bytes(8), raw[:184], bytes(264)):
            with self.assertRaises(ValueError):
                decoder.decode_post_resume(bad)

    def test_v1_receipt_has_no_poll_failure(self):
        # rc92: receipts written before v2 kept the word reserved (0).
        raw = bytearray(184)
        raw[0:8] = (1).to_bytes(4, 'little') + (184).to_bytes(4, 'little')
        raw[32:40] = decoder.FAULT_INFO_NOT_READ.to_bytes(8, 'little')
        decoded = decoder.DECODERS[decoder.POST_RESUME_NAME](bytes(raw))
        self.assertIsNone(decoded['poll_failure'])
        self.assertEqual(decoder.decode_poll_failure(
            (1 << 24) | 8), {'source': 'rtkit_drain', 'result': 'firmware_crashed'})

    def test_drain_guard_names_match_the_provider_enum(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A drain guard added
        # or renumbered in the provider must not be decoded under a stale name
        # (rc97: EXP2030's guard 10 was the composer's ApplyEvent refusal).
        header = (ROOT / 'drivers/apple-agx/shared/include/'
                  'apple_agx_platform_provider.h').read_text()
        enum = {int(value): re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower()
                for name, value in re.findall(
                    r'AppleAgxPlatformDrainGuard(\w+) = (\d+)u', header)}
        self.assertEqual(decoder.DRAIN_GUARDS, enum)
        self.assertEqual(decoder.decode_poll_failure(0x2000a02),
                         {'source': 'provider_poll', 'poll_guard': 'drain_events',
                          'drain_guard': 'apply_event', 'ingest_guard': 0})


if __name__ == '__main__':
    unittest.main()
