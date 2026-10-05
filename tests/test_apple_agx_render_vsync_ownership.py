"""Internal D589 ingress remains live while OS notifications are disabled."""
import unittest
from test_scanout_vsync_replay import compile_and_run

class VsyncOwnershipTests(unittest.TestCase):
    def test_initial_modeset_and_disabled_notifications_still_retire_latches(self):
        compile_and_run(r"""
int main(void) {
 ADMISSION_CONTEXT c; ADMISSION_SCANOUT_RUNTIME r;
 setup(&c,&r);assert(finish_start(&c,&r)==0&&r.IrqEnabled);
 assert(AdmissionScanoutControlInterrupt(&c,0)==0);
 pending(&r,2,0x1500000000ULL);assert(AdmissionScanoutInterrupt(&c));
 assert(!r.PendingValid&&!r.PresentGate&&!r.Panel.Scanout.PresentPending&&!notifications&&!irq&&!r.Faulted);
 fakeIrql=2;AdmissionDdiDpcRoutine(&c);AdmissionScanoutTimerDpc(&r.VsyncDpc,&r,0,0);fakeIrql=0;
 assert(!AdmissionScanoutInterrupt(&c));
 assert(AdmissionScanoutControlInterrupt(&c,1)==0);
 pending(&r,3,0x1500010000ULL);assert(AdmissionScanoutInterrupt(&c));
 assert(notifications==1&&last_address==0x1500010000ULL);
 assert(AdmissionScanoutControlInterrupt(&c,0)==0);
 pending(&r,4,0x1500020000ULL);assert(AdmissionScanoutInterrupt(&c));
 assert(notifications==1&&!r.PendingValid&&!r.PresentGate);
 pending(&r,5,0x1500030000ULL);latch=4;assert(AdmissionScanoutInterrupt(&c));
 assert(!r.Faulted&&r.PendingValid&&notifications==1&&r.Timeline.ActiveSequence==4);
 return 0;
}
""")
