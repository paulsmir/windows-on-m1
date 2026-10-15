"""Wom1GpuFault/Wom1GpuReset bookkeeping and their offline decoder.

WHAT REAL INVARIANT: in a fault storm (EXP1296: 2435 firmware Timeout
recoveries) the receipt must keep the first event, the first event with a
valid FAULT_INFO and the last one, and count events the firmware did not halt
for; the decoder must read exactly the KMD's struct layout (a drift would
mis-attribute hardware evidence) and the FAULT_INFO fields as Asahi regs.rs.
The real render_gpu_fault.c fills the receipts; the decoder reads the bytes.
"""
from pathlib import Path
import importlib.util
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SPEC = importlib.util.spec_from_file_location(
    'decode_wom1_gpu_fault', ROOT / 'tools/decode_wom1_gpu_fault.py')
DECODER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DECODER)

PROGRAM = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "render_gpu_fault.h"

static void dump(const char *dir, const char *name, const void *data, size_t bytes) {
  char path[1024];
  FILE *file;
  snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
  file = fopen(path, "wb");
  assert(file && fwrite(data, 1, bytes, file) == bytes);
  fclose(file);
}

static ADMISSION_GPU_FAULT_EVENT event(unsigned fence, unsigned kind, unsigned flags,
                                       unsigned long long info) {
  ADMISSION_GPU_FAULT_EVENT e;
  memset(&e, 0, sizeof(e));
  e.Fence = fence; e.EventKind = kind; e.Flags = flags; e.FaultInfo = info;
  e.ProcessId = 1232u; e.SubmitToEventMs = 1u; e.HaltCount = fence & 0xffu;
  e.ContextToken = 0xffff8d02c0000000ull + fence;
  return e;
}

