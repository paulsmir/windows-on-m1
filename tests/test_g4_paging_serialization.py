"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Q22 must not 'fix' a fresh-map wait by publishing through a busy v5 broker or
retiring a later node-0 paging fence before its render predecessor. Exercise
the real broker and KMD queue, including a disjoint new leaf, another owner,
destructive updates, idle leases, publication failure and fence ordering.
The negotiated-extension runner additionally executes the real wire/broker
compatibility and live-publication cases from the selected m1n1 source tree.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_g4_paging_submit_containment import SHIM, function

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'
M1N1 = ROOT / 'm1n1_windows'

BROKER_CASES = r'''
#include <stdio.h>
#define main broker_fixture_selftest_main
#include "hv_agx_gpuva_v5_test.c"
#undef main
int main(void) {
  struct fixture *f = new_fixture();
  uint64_t token = 0;
  uint64_t logical[4] = {P_DATA, P_DATA+0x1000, P_DATA+0x2000, P_DATA+0x3000};
  uint64_t other[4] = {Q_DATA, Q_DATA+0x1000, Q_DATA+0x2000, Q_DATA+0x3000};
  prepare(f, 1, P_ROOT, P_L1, P_L2, P_DATA, false);
  prepare(f, 2, Q_ROOT, Q_L1, Q_L2, Q_DATA, false);
  assert(hv_agx_gpuva_v5_lease(&f->broker, 1, 1, 1, &token)==HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_job_begin(&f->broker, 1, token)==HV_AGX_GPUVA_V5_OK);
  uint64_t old = f->pages[2][8];
  uint64_t generation = f->broker.processes[0].map_generation;
  unsigned invalidations = f->invalidations[1];
  assert(old && !f->pages[2][9] && !f->pages[1][1]);

  /* An empty slot is unrelated to the existing slot8. Current v5 still
   * refuses it, before storing a PTE or changing the generation. */
  assert(hv_agx_gpuva_v5_update_leaf(&f->broker, 1, 1, P_L2, 9,
      logical, 17, 15, 15, 15)==HV_AGX_GPUVA_V5_BUSY); /* fresh-map gate */
  assert(hv_agx_gpuva_v5_update_parent(&f->broker, 1, 1, P_L1, 1, P_L2)==
      HV_AGX_GPUVA_V5_BUSY);
  assert(hv_agx_gpuva_v5_flush_tlb(&f->broker, 1, 1, P_ROOT,
      9*0x4000, 10*0x4000)==HV_AGX_GPUVA_V5_BUSY);
  assert(hv_agx_gpuva_v5_update_leaf(&f->broker, 1, 1, P_L2, 8,
      NULL, 0, 15, 0, 0)==HV_AGX_GPUVA_V5_BUSY);
  assert(hv_agx_gpuva_v5_update_leaf(&f->broker, 1, 1, P_L2, 8,
      logical, 17, 15, 15, 0)==HV_AGX_GPUVA_V5_BUSY);
  assert(f->pages[2][8]==old && !f->pages[2][9] && !f->pages[1][1]);
  assert(f->invalidations[1]==invalidations &&
      f->broker.processes[0].map_generation==generation);
  assert(f->broker.slots[1].jobs==1 && !f->broker.tainted);

  /* The exclusion belongs to the affected owner, not the whole GPU. */
  assert(hv_agx_gpuva_v5_update_leaf(&f->broker, 2, 1, Q_L2, 9,
      other, 17, 15, 15, 15)==HV_AGX_GPUVA_V5_OK);
  assert(f->pages[6][9] && f->broker.slots[1].jobs==1);
  assert(f->pages[2][8]==old && !f->pages[2][9]);

  /* End work but retain the lease: publication and TLB ack now succeed. */
  assert(hv_agx_gpuva_v5_job_end(&f->broker, 1, token)==HV_AGX_GPUVA_V5_OK);
  assert(hv_agx_gpuva_v5_update_leaf(&f->broker, 1, 1, P_L2, 9,
      logical, 17, 15, 15, 15)==HV_AGX_GPUVA_V5_OK);
  assert(f->pages[2][9]==old && f->invalidations[1]==invalidations+1);
  assert(f->broker.processes[0].map_generation==generation+1);
  assert(hv_agx_gpuva_v5_flush_tlb(&f->broker, 1, 1, P_ROOT, 0, 0)==
      HV_AGX_GPUVA_V5_OK);
  assert(f->invalidations[1]==invalidations+2);

  /* Failed publication must not leave the new slot visible or advance its
   * generation. The real broker retries invalidation after rollback. */
  f->fail_once = 1;
  assert(hv_agx_gpuva_v5_update_leaf(&f->broker, 1, 1, P_L2, 10,
      logical, 17, 15, 15, 15)==HV_AGX_GPUVA_V5_TLB);
  assert(!f->pages[2][10] && !f->broker.tainted);
  assert(f->broker.processes[0].map_generation==generation+1);
  assert(hv_agx_gpuva_v5_update_leaf(&f->broker, 1, 1, P_L2, 8,
      NULL, 0, 15, 0, 0)==HV_AGX_GPUVA_V5_OK);
  assert(!f->pages[2][8] && f->pages[2][9]==old);
  assert(hv_agx_gpuva_v5_release(&f->broker, 1, token)==HV_AGX_GPUVA_V5_OK);
  free(f);
  puts("Q22 current broker: fresh and destructive writes wait for own job; foreign owner proceeds");
  return 0;
}
'''

