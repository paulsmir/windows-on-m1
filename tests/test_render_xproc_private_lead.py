"""C2a (Wom1CrossProcessPrivateLead): no lead-Barrier wait for a private-only
compute job behind another process's render.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
C1 keeps the lead Barrier, so the compute of process B starts only after the
render of process A ended. C2a lets it start at once. That is legal only
when the two jobs cannot touch the same memory: two processes share memory
only through an allocation both can open, and the D3D12 UMD creates such an
allocation only on the runtime-resource path (pfnAllocateCb with hResource;
Microsoft D3DDDICB_ALLOCATE: a non-NULL hResource is required for shared
resources and the kernel resource handle exists only then) or receives it
through pfnOpenHeapAndResource. Both enter the winsys as imported BOs. So:
  P1 the UMD verdict is private-only only when every allocation the job
     references is a BO this device allocated itself (not imported); one
     imported or unknown BO clears it, and so does a missing device feature;
  P2 the KMD parser admits the envelope flag only under its parse flag (the
     device feature), and no other value of the reserved word at all;
  P3 the window waives only a cross-process plain render -> compute pair
     whose compute carries the flag; a same-process pair is OV2's business;
  P4 the waived Barrier waits on the 3D stamp already in memory (the OV2
     construct), any other pair on the running job's end;
  P5 the switch is separate from C1's, needs C1 and Q2c, and the feature is
     published at the end of start and withdrawn on stop and failure.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'


def build_run(code, sources, includes):
    with tempfile.TemporaryDirectory(prefix='xproc-lead-') as tmp:
        src = Path(tmp) / 'test.c'
        src.write_text(code)
        exe = Path(tmp) / 'test'
        cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', *(f'-I{p}' for p in includes),
               str(src), *map(str, sources), '-o', str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)


UMD_RULE = r'''
#include <stdio.h>
#include "apple_agx_g4_submit.h"
#include "agx_win32_private_only.h"
int main(void) {
  AGX_WIN32_PRIVATE_ONLY v;
  unsigned i;
  /* nothing begun: unknown */
  AGX_WIN32_PRIVATE_ONLY zero = {0};
  printf("unbegun %u\n", AgxWin32PrivateOnlyFlag(&zero, 1u));
  AgxWin32PrivateOnlyBegin(&v);
  printf("empty %u\n", AgxWin32PrivateOnlyFlag(&v, 1u)); /* no reference seen: unknown */
  for (i = 0; i < 12u; ++i) AgxWin32PrivateOnlyAdd(&v, 1);
  printf("private %u %u\n", AgxWin32PrivateOnlyFlag(&v, 1u),
         AgxWin32PrivateOnlyFlag(&v, 1u) == APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY);
  printf("no_feature %u\n", AgxWin32PrivateOnlyFlag(&v, 0u));
  AgxWin32PrivateOnlyAdd(&v, 0); /* an imported or unknown BO */
  printf("foreign %u refs %u foreign %u\n", AgxWin32PrivateOnlyFlag(&v, 1u), v.Refs, v.Foreign);
  AgxWin32PrivateOnlyAdd(&v, 1);
  printf("sticky %u\n", AgxWin32PrivateOnlyFlag(&v, 1u));
  AgxWin32PrivateOnlyBegin(&v);
  AgxWin32PrivateOnlyAdd(&v, 1);
  printf("next_job %u\n", AgxWin32PrivateOnlyFlag(&v, 1u));
  AgxWin32PrivateOnlyAdd(0, 1);
  printf("null %u\n", AgxWin32PrivateOnlyFlag(0, 1u));
  return 0;
}
'''

KMD_RULE = r'''
#include <stdio.h>
#include <string.h>
#include "render_job_window.h"
static ADMISSION_JOB_WINDOW_JOB job(unsigned kind, unsigned long long process, unsigned slot,
                                    unsigned private_only) {
  ADMISSION_JOB_WINDOW_JOB j;
  memset(&j, 0, sizeof(j));
  j.Fence = 40u + kind; j.Kind = kind; j.Process = process; j.VmSlot = slot;
  j.Context = process * 16ULL; j.PrivateOnly = private_only;
  return j;
}
#define C3D ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D
int main(void) {
  ADMISSION_JOB_WINDOW_JOB r = job(ADMISSION_JOB_WINDOW_KIND_RENDER, 100, 2, 0);
  ADMISSION_JOB_WINDOW_JOB rp = job(ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE, 100, 2, 0);
  ADMISSION_JOB_WINDOW_JOB c = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 200, 3, 1);
  ADMISSION_JOB_WINDOW_JOB plain = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 200, 3, 0);
  ADMISSION_JOB_WINDOW_JOB same = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 100, 2, 1);
  ADMISSION_JOB_WINDOW_JOB present = job(ADMISSION_JOB_WINDOW_KIND_PRESENT, 200, 3, 1);
  ADMISSION_JOB_WINDOW_JOB noproc = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 0, 3, 1);
  ADMISSION_JOB_WINDOW_JOB sameslot = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 200, 2, 1);
  ADMISSION_JOB_WINDOW_XPROC_RECEIPT receipt;
  unsigned due = 0u, i;
  printf("waive %u\n", AdmissionJobWindowXprocLeadRefusal(1, C3D, &r, &c));
  printf("off %u\n", AdmissionJobWindowXprocLeadRefusal(0, C3D, &r, &c));
  printf("class_cc %u\n", AdmissionJobWindowXprocLeadRefusal(1,
      ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE, &r, &c));
  printf("prelude %u\n", AdmissionJobWindowXprocLeadRefusal(1, C3D, &rp, &c));
  printf("present %u\n", AdmissionJobWindowXprocLeadRefusal(1, C3D, &r, &present));
  printf("same_process %u\n", AdmissionJobWindowXprocLeadRefusal(1, C3D, &r, &same));
  printf("no_process %u\n", AdmissionJobWindowXprocLeadRefusal(1, C3D, &r, &noproc));
  printf("same_slot %u\n", AdmissionJobWindowXprocLeadRefusal(1, C3D, &r, &sameslot));
  printf("unflagged %u\n", AdmissionJobWindowXprocLeadRefusal(1, C3D, &r, &plain));
  printf("null %u\n", AdmissionJobWindowXprocLeadRefusal(1, C3D, 0, &c));
  /* the wait value is OV2's: the stamp in memory for a waived pair */
  printf("wait %u %u\n", AdmissionJobWindowOv2WaitValue(C3D, 0x500u, 0x400u),
         AdmissionJobWindowOv2WaitValue(C3D, 0x500u, 0u));
  AdmissionJobWindowXprocReceiptInitialize(&receipt);
  for (i = 0; i < 20u; ++i)
    due += AdmissionJobWindowXprocLeadNote(&receipt,
        i < 3u ? ADMISSION_JOB_WINDOW_XPL_NOT_FLAGGED :
        i == 3u ? ADMISSION_JOB_WINDOW_XPL_STAMP : ADMISSION_JOB_WINDOW_XPL_WAIVE,
        50u + i, 0x500u, 0x400u);
  printf("receipt v%u bytes %u considered %u waived %u unflagged %u stamp %u last %u %u %u due %u\n",
         receipt.Version, receipt.Bytes == sizeof(receipt), receipt.LeadConsidered,
         receipt.LeadWaived, receipt.LeadRefused[ADMISSION_JOB_WINDOW_XPL_NOT_FLAGGED],
         receipt.LeadRefused[ADMISSION_JOB_WINDOW_XPL_STAMP], receipt.LastLeadFence,
         receipt.LastLeadExpected, receipt.LastLeadWaitValue, due);
  printf("null_note %u\n", AdmissionJobWindowXprocLeadNote(0, 0u, 1u, 2u, 3u));
  return 0;
}
'''

PARSER = r'''
#include <stdio.h>
#include <string.h>
#include "apple_agx_g4_submit.h"
static int access_ok(void *context, unsigned long long va, unsigned int bytes, int write,
                     APPLE_AGX_G4_ACCESS_KIND kind, unsigned int ordinal) {
  (void)context; (void)va; (void)bytes; (void)write; (void)kind; (void)ordinal;
  return 1;
}
static APPLE_AGX_G4_COMPUTE_ENVELOPE envelope(unsigned reserved, unsigned vdm) {
  APPLE_AGX_G4_COMPUTE_ENVELOPE e;
  APPLE_AGX_G4_PROCESS_RANGE preemption;
  memset(&e, 0, sizeof(e)); memset(&preemption, 0, sizeof(preemption));
  preemption.Va = 0x9000000ULL; preemption.Bytes = 0x10000u;
  e.Native.Type = APPLE_AGX_G4_COMPUTE; e.Native.Size = sizeof(e.Compute);
  e.Native.VdmBarrier = (unsigned short)vdm;
  e.Compute.CdmCtrlStreamBase = 0x8000000ULL; e.Compute.CdmCtrlStreamEnd = 0x8000100ULL;
  if (!AppleAgxG4ComposeHeaderCompute(&e.Header, 0x7000000ULL,
          (unsigned)(sizeof(e.Native) + sizeof(e.Compute)), &preemption)) puts("compose failed");
  e.Header.Base.Reserved = reserved;
  return e;
}
static void parse(const char *name, unsigned reserved, unsigned vdm, unsigned flags) {
  APPLE_AGX_G4_COMPUTE_ENVELOPE e = envelope(reserved, vdm);
  APPLE_AGX_G4_COMPUTE_VIEW view;
  APPLE_AGX_G4_PARSE_RESULT result = AppleAgxG4ParseComputeFlagsEx(&e, sizeof(e), sizeof(e),
      e.Header.Base.CommandVa, e.Header.Base.CommandBytes, 0x10000u, flags, access_ok, 0,
      &view, 0);
  printf("%s %d %d %d\n", name, (int)result, result == AppleAgxG4ParseOk ? view.PrivateOnly : -1,
         result == AppleAgxG4ParseOk ? view.Independent : -1);
}
int main(void) {
  const unsigned P = APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY;
  parse("plain", 0u, 0u, 0u);
  parse("plain_under_flag", 0u, 0u, APPLE_AGX_G4_PARSE_PRIVATE_ONLY);
  parse("flag_without_feature", P, 0u, 0u);
  parse("flag_under_ov2_only", P, 0u, APPLE_AGX_G4_PARSE_INDEPENDENT_SUBQUEUE);
  parse("flag", P, 0u, APPLE_AGX_G4_PARSE_PRIVATE_ONLY);
  parse("other_bits", P | 2u, 0u, APPLE_AGX_G4_PARSE_PRIVATE_ONLY);
  parse("other_value", 2u, 0u, APPLE_AGX_G4_PARSE_PRIVATE_ONLY);
  parse("both", P, APPLE_AGX_G4_BARRIER_NONE,
        APPLE_AGX_G4_PARSE_PRIVATE_ONLY | APPLE_AGX_G4_PARSE_INDEPENDENT_SUBQUEUE);
  parse("unknown_parse_flag", 0u, 0u, 0x8u);
  printf("values %u %u\n", APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY, APPLE_AGX_G4_PARSE_PRIVATE_ONLY);
  return 0;
}
'''


class PrivateOnlyLead(unittest.TestCase):
    def test_umd_private_only_verdict(self):
        ran = build_run(UMD_RULE, [], [WINSYS, SHARED / 'include'])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split('\n')[:-1], [
            'unbegun 0', 'empty 0', 'private 1 1', 'no_feature 0', 'foreign 0 refs 13 foreign 1',
            'sticky 0', 'next_job 1', 'null 0'])

    def test_kmd_lead_decision(self):
        ran = build_run(KMD_RULE, [RENDER / 'src/render_job_window.c'], [RENDER / 'include'])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split('\n')[:-1], [
            'waive 0', 'off 1', 'class_cc 2', 'prelude 2', 'present 2', 'same_process 2',
            'no_process 2', 'same_slot 2', 'unflagged 3', 'null 2', 'wait 1024 1280',
            'receipt v2 bytes 1 considered 20 waived 16 unflagged 3 stamp 1 last 69 1280 1024 due 16',
            'null_note 0'])

    def test_parser_admits_the_flag_only_under_the_feature(self):
        ran = build_run(PARSER, [SHARED / 'src/apple_agx_g4_submit.c'], [SHARED / 'include'])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        ok, invalid = 0, None
        lines = dict(line.split(' ', 1) for line in ran.stdout.split('\n') if line)
        self.assertEqual(lines['plain'], f'{ok} 0 0')
        self.assertEqual(lines['plain_under_flag'], f'{ok} 0 0')
        self.assertEqual(lines['flag'], f'{ok} 1 0')
        self.assertEqual(lines['both'], f'{ok} 1 1')
        invalid = lines['flag_without_feature']
        self.assertNotEqual(invalid.split()[0], str(ok))
        for name in ('flag_under_ov2_only', 'other_bits', 'other_value', 'unknown_parse_flag'):
            self.assertEqual(lines[name], invalid, name)
        self.assertEqual(lines['values'], '1 4')

    def test_umd_wiring(self):
        batch = (WINSYS / 'agx_win32_gpuva_batch.c').read_text()
        add = batch[batch.index('static int add_bo('):]
        add = add[:add.index('\n}\n')]
        # every allocation that enters a reference list is classified once
        self.assertIn('refs[(*count)++]=view;\n'
                      '#ifdef _MSC_VER /* C2a; the host models extract this body with their own backend */\n'
                      '  AgxWin32PrivateOnlyAdd(&b->PrivateOnly,AgxWin32AsahiBoPrivate(b,bo));\n#endif', add)
        # add_bo returns before this line for a duplicate or a failure only
        self.assertEqual(batch.count('AgxWin32PrivateOnlyAdd('), 1)
        finish = batch[batch.index('int AgxWin32AsahiBatchFinishCompute('):]
        finish = finish[:finish.index('\nfail:')]
        begin = finish.index('AgxWin32PrivateOnlyBegin(&b->PrivateOnly);')
        first = finish.index('add_bo(b,refs,&count,limit,g->Timestamp)')
        last = finish.index('util_dynarray_foreach(&batch->pipeline_pool.bos')
        flag = finish.index('envelope.Header.Base.Reserved=AgxWin32PrivateOnlyFlag(&b->PrivateOnly,'
                            'AgxWin32PrivateOnlyFeature);')
        submit = finish.index('AgxWin32GpuvaSubmit(&b->Gpuva,refs,count,')
        self.assertLess(begin, first)
        self.assertLess(last, flag)
        self.assertLess(flag, submit)
        self.assertEqual(batch.count('Header.Base.Reserved='), 1)
        bo = (WINSYS / 'agx_win32_asahi_bo.c').read_text()
        private = bo[bo.index('int AgxWin32AsahiBoPrivate('):]
        private = private[:private.index('\n}\n')]
        self.assertIn('bo->Backend==b', private)
        self.assertIn('!bo->Imported', private)
        # nothing but the import path marks a BO imported, and it always does
        self.assertEqual(bo.count('bo->Imported=1;'), 1)
        scene = (WINSYS / 'agx_win32_asahi_scene.c').read_text()
        self.assertIn('AgxWin32PrivateOnlyFeature=(windows->Info.Features & '
                      'AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE) ? 1u : 0u;', scene)
        # the only producers of a runtime-resource allocation import their BO
        owner = (WINSYS / 'agx_d3d12_windows.cpp').read_text()
        for name in ('AgxD3d12OwnerCreatePresentationTexture', 'AgxD3d12OwnerOpenSharedTexture'):
            body = owner[owner.index('extern "C" HRESULT ' + name + '('):]
            body = body[:body.index('\n}\n')]
            self.assertIn('AgxWin32AsahiImportLinearColor32(', body, name)
        screen = (RENDER / 'umd/src/umd_win32_screen.c').read_text()
        self.assertIn('allocate.hResource = NULL;', screen)

    def test_kmd_wiring(self):
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        at = backend.index('runtime->XprocLeadEnabled = runtime->CrossProcessPairs &&')
        stmt = backend[at:backend.index(';', at)]
        self.assertIn('runtime->Initdata.RenderSharedMemory.ComputeOwnQueue', stmt)
        self.assertIn('AdmissionPlatformSwitchEnabled(Context, L"Wom1CrossProcessPrivateLead")', stmt)
        self.assertEqual(backend.count('L"Wom1CrossProcessPrivateLead"'), 1)
        self.assertLess(backend.index('L"Wom1CrossProcessPairs"'), at)
        self.assertEqual(backend.count('runtime->XprocLeadWaive = TRUE;'), 1)
        self.assertIn('AdmissionJobWindowXprocLeadRefusal(1u, (unsigned int)barrier,', backend)
        # the feature: published last on the success path, withdrawn on failure and stop
        start = backend.index('NTSTATUS AdmissionPlatformRuntimeStart(')
        body = backend[start:backend.index('\nFail:', start)]
        reset = body.rindex('InterlockedExchange(&Context->GpuSoftFaults, 0);')
        last = body.index('GpuPrivateOnlyCompute', reset)
        line = body[body.rindex('\n', 0, last):body.index('\n', last)]
        self.assertIn('runtime->XprocLeadEnabled ? 1 : 0', line)
        self.assertNotIn('GpuPrivateOnlyCompute', body[:reset])
        self.assertEqual(backend.count('InterlockedExchange(&Context->GpuPrivateOnlyCompute, 0);'), 3)
        lifecycle = (RENDER / 'src/lifecycle.c').read_text()
        self.assertIn('if (InterlockedCompareExchange(&context->GpuPrivateOnlyCompute, 0, 0) != 0)\n'
                      '      info->Features |= AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE;', lifecycle)
        g3 = (RENDER / 'src/gpuva_g3_windows.c').read_text()
        flags = g3[g3.index('static unsigned int AdmissionG4ComputeParseFlags('):]
        flags = flags[:flags.index('\n}\n')]
        self.assertIn('GpuPrivateOnlyCompute', flags)
        self.assertIn('APPLE_AGX_G4_PARSE_PRIVATE_ONLY', flags)
        self.assertIn('APPLE_AGX_G4_PARSE_INDEPENDENT_SUBQUEUE', flags)
        info = (SHARED / 'include/apple_agx_win32_device_info.h').read_text()
        self.assertIn('#define AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE 0x8u', info)
        known = info[info.index('#define AGX_WIN32_DEVICE_FEATURES_KNOWN'):]
        self.assertIn('AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE', known[:400])
        # the waived Barrier is built with OV2's wait value on the 3D stamp in memory
        lead = backend[backend.index('static APPLE_AGX_BACKEND_BOOL AdmissionBuildLeadBarrier('):]
        lead = lead[:lead.index('\n}\n')]
        waive = lead[lead.index('if (runtime->XprocLeadWaive &&'):]
        waive = waive[:waive.index('runtime->XprocLeadWaive = FALSE;')]
        self.assertIn('AppleAgxRenderSharedMemoryReadFirmwareStamp(owner,', waive)
        self.assertIn('APPLE_AGX_RENDER_STAMP_QUEUE_3D, &now) && now != 0u', waive)
        self.assertIn('AdmissionJobWindowOv2WaitValue(cls,', waive)
        self.assertIn('AdmissionJobWindowXprocLeadNote(&runtime->XprocReceipt,', waive)
        describe = backend[backend.index('static VOID AdmissionJobWindowDescribeLocked('):]
        describe = describe[:describe.index('\n}\n')]
        self.assertIn('job->PrivateOnly = image->BoundFence == fence && image->G4Compute &&', describe)
        self.assertIn('image->G4ComputeEnvelope.Header.Base.Reserved ==', describe)


if __name__ == '__main__':
    unittest.main()
