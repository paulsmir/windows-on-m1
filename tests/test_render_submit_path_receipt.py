"""Wom1SubmitPath1135 v2: the layout the KMD writes and the decoder reads.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The receipt decides whether the "in time, never prebound" submit miss is the
context two-fence limit lagging the dxgkrnl notify (R9 A.6). A decoder that
reads a v2 field at a v1 offset, a v1 field that moved, or a reported-fence
marker that outlives its retire would give a confident wrong answer.
"""
import importlib.util
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


def tool():
    spec = importlib.util.spec_from_file_location('agx_submit_path', ROOT / 'tools/agx_submit_path.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SubmitPathReceipt(unittest.TestCase):
    def test_layout_matches_the_structure(self):
        """The structure as declared, compiled for the Windows ABIs."""
        header = (RENDER / 'include/render_admission.h').read_text()
        body = header[header.index('typedef struct _ADMISSION_SUBMIT_PATH {'):]
        body = body[:body.index('} ADMISSION_SUBMIT_PATH;') + len('} ADMISSION_SUBMIT_PATH;')]
        code = ('#include "render_submit_path.h"\n'
                'typedef unsigned long ULONG; typedef long LONG; typedef long long LONG64;\n' + body + '\n'
                '_Static_assert(sizeof(ADMISSION_SUBMIT_PATH) == 112, "size");\n'
                '_Static_assert(__builtin_offsetof(ADMISSION_SUBMIT_PATH, Waits) == 24, "v1 waits");\n'
                '_Static_assert(__builtin_offsetof(ADMISSION_SUBMIT_PATH, WaitQpc) == 40, "v1 qpc");\n'
                '_Static_assert(__builtin_offsetof(ADMISSION_SUBMIT_PATH, MaxWaitQpc) == 72, "v1 max");\n'
                '_Static_assert(__builtin_offsetof(ADMISSION_SUBMIT_PATH, NotAdmissible) == 80, "v2");\n'
                '_Static_assert(__builtin_offsetof(ADMISSION_SUBMIT_PATH, ContextLimitReported) == 104, "r");\n'
                '_Static_assert(__builtin_offsetof(ADMISSION_SUBMIT_PATH, Requeued) == 108, "q");\n'
                '_Static_assert(ADMISSION_SUBMIT_PATH_VERSION == 2u && ADMISSION_SUBMIT_NOT_REASONS == 6u, "v");\n')
        with tempfile.TemporaryDirectory(prefix='submit-path-') as tmp:
            src = Path(tmp) / 'abi.c'
            src.write_text(code)
            for target in ('x86_64-pc-windows-msvc', 'aarch64-pc-windows-msvc'):
                done = subprocess.run([os.environ.get('CC', 'clang'), '-target', target, '-std=c11',
                                       '-Werror', '-fsyntax-only', '-I', str(RENDER / 'include'), str(src)],
                                      capture_output=True, text=True)
                self.assertEqual(done.returncode, 0, done.stderr)
        receipts = (RENDER / 'src/receipts.c').read_text()
        self.assertIn('Context->SubmitPath.Version = ADMISSION_SUBMIT_PATH_VERSION;', receipts)

    def test_decoder_reads_v1_and_v2(self):
        t = tool()
        v1 = struct.pack('<IIiiii4i4qq', 1, 80, 6187, 29392, 29000, 0, 29, 1840, 0, 1,
                         1000, 80000000, 0, 5, 240000)
        out = t.decode(v1)
        self.assertEqual((out['immediate'], out['deferred'], out['waits']), (6187, 29392, [29, 1840, 0, 1]))
        self.assertNotIn('not_admissible', out)
        v2 = struct.pack('<IIiiii4i4qq6i2i', 2, 112, 7000, 30000, 29900, 0, 30, 1900, 0, 1,
                         1100, 84000000, 0, 5, 240000, 0, 31, 1850, 2, 0, 0, 1800, 40)
        new = t.decode(v2)
        self.assertEqual(new['not_admissible'], {'irql': 0, 'not queueable': 31, 'context limit': 1850,
                                                 'pending capacity': 2, 'idle': 0, 'faulted': 0})
        self.assertEqual((new['context_limit_reported'], new['requeued']), (1800, 40))
        d = t.delta(new, out, 24000000.0)
        self.assertEqual(d['waits']['worker finishing'], 60)
        self.assertEqual(d['mean_wait_us']['worker finishing'], round(4000000 * 1e6 / 24000000.0 / 60))
        self.assertNotIn('not_admissible', d)  # no v2 base to subtract from
        self.assertEqual(t.delta(new, new, 24000000.0)['not_admissible']['context limit'], 0)
        with self.assertRaises(ValueError):
            t.decode(v2[:100])
        with self.assertRaises(ValueError):
            t.decode(struct.pack('<II', 3, 80) + bytes(72))
        with tempfile.TemporaryDirectory(prefix='submit-path-') as tmp:
            snapshot = Path(tmp) / 'kmd-receipts.txt'
            snapshot.write_text('Wom1Other 00\nWom1SubmitPath1135 ' + v2.hex() + '\n')
            self.assertEqual(t.decode(t.load(str(snapshot))), new)
            self.assertEqual(t.decode(t.load(v2.hex())), new)

    def test_reported_fence_marker_is_bounded_by_notify_and_retire(self):
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        self.assertEqual(backend.count('SubmitReportedFence'), 2)
        notify = backend.index('AdmissionNotifyCompletionAtInterrupt, &notification, 0u,')
        refused = backend.index('if (!NT_SUCCESS(sync_status) || !reported)', notify)
        mark = backend.index('InterlockedExchange(&adapter->SubmitReportedFence, (LONG)Fence);')
        retire = backend.index('AdmissionContextRetireFence(runtime->CompletionContext, Fence);')
        clear = backend.index('InterlockedExchange(&adapter->SubmitReportedFence, 0);')
        unlock = backend.index('KeReleaseSpinLock(&adapter->SchedulerLock,old_irql);', retire)
        self.assertLess(refused, mark)   # only a fence dxgkrnl was told about
        self.assertLess(mark, retire)
        self.assertLess(retire, clear)
        self.assertLess(clear, unlock)   # cleared under SchedulerLock, with the retire
        g3 = (RENDER / 'src/gpuva_g3_windows.c').read_text()
        why = g3[g3.index('static BOOLEAN AdmissionG4PendingAdmissibleWhy('):]
        why = why[:why.index('\n}\n')]
        lock = why.index('KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);')
        read = why.index('InterlockedCompareExchange(&adapter->SubmitReportedFence, 0, 0)')
        self.assertLess(lock, read)
        self.assertLess(read, why.index('KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);'))
        # code order of the predicates
        order = [why.index(name) for name in (
            'ADMISSION_SUBMIT_NOT_IRQL;', 'ADMISSION_SUBMIT_NOT_QUEUEABLE;',
            'ADMISSION_SUBMIT_NOT_CONTEXT_LIMIT;', 'ADMISSION_SUBMIT_NOT_CAPACITY;',
            'ADMISSION_SUBMIT_NOT_IDLE;', 'ADMISSION_SUBMIT_NOT_FAULTED;')]
        self.assertEqual(order, sorted(order))
        # the decision counts; the predicate itself has no side effect
        self.assertNotIn('SubmitPath', why)
        self.assertEqual(g3.count('SubmitPath.NotAdmissible['), 1)


if __name__ == '__main__':
    unittest.main()
