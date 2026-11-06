"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2215 (and EXP2203): one ENGINE reset per suite. Wom1PreemptRefusal showed
PreemptCommand 1104 arriving while boundary 1103 was still
NotificationClaimed, at the same millisecond: AdmissionSchedulerTryNotifyPreemption
claims, drops the scheduler lock, reports DMA_PREEMPTED and only then re-takes
the lock to commit. VidSch can process the notification and send the next
PreemptCommand inside that window; BeginBoundaryPreemption refused it, set
SchedulerFaulted, and the unreported preemption ended in a TDR.
Invariant: a claimed boundary whose notification was delivered is complete
for the next PreemptCommand, and its claimer's late commit is not a fault; an
undelivered claimed boundary still refuses a new one.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

try:
    import test_preempt_notification_competing_callers as base
    from test_g4_submit_virtual_replay import function_body
except ModuleNotFoundError:
    from tests import test_preempt_notification_competing_callers as base
    from tests.test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]

SHIM = base.SHIM.replace(
    ' assert(data->DmaPreempted.PreemptionFenceId==77);\n',
    ' assert(data->DmaPreempted.PreemptionFenceId==77||data->DmaPreempted.PreemptionFenceId==78);\n'
    ' lastNotified=data->DmaPreempted.PreemptionFenceId;\n').replace(
    'static int locks,', 'static unsigned lastNotified; static int nextInsideNotify;\n'
    'NTSTATUS AdmissionDdiPreemptCommand(HANDLE,const DXGKARG_PREEMPTCOMMAND *);\nstatic int locks,').replace(
    ' if(injectInsideNotify){',
    ' if(nextInsideNotify && data->DmaPreempted.PreemptionFenceId==77){\n'
    '  DXGKARG_PREEMPTCOMMAND next={78,0,0,{0}};nextInsideNotify=0;\n'
    '  /* VidSch processed 77 and sends the next preemption before the\n'
    '   * claimer re-took the lock to commit 77. */\n'
    '  assert(AppleAgxSchedulerPreemptionPhase(&a->Scheduler)==AppleAgxPreemptionNotificationClaimed);\n'
    '  assert(AdmissionDdiPreemptCommand(a,&next)==STATUS_SUCCESS);\n'
    ' }\n'
    ' if(injectInsideNotify){')
assert 'nextInsideNotify=0' in SHIM and 'lastNotified=' in SHIM

CASES = base.CASES.split('int main(void){')[0] + r'''
int main(void){
 ADMISSION_CONTEXT a;
 DXGKARG_PREEMPTCOMMAND args={77,0,0,{0}};
 APPLE_AGX_PREEMPTION claim;
 /* EXP2215: the next PreemptCommand inside the delivery window. */
 setup(&a);nextInsideNotify=1;
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 fprintf(stderr,"next inside delivery: fault=%x refusals=%d notified=%d last=%u phase=%u\n",
   (unsigned)a.SchedulerFaulted,(int)a.PreemptRefusals,interrupts,lastNotified,
   (unsigned)a.Scheduler.PreemptionPhase);
 assert(!a.SchedulerFaulted && !a.PreemptRefusals);
 assert(interrupts==2 && lastNotified==78 && !locks);
 assert(a.Scheduler.PreemptionPhase==AppleAgxPreemptionIdle && !a.Scheduler.PendingPreemptionFence);
 assert(!a.Scheduler.EarlyCommittedPreemptionFence && !a.Scheduler.DeliveredPreemptionFence);
 assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,0x735));
 /* Not yet delivered: a claimed boundary still refuses a new preemption. */
 setup(&a);
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&a.Scheduler,0,0,77,0x732,0));
 assert(AppleAgxSchedulerClaimBoundaryPreemption(&a.Scheduler,&claim));
 assert(!AppleAgxSchedulerBeginBoundaryPreemption(&a.Scheduler,0,0,78,0x732,0));
 assert(a.Scheduler.PreemptionPhase==AppleAgxPreemptionNotificationClaimed);
 /* Delivered: committed by the next Begin, the late commit succeeds once. */
 a.Scheduler.DeliveredPreemptionFence=77;
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&a.Scheduler,0,0,78,0x732,0));
 assert(a.Scheduler.PendingPreemptionFence==78);
 assert(AppleAgxSchedulerCommitBoundaryPreemption(&a.Scheduler,77));
 assert(!AppleAgxSchedulerCommitBoundaryPreemption(&a.Scheduler,77));
 assert(a.Scheduler.PendingPreemptionFence==78 &&
        a.Scheduler.PreemptionPhase==AppleAgxPreemptionReadyToNotify);
 /* The same fence again is not a new preemption. */
 setup(&a);
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&a.Scheduler,0,0,77,0x732,0));
 assert(AppleAgxSchedulerClaimBoundaryPreemption(&a.Scheduler,&claim));
 a.Scheduler.DeliveredPreemptionFence=77;
 assert(!AppleAgxSchedulerBeginBoundaryPreemption(&a.Scheduler,0,0,77,0x732,0));
 assert(a.Scheduler.PreemptionPhase==AppleAgxPreemptionNotificationClaimed);
 return 0;
}
'''


class PreemptNextCommandBeforeCommit(unittest.TestCase):
    def test_next_preemption_inside_delivery_window_is_not_a_fault(self):
        source = Path(os.environ.get('AGX_PREEMPT_SCHEDULER_SOURCE',
            ROOT / 'drivers/apple-agx/render-admission/src/scheduler_windows.c')).read_text()
        scheduler = Path(os.environ.get('AGX_SCHEDULER_SOURCE',
            ROOT / 'drivers/apple-agx/shared/src/apple_agx_scheduler.c'))
        body = '\n'.join(function_body(source, name) for name in (
            'AdmissionNotifyPreemptionAtInterrupt',
            'AdmissionSchedulerTryNotifyPreemption',
            'AdmissionSchedulerWorkerFinished',
            'AdmissionDdiPreemptCommand'))
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'next.c').write_text(SHIM + body + CASES)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-Wno-unused-function',
                '-fsanitize=address,undefined', '-I',
                str(Path(os.environ.get('AGX_SCHEDULER_INCLUDE',
                    ROOT / 'drivers/apple-agx/shared/include'))), str(p / 'next.c'),
                str(scheduler), '-o', str(p / 'next')], check=True)
            subprocess.run([str(p / 'next')], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
