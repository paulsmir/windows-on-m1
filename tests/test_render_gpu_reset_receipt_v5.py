"""Wom1GpuReset v5 (TDR-FIRMWARE-RESTART-RECOVERY.md section 6 stage 1, 10.4).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- A v5 field read at a v4 offset (or padding the compiler adds and the
  decoder does not know) silently misreports the failure class and the
  firmware restart verdict that the H1 preregistration reads; v1-v4 records
  from earlier experiments must keep decoding, and a truncated or mislabelled
  record must be rejected, never guessed.
- The class table: a crashed firmware (F2) must not be reported as F1 because
  a recovery was also pending (EXP2100 against EXP2214), a reset that did not
  fail has no class, and an unmapped status word is not a halt.
- "Not read" is not a value: a CPU_CONTROL or mailbox word that was not read
  must decode as unknown, not as "RUN clear".
- A reset must clear its own class but never the restart record.
"""
import pathlib
import re
import struct
import unittest

from test_render_irq_quiesce import function
from test_wom1_gpu_fault_receipt import DECODER

ROOT = pathlib.Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
BACKEND = (RENDER / "src" / "backend_platform_windows.c").read_text()
HEADER = (RENDER / "include" / "render_gpu_fault.h").read_text()


def run_c(source, includes=(), extra=()):
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "model.c").write_text(source)
        command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined"]
        for include in includes:
            command += ["-I", str(include)]
        command += [str(tmp / "model.c"), *map(str, extra), "-o", str(tmp / "model")]
        subprocess.run(command, check=True)
        return subprocess.run([str(tmp / "model")], check=True, capture_output=True,
                              timeout=60).stdout


RECEIPT = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "render_gpu_fault.h"