int main(int argc, char **argv) {
  ADMISSION_GPU_FAULT_RECEIPT receipt;
  ADMISSION_GPU_RESET_RECEIPT reset;
  ADMISSION_GPU_FAULT_EVENT e;
  unsigned index;
  assert(argc == 2);
  printf("%zu %zu %zu %zu\n", sizeof(ADMISSION_GPU_FAULT_EVENT),
         sizeof(ADMISSION_GPU_FAULT_RECEIPT), offsetof(ADMISSION_GPU_FAULT_RECEIPT, First),
         sizeof(ADMISSION_GPU_RESET_RECEIPT));
  AdmissionGpuFaultReceiptInitialize(&receipt);
  /* A Timeout without a halt (no resume), info not read. */
  e = event(0x100u, 4u, ADMISSION_GPU_FAULT_JOB_KNOWN, ADMISSION_GPU_FAULT_INFO_NOT_READ);
  AdmissionGpuFaultReceiptRecord(&receipt, &e);
  assert(e.Sequence == 1u);
  /* A storm of resumed Timeouts with an invalid FAULT_INFO read. */
  for (index = 0u; index < 2433u; ++index) {
    e = event(0x101u + index, 4u, ADMISSION_GPU_FAULT_RESUMED | ADMISSION_GPU_FAULT_INFO_READ |
              ADMISSION_GPU_FAULT_JOB_KNOWN, 0ull);
    AdmissionGpuFaultReceiptRecord(&receipt, &e);
  }
  /* A Fault with a valid FAULT_INFO: unmapped write, unit 0x08 VDM(0),
   * VM slot 2, address 0x1500fa0040 >> 6 << 30. */
  e = event(0xce71cu, 0u, ADMISSION_GPU_FAULT_RESUMED | ADMISSION_GPU_FAULT_INFO_READ |
            ADMISSION_GPU_FAULT_INFO_VALID | ADMISSION_GPU_FAULT_JOB_KNOWN,
            ((0x1500fa0040ull >> 6) << 30) | (2ull << 17) | (0x08ull << 9) | (1ull << 7) | 1ull);
  AdmissionGpuFaultReceiptRecord(&receipt, &e);
  assert(receipt.Events == 2435u && receipt.Resumed == 2434u && receipt.NotHalted == 1u &&
         receipt.ValidFaults == 1u && receipt.First.Fence == 0x100u &&
         receipt.FirstValid.Sequence == 2435u && receipt.Last.Fence == 0xce71cu);
  dump(argv[1], "Wom1GpuFault", &receipt, sizeof(receipt));

  memset(&reset, 0, sizeof(reset));
  reset.Version = ADMISSION_GPU_RESET_RECEIPT_VERSION; reset.Bytes = sizeof(reset);
  reset.Resets = 2u; reset.Succeeded = 1u; reset.Entry = ADMISSION_GPU_RESET_ADAPTER;
  reset.Status = 0u; reset.SchedulerFaulted = 0x40000u | 3930u; reset.PacketState = 3u;
  reset.PacketFence = reset.ActiveFence = 0xce71cu; reset.CompletedFence = 0xce71bu;
  reset.LastSubmittedFence = 0xce71cu; reset.PreemptionPhase = 1u;
  reset.LastAbortedFence = 0xce71cu; reset.Quiesce = AdmissionGpuResetQuiesceEnded;
  reset.WaitMs = 2u; reset.BackendPhase = 3u; reset.ProviderPhase = 4u;
  reset.LastPollGuard = 1u; reset.WorkerBusy = 0u; reset.Halted = 1u;
  reset.FirmwareRecoveries = 2436u; reset.ContextToken = 0xffff8d02cced4000ull;
  dump(argv[1], "Wom1GpuReset", &reset, sizeof(reset));
  return 0;
}
'''

# EXP1296 minidump, dxgkrnl blackbox {270A33FD-...} section 1 (29 bytes:
# trailing zeros dropped): the KMD's CollectDbgInfo version 1.
EXP1296_DBGINFO = '01000000200000001be70c001ce70c001ce70c00010000001400000001'


class Wom1GpuFaultReceipt(unittest.TestCase):
    def test_storm_receipt_and_reset_receipt_decode_from_kmd_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'receipt.c').write_text(PROGRAM)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I',
                            str(RENDER / 'include'), str(tmp / 'receipt.c'),
                            str(RENDER / 'src/render_gpu_fault.c'), '-o',
                            str(tmp / 'receipt')], check=True)
            sizes = subprocess.run([str(tmp / 'receipt'), str(tmp)], check=True,
                                   capture_output=True, text=True).stdout.split()
            self.assertEqual([int(v) for v in sizes],
                             [DECODER.EVENT.size, DECODER.FAULT_HEADER.size + 3 * DECODER.EVENT.size,
                              DECODER.FAULT_HEADER.size, DECODER.RESET.size])
            fault = DECODER.decode_fault((tmp / 'Wom1GpuFault.bin').read_bytes())
            reset = DECODER.decode_reset((tmp / 'Wom1GpuReset.bin').read_bytes())

        self.assertEqual((fault['events'], fault['resumed'], fault['not_halted'],
                          fault['valid_faults']), (2435, 2434, 1, 1))
        first = fault['first']
        self.assertEqual((first['sequence'], first['event'], first['fence'], first['flags']),
                         (1, 'timeout', '0x100', ['job_known']))
        self.assertEqual(first['fault_info'], {'read': False})
        self.assertEqual(fault['first_valid'], fault['last'])
        last = fault['last']
        self.assertEqual((last['sequence'], last['event'], last['fence'], last['process_id']),
                         (2435, 'fault', '0xce71c', 1232))
        self.assertEqual(last['flags'], ['resumed', 'info_read', 'info_valid', 'job_known'])
        info = last['fault_info']
        self.assertEqual((info['valid'], info['reason'], info['access'], info['level'],
                          info['unit'], info['vm_slot'], info['address']),
                         (True, 'unmapped', 'write', 1, 'VDM(0)', 2, '0x1500fa0040'))

        self.assertEqual((reset['entry'], reset['status'], reset['quiesce'], reset['wait_ms']),
                         ('DxgkDdiResetFromTimeout', '0x00000000', 'ended', 2))
        self.assertEqual(reset['scheduler_faulted_at_entry'],
                         {'raw': '0x40f5a', 'file': 'backend_platform_windows.c', 'line': 3930})
        self.assertEqual((reset['packet']['state'], reset['fences']['last_aborted'],
                          reset['preemption'], reset['firmware_recoveries']),
                         ('active', '0xce71c', 'wait-current-boundary', 2436))

    def test_exp1296_minidump_debug_snapshot(self):
        snapshot = DECODER.decode_dbginfo(bytes.fromhex(EXP1296_DBGINFO))
        self.assertEqual(snapshot, {
            'version': 1, 'completed': '0xce71b', 'last_submitted': '0xce71c',
            'active': '0xce71c', 'preemption': 'wait-current-boundary', 'contexts': 20,
            'faulted': True, 'paging_pending': False})

    def test_debug_snapshot_v2_matches_the_kmd_struct(self):
        scheduler = (RENDER / 'src/scheduler_windows.c').read_text()
        struct = re.search(r'typedef struct _ADMISSION_SCHEDULER_DEBUG_SNAPSHOT \{.*?\} '
                           r'ADMISSION_SCHEDULER_DEBUG_SNAPSHOT;', scheduler, re.S).group(0)
        program = ('#include <stdio.h>\n#include <string.h>\ntypedef unsigned int ULONG;\n' +
                   struct + r'''
