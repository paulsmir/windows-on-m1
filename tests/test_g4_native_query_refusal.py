"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2206: DWM's readiness escapes failed with STATUS_INVALID_PARAMETER (431 of
432 rejects, each followed by a DWM device re-creation), a status shared by
about 60 predicates of AdmissionGpuvaG3NativeQueryEscape and its batch
wrapper, so the evidence could not name the predicate. This pins the
receipt-only refusal codes:
- every refusing exit of both escapes carries its own code (1-67), and each
  code has a name in apple_agx_g3_native_query_refusal.h;
- with the receipt compiled in, every status and every output byte equals
  the build without it (no behaviour change), and a success or not-ready
  answer notes nothing;
- each driven predicate notes its own code, under the G3 lock exactly for
  codes 28-48, and the registry export runs with no lock held.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body
from test_g4_native_allocation_guard import KMD_FIXTURE

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
KMD = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'
HEADER = SHARED / 'include/apple_agx_g3_native_query_refusal.h'

STUBS = r'''
#include "apple_agx_g3_native_query_refusal.h"
static unsigned noted_code, notes, exports, noted_flags;
static void AdmissionG3NativeQueryRefused(ADMISSION_CONTEXT *a, const DXGKARG_ESCAPE *e,
    const APPLE_AGX_G3_NATIVE_QUERY *q, ULONG code, NTSTATUS status, ULONG flags,
    ADMISSION_G3_PROCESS *p, ADMISSION_RENDER_CONTEXT *c, ADMISSION_OPEN_ALLOCATION *o) {
  (void)a; (void)e; (void)q; (void)status; (void)p; (void)c; (void)o;
  assert(((flags & APPLE_AGX_G3_NQ_FLAG_LOCKED) != 0) == (locked != 0));
  noted_code = code; noted_flags = flags; ++notes;
}
static void AdmissionG3NativeQueryRefusalExport(ADMISSION_CONTEXT *a) {
  (void)a; assert(!locked); ++exports;
}
'''

# (expected code, change, restore); 0 = must note nothing.
CASES = [
    (0, '', ''),
    (2, 'a.Started=0', 'a.Started=1'),
    (4, 'irql=1', 'irql=0'),
    (5, 'e.Flags.Value=1', 'e.Flags.Value=0'),
    (6, 'e.pPrivateDriverData=NULL', 'e.pPrivateDriverData=&q'),
    (7, 'e.PrivateDriverDataSize--', 'e.PrivateDriverDataSize++'),
    (8, 'a.Interface.DxgkCbAcquireHandleData=NULL', 'a.Interface.DxgkCbAcquireHandleData=acquire'),
    (9, 'a.Interface.DxgkCbReleaseHandleData=NULL', 'a.Interface.DxgkCbReleaseHandleData=release'),
    (10, 'a.GpuvaG3State=NULL', 'a.GpuvaG3State=&state'),
    (11, 'q.Magic++', 'q.Magic--'),
    (12, 'q.Version=9', 'q.Version=1'),
    (13, 'q.Bytes--', 'q.Bytes++'),
    (14, 'q.Allocation=0', 'q.Allocation=42'),
    (15, 'q.GpuVa=0', 'q.GpuVa=va'),
    (16, 'q.GpuVa+=4096', 'q.GpuVa=va'),
    (17, 'q.GpuVa=1ULL<<39', 'q.GpuVa=va'),
    (18, 'q.AllocationBytes=0', 'q.AllocationBytes=65536'),
    (19, 'q.AllocationBytes=1ULL<<32', 'q.AllocationBytes=65536'),
    (20, 'q.GpuVa=(1ULL<<39)-65536;q.AllocationBytes=131072', 'q.GpuVa=va;q.AllocationBytes=65536'),
    (21, 'q.Write=2', 'q.Write=1'),
    (22, 'q.Ready=1', ''),
    (23, 'q.Reason=1', ''),
    (24, 'q.FailedVa=1', ''),
    (25, 'q.Reserved2=1', 'q.Reserved2=0'),
    (26, 'q.Allocation=43', 'q.Allocation=42'),
    (28, 'e.hKmdProcessHandle=NULL', 'e.hKmdProcessHandle=&p'),
    (29, 'p.Poisoned=1', 'p.Poisoned=0'),
    (30, 'p.CpuOnlyMappings=1', 'p.CpuOnlyMappings=0'),
    (31, 'p.Graph.Created=0', 'p.Graph.Created=1'),
    (32, 'p.Graph.Uncertain=1', 'p.Graph.Uncertain=0'),
    (33, 'e.hContext=NULL', 'e.hContext=&c'),
    (34, 'c.Win32Transport=0', 'c.Win32Transport=1'),
    (35, 'c.GpuvaG3Closing=1', 'c.GpuvaG3Closing=0'),
    (36, 'c.GpuvaG3Poisoned=1', 'c.GpuvaG3Poisoned=0'),
    (37, 'c.Object.Device=NULL', 'c.Object.Device=&d.Object'),
    (38, 'e.hDevice=NULL', 'e.hDevice=&d'),
    (39, 'd.Object.Adapter=NULL', 'd.Object.Adapter=&a.ObjectAdapter'),
    (40, 'opened.Magic=0', 'opened.Magic=ADMISSION_OPEN_ALLOCATION_MAGIC'),
    (41, 'opened.Device=NULL', 'opened.Device=&d'),
    (43, 'opened.Allocation=NULL', 'opened.Allocation=&allocation.Object'),
    (44, 'allocation.Object.Magic=0', 'allocation.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC'),
    # EXP2208: no longer a refusal; answered as READ_ONLY data.
    (0, 'opened.ReadOnly=1', 'opened.ReadOnly=0'),
    (46, 'q.AllocationBytes=65537', 'q.AllocationBytes=65536'),
    (47, 'allocation.Object.Description.Size=1ULL<<32', 'allocation.Object.Description.Size=65536'),
    (48, 'q.GpuVa=(1ULL<<39)-65536;allocation.Object.Description.Size=131072',
     'q.GpuVa=va;allocation.Object.Description.Size=65536'),
    # A not-ready answer is data, not a refusal.
    (0, 'q.AllocationBytes=16384;leaves[3].AuxIpa=0', 'q.AllocationBytes=65536;leaves[3].AuxIpa=ipa+49152'),
    (0, 'q.Write=0', 'q.Write=1'),
]