int main(int argc, char **argv) {
  ADMISSION_GPU_RESET_RECEIPT reset;
  ADMISSION_GPU_FW_RESTART_RECEIPT *r = &reset.Restart;
  unsigned quiesce, crashed, halted, pending, index;
  /* Layout: the v5 words start where v4 ended, with no padding anywhere. */
  assert(offsetof(ADMISSION_GPU_RESET_RECEIPT, FailureClass) == 792u);
  assert(offsetof(ADMISSION_GPU_RESET_RECEIPT, Restart) == 816u);
  assert(sizeof(ADMISSION_GPU_FW_RESTART_RECEIPT) == 26u * 4u + 2u * 8u + 32u * 4u);
  assert(offsetof(ADMISSION_GPU_FW_RESTART_RECEIPT, EpochBefore) == 104u);
  assert(offsetof(ADMISSION_GPU_FW_RESTART_RECEIPT, FirmwareStatusBefore) == 120u);
  /* v6 (TDR-H0) appends the halt observation after the complete v5 prefix. */
  assert(offsetof(ADMISSION_GPU_RESET_RECEIPT, HaltObserve) == 1064u);
  assert(sizeof(ADMISSION_GPU_TDR_HALT_OBSERVE) == 20u * 4u);
  assert(sizeof(reset) == 1064u + sizeof(ADMISSION_GPU_TDR_HALT_OBSERVE));

  /* Class: only a wait that failed has one; a crash wins over a halt. */
  for (quiesce = 0u; quiesce < 9u; ++quiesce)
    for (crashed = 0u; crashed < 2u; ++crashed)
      for (halted = 0u; halted < 3u; ++halted)
        for (pending = 0u; pending < 2u; ++pending) {
          unsigned word = halted == 2u ? 0xffffffffu : halted;
          unsigned got = AdmissionGpuResetFailureClass(quiesce, crashed, word, pending);
          unsigned failed = quiesce == AdmissionGpuResetQuiesceTimeout ||
              quiesce == AdmissionGpuResetQuiescePoll || quiesce == AdmissionGpuResetQuiesceBackend;
          unsigned want = !failed ? ADMISSION_GPU_RESET_CLASS_NONE
              : crashed ? ADMISSION_GPU_RESET_CLASS_F2
              : (halted == 1u || pending) ? ADMISSION_GPU_RESET_CLASS_F1
              : ADMISSION_GPU_RESET_CLASS_F3; /* an unmapped status word is not a halt */
          assert(got == want);
        }

  memset(&reset, 0, sizeof(reset));
  reset.Version = ADMISSION_GPU_RESET_RECEIPT_VERSION; reset.Bytes = sizeof(reset);
  reset.Resets = 1u; reset.Entry = ADMISSION_GPU_RESET_ADAPTER; reset.Status = 0xc0000483u;
  reset.Quiesce = AdmissionGpuResetQuiescePoll;
  reset.FailureClass = AdmissionGpuResetFailureClass(reset.Quiesce, 1u, 1u, 1u);
  reset.HaltCount = 3u; reset.RtkitCrashed = 1u; reset.MailboxValid = 2u;
  reset.InboxControl = 0x111u; reset.OutboxControl = 0x20101u;
  r->Restarts = 3u; r->Succeeded = 2u; r->Refused = 4u;
  r->RefusedReason = ADMISSION_FW_RESTART_REFUSED_BUSY;
  r->Stage = AdmissionFwRestartStageStartWarm; r->Status = 0xc0000483u;
  r->StopKind = ADMISSION_FW_RESTART_STOP_FORCED;
  r->StopMs = 12u; r->RebuildMs = 7u; r->BootMs = 2200u; r->TotalMs = 2230u;
  r->BackendStopResult = 0u; r->BackendStartResult = 6u;
  r->FirmwarePhase = 2u; r->FirmwareCompletedMask = 3u; r->FirmwareCleanupMask = 0u;
  r->FirmwareLastResult = 5u; r->ProviderState = 7u;
  r->CpuControlAfterStop = 0x100u;
  r->Flags = ADMISSION_FW_RESTART_FLAG_CPU_CONTROL_READ | ADMISSION_FW_RESTART_FLAG_ISR_CLOSED |
      ADMISSION_FW_RESTART_FLAG_ISR_REOPENED | ADMISSION_FW_RESTART_FLAG_FORCED_AFTER_BOOT |
      ADMISSION_FW_RESTART_FLAG_STATUS_BEFORE | ADMISSION_FW_RESTART_FLAG_LINE_LISTED |
      ADMISSION_FW_RESTART_FLAG_OUTBOX_READ;
  r->RetainedCommandMask = (1u << 8) | (1u << 2);
  r->RetainedFlags = 3u;
  r->RtkitMailboxValid = 1u; r->RtkitCrashed = 0u;
  r->InboxControlAfterStop = 0xdeadu; r->OutboxControlAfterStop = 0x20001u;
  r->EpochBefore = 5ull; r->EpochAfter = 6ull;
  for (index = 0u; index < 16u; ++index) {
    r->FirmwareStatusBefore[index] = 0xb00u + index;
    r->FirmwareStatusAfter[index] = 0xa00u + index;
  }
  assert(argc == 1); (void)argv;
  assert(fwrite(&reset, 1, sizeof(reset), stdout) == sizeof(reset));
  return 0;
}
'''



class ReceiptV5(unittest.TestCase):
    def test_v5_layout_class_and_decoder(self):
        data = run_c(RECEIPT, includes=(RENDER / "include",),
                     extra=(RENDER / "src" / "render_gpu_fault.c",))
        self.assertEqual(len(data), DECODER.RESET_V6)
        reset = DECODER.decode_reset(data)
        self.assertEqual((reset['version'], reset['failure_class'], reset['halt_count'],
                          reset['rtkit_crashed']), (6, 'F2', 3, True))
        self.assertEqual(reset['tdr_halt_observe'], {'observe': 'off'})
        self.assertEqual((reset['inbox_control'], reset['outbox_control']), (None, '0x20101'))
        restart = reset['firmware_restart']
        self.assertEqual((restart['restarts'], restart['succeeded'], restart['refused'],
                          restart['refused_reason']), (3, 2, 4, 'busy'))
        self.assertEqual((restart['stage'], restart['status'], restart['stop_kind']),
                         ('start-warm', '0xc0000483', 'forced'))
        self.assertEqual(restart['ms'], {'stop': 12, 'rebuild': 7, 'boot': 2200, 'total': 2230})
        self.assertEqual(restart['flags'], ['cpu_control_read', 'isr_closed', 'isr_reopened',
                                            'forced_after_boot', 'status_before', 'line_listed',
                                            'outbox_read'])
        self.assertEqual((restart['cpu_run_after_stop'], restart['cpu_control_after_stop']),
                         (False, '0x100'))
        # Not read is not a value.
        self.assertIsNone(restart['inbox_control_after_stop'])
        self.assertEqual(restart['outbox_control_after_stop'], '0x20001')
        self.assertEqual(restart['retained_commands'], ['ACTIVATE', 'QUERY_ARENA'])
        self.assertEqual(restart['retained_forbidden_sent'], ['ACTIVATE'])
        self.assertEqual(restart['retained_flags'], {'raw': '0x3', 'prepared': True,
                                                     'active': True, 'prefix_unchanged': False})
        self.assertEqual((restart['epoch_before'], restart['epoch_after'],
                          restart['epoch_unchanged']), (5, 6, False))
        self.assertEqual(restart['firmware_status_before'][4], '0xb04')
        self.assertIsNone(restart['firmware_status_after'])
        self.assertEqual(restart['firmware'], {'phase': 2, 'completed_mask': '0x3',
                                               'cleanup_mask': '0x0', 'last_result': 5})

    def test_v4_records_keep_decoding_and_v5_rejects_truncation(self):
        v5 = bytearray(DECODER.RESET_V5)
        struct.pack_into('<2I', v5, 0, 5, DECODER.RESET_V5)
        decoded = DECODER.decode_reset(v5)
        self.assertEqual(decoded['failure_class'], 'none')
        self.assertIsNone(decoded['firmware_restart']['epoch_unchanged'])  # never reached
        self.assertIsNone(decoded['firmware_restart']['cpu_run_after_stop'])
        v4 = bytearray(792)
        struct.pack_into('<2I', v4, 0, 4, 792)
        old = DECODER.decode_reset(v4)
        self.assertNotIn('failure_class', old)
        self.assertNotIn('firmware_restart', old)
        for length in (792, 816, DECODER.RESET_V5 - 1):
            with self.assertRaises(ValueError):
                DECODER.decode_reset(v5[:length])
        # A v4 header on v5 bytes (or the reverse) is a mismatch, never a guess.
        struct.pack_into('<I', v5, 0, 4)
        with self.assertRaises(ValueError):
            DECODER.decode_reset(v5)
        struct.pack_into('<I', v4, 0, 5)
        with self.assertRaises(ValueError):
            DECODER.decode_reset(v4)
        bad = bytearray(DECODER.RESET_V5)
        struct.pack_into('<2I', bad, 0, 5, DECODER.RESET_V5)
        struct.pack_into('<I', bad, 816 + 19 * 4, 1 << 11)  # an undefined restart flag
        with self.assertRaises(ValueError):
            DECODER.decode_reset(bad)

    def test_decoder_names_follow_the_header(self):
        stages = re.search(r"typedef enum _ADMISSION_FW_RESTART_STAGE \{(.*?)\}", HEADER, re.S).group(1)
        names = re.findall(r"AdmissionFwRestartStage(\w+)", stages)
        self.assertEqual(len(names), len(DECODER.FW_RESTART_STAGES))
        self.assertEqual([re.sub(r"(?<!^)(?=[A-Z])", "-", n).lower() for n in names],
                         list(DECODER.FW_RESTART_STAGES))
        flags = re.findall(r"#define ADMISSION_FW_RESTART_FLAG_(\w+) (0x[0-9a-f]+)u", HEADER)
        self.assertEqual([(name.lower(), int(value, 16)) for name, value in flags],
                         [(name, 1 << bit) for bit, name in enumerate(DECODER.FW_RESTART_FLAGS)])
        refusals = re.findall(r"#define ADMISSION_FW_RESTART_REFUSED_(\w+) (\d+)u", HEADER)
        self.assertEqual([(name.lower().replace('_', '-'), int(value)) for name, value in refusals],
                         list(zip(DECODER.FW_RESTART_REFUSALS[1:], range(1, 7))))
        self.assertEqual(re.search(r"#define ADMISSION_GPU_RESET_RECEIPT_VERSION (\d+)u",
                                   HEADER).group(1), '6')

    def test_reset_entry_clears_the_class_but_not_the_restart_record(self):
        scheduler = (RENDER / "src" / "scheduler_windows.c").read_text()
        recorded = function(scheduler, "AdmissionResetEngineRecorded")
        self.assertIn("receipt->FailureClass = ADMISSION_GPU_RESET_CLASS_NONE;", recorded)
        self.assertNotIn("Restart", re.sub(r"/\*.*?\*/", "", recorded, flags=re.S))
        quiesce = function(BACKEND, "AdmissionPlatformRuntimeQuiesceActive")
        # the worker still owns the provider when it did not leave: no class then
        self.assertRegex(quiesce, r"if \(outcome != AdmissionGpuResetQuiesceWorker\) \{\s*"
                                  r"if \(Receipt != NULL\)\s*AdmissionGpuResetCaptureClass\(")


if __name__ == "__main__":
    unittest.main()