int main(int argc, char **argv) {
  ADMISSION_SCHEDULER_DEBUG_SNAPSHOT s;
  FILE *f;
  if (argc != 2) return 1;
  memset(&s, 0, sizeof(s));
  s.Version = 2u; s.Size = sizeof(s); s.CompletedFence = 0xce71bu;
  s.LastSubmittedFence = s.ActiveFence = 0xce71cu; s.PreemptionPhase = 1u;
  s.ContextCount = 20u; s.Flags = 1u; s.SchedulerFaultedCode = 0x40000u | 3930u;
  s.RenderPacketState = 3u; s.RenderPacketFence = 0xce71cu;
  s.PlatformState = 3u | (4u << 8) | (1u << 16) | (1u << 25); s.Resets = 1u;
  f = fopen(argv[1], "wb");
  if (!f || fwrite(&s, sizeof(s), 1, f) != 1) return 1;
  fclose(f);
  return 0;
}
''')
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'dbg.c').write_text(program)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', str(tmp / 'dbg.c'), '-o', str(tmp / 'dbg')], check=True)
            subprocess.run([str(tmp / 'dbg'), str(tmp / 'dbg.bin')], check=True)
            data = (tmp / 'dbg.bin').read_bytes()
        self.assertEqual(len(data), DECODER.DBGINFO_V2)
        snapshot = DECODER.decode_dbginfo(data.rstrip(b'\0'))
        self.assertEqual(snapshot['scheduler_faulted'],
                         {'raw': '0x40f5a', 'file': 'backend_platform_windows.c', 'line': 3930})
        self.assertEqual(snapshot['packet'], {'state': 'active', 'fence': '0xce71c'})
        self.assertEqual(snapshot['platform'], {
            'backend_phase': 3, 'provider_phase': 4, 'last_poll_guard': 1,
            'worker_scheduled': False, 'resetting': True, 'stopping': False})
        self.assertEqual((snapshot['resets'], snapshot['resets_succeeded']), (1, 0))

    def test_enumtag_extraction_finds_the_driver_section(self):
        # EXP1296 watchdog secondary data: header dwords, (offset, id) pairs,
        # section 1 = the KMD buffer, followed by section 2 (driver version).
        table = [0x27, 0x53a5, 0x140, 0xffff, 0xbec, 0x11, 0x4ba9, 0x18, 0x53a5, 0x01,
                 0x53c2, 0x02, 0x5401, 0x06, 0, 0]
        blob = bytearray(0x5410)
        blob[:4 * len(table)] = b''.join(v.to_bytes(4, 'little') for v in table)
        blob[0x53a5:0x53c2] = bytes.fromhex(EXP1296_DBGINFO)
        blob[0x53c2:0x53d0] = b'Apple Silicon '
        lines = ['{65755A40-F146-43EA-8C9136B85728FD35} - 0x0 bytes',
                 '{270A33FD-3DA6-460D-BA893C1BAE21E39B} - 0x5410 bytes']
        for at in range(0, len(blob), 16):
            chunk = blob[at:at + 16]
            lines.append('  ' + ' '.join(f'{b:02X}' for b in chunk) + '  ' + '.' * len(chunk))
        lines.append('{F87E4A4C-C5A1-4D2F-BFF0D5DE63A5E4C3} - 0x10 bytes')
        lines.append('  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................')
        data = DECODER.read_enumtag('\n'.join(lines))
        self.assertEqual(len(data), len(blob))
        section = DECODER.dbginfo_from_blackbox(data)
        self.assertEqual(section, bytes.fromhex(EXP1296_DBGINFO))
        self.assertEqual(DECODER.decode_dbginfo(section)['active'], '0xce71c')
        with self.assertRaises(ValueError):
            DECODER.read_enumtag('{F87E4A4C-C5A1-4D2F-BFF0D5DE63A5E4C3} - 0x0 bytes')

    def test_fault_info_layout_matches_asahi_g13(self):
        self.assertEqual(DECODER.decode_fault_info(DECODER.FAULT_INFO_NOT_READ), {'read': False})
        self.assertEqual(DECODER.decode_fault_info(0)['valid'], False)
        info = DECODER.decode_fault_info((7 << 1) | (1 << 4) | (0xa1 << 9) | 1)
        self.assertEqual((info['reason'], info['access'], info['unit']),
                         ('unknown(7)', 'read', 'dPM'))
        self.assertEqual(DECODER.fault_unit(0xe6), 'gCDM_CS_KS0_SP(0)')
        self.assertEqual(DECODER.fault_unit(0xb3), 'GL2CC_META(3)')
        self.assertEqual(DECODER.fault_unit(0x0f), 'unknown(0xf)')


if __name__ == '__main__':
    unittest.main()