def functions(text):
    return {name: body(text, name) for name in (
        'AdmissionGpuvaG3NativeQueryEscape', 'AdmissionGpuvaG3NativeQueryBatchEscape')}


class NativeQueryRefusalCodes(unittest.TestCase):
    def test_every_refusing_exit_has_a_distinct_named_code(self):
        text = KMD.read_text(encoding='latin1')
        names = {int(v): n for n, v in re.findall(
            r'#define (APPLE_AGX_G3_NQ_[A-Z0-9_]+) (\d+)u', HEADER.read_text())
            if not n.startswith(('APPLE_AGX_G3_NQ_REFUSAL', 'APPLE_AGX_G3_NQ_LAST'))
            and '_FLAG_' not in n and not n.startswith((
                'APPLE_AGX_G3_NQ_PROCESS_FOUND', 'APPLE_AGX_G3_NQ_CONTEXT_FOUND',
                'APPLE_AGX_G3_NQ_OPENED_FOUND'))}
        codes = []
        for name, src in functions(text).items():
            src = re.sub(r'#define(?:[^\n]*\\\n)*[^\n]*', '', src)
            # No refusing exit may bypass the receipt.
            self.assertNotRegex(src, r'\breturn STATUS_INVALID|\bgoto Unlock\b', name)
            codes += [int(c) for c in re.findall(
                r'\bNQB?_(?:RETURN|QUERY_RETURN|UNLOCK)\((\d+)u', src)]
            codes += [int(c) for c in re.findall(r'refusal = !opened \? (\d+)u : (\d+)u', src)[0]] \
                if 'refusal = !opened' in src else []
        # 45 (READ_ONLY) retired by EXP2208: answered as data.
        self.assertEqual(sorted(codes), [c for c in range(1, 68) if c != 45])
        last = int(re.search(r'APPLE_AGX_G3_NQ_LAST_CODE (\d+)u', HEADER.read_text()).group(1))
        self.assertEqual(last, 67)
        for code in codes:
            self.assertIn(code, names, 'code %d has no name' % code)

    def run_cases(self, receipt):
        funcs = '\n'.join(body(KMD.read_text(encoding='latin1'), n) for n in (
            'AdmissionG3CopyPte', 'AdmissionGpuvaG3NativeQueryEscape'))
        prelude, rest = KMD_FIXTURE.split('@@FUNCS@@')
        setup = rest[rest.index('int main(void) {'):rest.index(' #define CHECK')]
        globals_ = rest[:rest.index('int main(void) {')]
        drive = [' CHECK0();']
        for code, change, restore in CASES:
            drive.append(' {%s;q.Ready=0;q.Reason=0;q.FailedVa=0;'
                         'if(%d==22)q.Ready=1;if(%d==23)q.Reason=1;if(%d==24)q.FailedVa=1;'
                         'unsigned before=notes;NTSTATUS st=AdmissionGpuvaG3NativeQueryEscape(&a,&e);'
                         'assert(!locked && !refs);'
                         'printf("%d %%d ",st);dump(&q);'
                         'printf(" %%u\\n",notes!=before?noted_code:0u);%s;}'
                         % (change, code, code, code, code, restore))
        drive.append(' {NTSTATUS st=AdmissionGpuvaG3NativeQueryEscape(NULL,&e);'
                     'printf("1 %d %u\\n",st,noted_code);}')
        drive.append(' {unsigned before=notes;NTSTATUS st=AdmissionGpuvaG3NativeQueryEscape(&a,NULL);'
                     'printf("3 %d %u\\n",st,notes!=before?noted_code:0u);}')
        source = (('#define ADMISSION_G3_NATIVE_QUERY_REFUSAL_RECEIPT 1\n' if receipt else '') +
                  '#include <stdio.h>\n' + prelude + (STUBS if receipt else
                  'static unsigned noted_code, notes;\n') + funcs + globals_ +
                  'static void dump(const APPLE_AGX_G3_NATIVE_QUERY *q){const unsigned char *b='
                  '(const unsigned char *)q;for(unsigned i=0;i<sizeof(*q);++i)printf("%02x",b[i]);}\n' +
                  setup + ' #define CHECK0() do {q.Ready=0;q.FailedVa=0;q.Reason=0;'
                  'assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)==0);assert(q.Ready==1);} while(0)\n' +
                  '\n'.join(drive) + '\n return 0;\n}\n')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.c'
            exe = Path(tmp) / 'replay'
            src.write_text(source)
            cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-include',
                   str(ROOT / 'tests/d3d12_wait_disabled_fixture.h'), '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-I', str(SHARED / 'include'), str(src)] + [
                   str(SHARED / 'src' / n) for n in ('apple_agx_gpuva_g3_graph.c',
                   'apple_agx_gpuva_g3_translation.c', 'apple_agx_gpuva_broker_v5_client.c')]
            build = subprocess.run(cmd + ['-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            return run.stdout.split('\n')[:-1]

    def test_receipt_changes_no_status_or_output_and_names_each_predicate(self):
        plain = self.run_cases(False)
        noted = self.run_cases(True)
        self.assertEqual(len(plain), len(CASES) + 2)
        for a, b in zip(plain, noted):
            # Same expected code, status and every output byte.
            self.assertEqual(a.split()[:-1], b.split()[:-1])
            self.assertEqual(a.split()[-1], '0')
        for line in noted:
            expected, status, *_, code = line.split()
            self.assertEqual(int(code), int(expected), line)
            self.assertEqual(status != '0', int(expected) != 0, line)

    def test_note_keeps_first_and_last_and_counts_codes(self):
        source = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "apple_agx_g3_native_query_refusal.h"
int main(void) {
  /* tools/decode_g3_native_query_refusal.py reads this layout. */
  assert(sizeof(APPLE_AGX_G3_NQ_REFUSAL_RECORD) == 152u);
  assert(offsetof(APPLE_AGX_G3_NQ_REFUSAL_RECEIPT, First) == 528u);
  assert(sizeof(APPLE_AGX_G3_NQ_REFUSAL_RECEIPT) == 832u);
  static APPLE_AGX_G3_NQ_REFUSAL_RECEIPT r;
  APPLE_AGX_G3_NQ_REFUSAL_RECORD a, b;
  memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
  a.OsProcessId = 864u; b.OsProcessId = 9628u;
  AppleAgxG3NqRefusalNote(&r, 33u, &a);
  AppleAgxG3NqRefusalNote(&r, 41u, &b);
  AppleAgxG3NqRefusalNote(&r, 41u, NULL);
  AppleAgxG3NqRefusalNote(&r, 999u, &b);
  assert(r.Version == APPLE_AGX_G3_NQ_REFUSAL_VERSION && r.Bytes == sizeof(r));
  assert(r.Total == 3u && r.Dropped == 1u);
  assert(r.Counts[33] == 1u && r.Counts[41] == 1u && r.Counts[0] == 1u);
  assert(r.First.Code == 33u && r.First.OsProcessId == 864u && r.First.Sequence == 1u);
  assert(r.Last.Code == 0u && r.Last.Sequence == 3u);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'note.c'
            exe = Path(tmp) / 'note'
            src.write_text(source)
            build = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                                    '-Werror', '-Wconversion', '-I', str(SHARED / 'include'),
                                    str(src), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