QUEUE_CASES = r'''
int main(void) {
  ADMISSION_CONTEXT *c = calloc(1, sizeof(*c));
  DXGKARG_SUBMITCOMMAND args = {0};
  ADMISSION_PAGING_RECORD record = {0};
  assert(c);
  c->Started = c->SchedulerInitialized = 1;
  c->PagingWorkItem = c;
  AppleAgxSchedulerInitialize(&c->Scheduler);
  AdmissionRenderPacketInitialize(&c->RenderPacket);
  record.Header.Magic = ADMISSION_PAGING_MAGIC;
  record.Header.Version = ADMISSION_PAGING_VERSION;
  record.Header.RecordBytes = sizeof(record);
  record.Kind = AdmissionPagingMonitoredFence;
  record.DestinationSegment = 2;
  record.DestinationIpa = 0x20000000;
  record.Bytes = 8;
  record.FenceValue = 53;
  assert(AdmissionPagingRecordsValid(&record, 1, 64, sizeof(ADMISSION_PAGING_MARKER)));
  assert(AppleAgxSchedulerQueueFence(&c->Scheduler, 0, 0, 41));
  assert(AppleAgxSchedulerActivateFence(&c->Scheduler, 0, 0, 41));
  c->DispatchedFence = 41;
  args.SubmissionFenceId = 42;
  c->PagingRecordsUnsubmitted = 1;
  assert(AdmissionCpuQueueSubmit(c, &args, ADMISSION_CPU_PACKET_PAGING,
      &record, sizeof(record))==STATUS_SUCCESS);
  assert(c->CpuQueueCount==1 && cpu_launches==0 && !c->PagingPending);
  assert(!AdmissionPagingQuiescent(c));
  assert(c->Scheduler.ActiveFence==41 && c->Scheduler.QueuedFence==42);
  assert(!AppleAgxSchedulerCompleteActiveFence(&c->Scheduler, 0, 0, 42));
  assert(c->Scheduler.CompletedFence==0);
  assert(AppleAgxSchedulerCompleteActiveFence(&c->Scheduler, 0, 0, 41));
  c->DispatchedFence = 0;
  AdmissionDispatchQueuedWork(c);
  assert(cpu_launches==1 && c->PagingPending && c->PagingFence==42);
  assert(c->CpuQueueCount==0 && c->PagingRecordCount==1);
  assert(c->PagingRecords[0].Kind==AdmissionPagingMonitoredFence &&
      c->PagingRecords[0].FenceValue==53);
  assert(c->Scheduler.CompletedFence==41);
  /* OS worker/interrupt I/O is modeled; the queue/scheduler above is real. */
  assert(AppleAgxSchedulerCompleteActiveFence(&c->Scheduler, 0, 0, 42));
  c->PagingPending = 0;
  c->DispatchedFence = 0;
  assert(AdmissionPagingQuiescent(c) && !lock_depth);
  free(c);
  return 0;
}
'''


class PagingSerialization(unittest.TestCase):
    def test_negotiated_broker_live_publication(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Negotiated fresh publication must succeed before job retirement,
        while legacy clients and destructive updates retain their guards.
        Use Q22_M1N1_SOURCE for an independently checked-out broker candidate.
        """
        source = Path(os.environ.get('Q22_M1N1_SOURCE', str(M1N1)))
        runner = source / 'tests/run_gpuva_live_tests.py'
        if not runner.is_file():
            self.skipTest('negotiated m1n1 source unavailable; set Q22_M1N1_SOURCE')
        env = dict(os.environ, AGX_SHARED_ROOT=str(ROOT))
        import sys
        run = subprocess.run([sys.executable, str(runner)], env=env,
                             capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('hv_agx_gpuva_live_test PASS', run.stdout)

    def compile_run(self, source, sources, includes):
        with tempfile.TemporaryDirectory(prefix='q22-paging-') as tmp:
            tmp = Path(tmp)
            program, binary = tmp / 'case.c', tmp / 'case'
            program.write_text(source)
            command = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                       '-Wextra', '-Werror', '-Wno-unused-function',
                       '-ftrivial-auto-var-init=pattern', '-fsanitize=address,undefined']
            for include in includes:
                command += ['-I', str(include)]
            command += [str(program), *map(str, sources), '-o', str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_real_broker_requires_job_end_even_for_a_fresh_leaf(self):
        if not (M1N1 / 'src/hv_agx_gpuva_v5.c').is_file():
            self.skipTest('m1n1_windows uninitialized; real broker unavailable')
        self.compile_run(BROKER_CASES,
                         [M1N1 / 'src/hv_agx_gpuva_v5.c', SHARED / 'src/apple_agx_uat.c'],
                         [M1N1 / 'tests', SHARED / 'include'])

    def test_real_kmd_paging_fence_follows_active_render_on_node_zero(self):
        header = (RENDER / 'include/render_admission.h').read_text()
        packet = re.search(r'typedef struct _ADMISSION_CPU_PACKET\s*\{.*?'
                           r'\}\s*ADMISSION_CPU_PACKET;', header, re.S).group(0)
        queue = (RENDER / 'src/work_queue_windows.c').read_text()
        source = SHIM.replace('/*PACKET*/', packet)
        for name in ('AdmissionCpuQueueSubmit', 'AdmissionDispatchQueuedWork',
                     'AdmissionPagingQuiescent'):
            source += '\n' + function(queue, name)
        self.compile_run(source + QUEUE_CASES,
                         [SHARED / 'src/apple_agx_scheduler.c',
                          RENDER / 'src/render_submission.c', RENDER / 'src/render_paging.c'],
                         [RENDER / 'include', SHARED / 'include'])


if __name__ == '__main__':
    unittest.main()
