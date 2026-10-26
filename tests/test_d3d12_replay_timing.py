"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Dispatch phase timers must partition owner wall time, including allocation
failure and indirect rollover, without adding a wait or per-dispatch logging.
DDI command buckets must keep queries/copies separate from draws/dispatches.
"""
import unittest
from test_d3d12_owner_allocation_failure import OWNER, ROOT, function

class ReplayTiming(unittest.TestCase):
    def test_owner_phases_and_aggregate(self):
        s=OWNER.read_text()
        self.assertIn('struct DispatchTiming',s,'Dispatch costs are not split')
        dispatch=function(s,'extern "C" HRESULT AgxD3d12OwnerDispatch(')
        self.assertIn('DispatchTiming timing(owner);',dispatch)
        for phase in ['Bind','Heap','Launch']:
            self.assertIn('timing.Mark(DispatchTiming::'+phase+')',dispatch)
        self.assertNotIn('AdmissionUmdDiagnostic(',dispatch)
        submit=function(s,'static HRESULT submit_owner(')
        self.assertIn('measure-d3d12-dispatch-time',submit)
        self.assertIn('memset(owner->DispatchTicks, 0',submit)
    def test_replay_categories(self):
        s=(ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        self.assertIn('ReplayTimingBucket(',s,'Copies/queries are not timed')
        f=function(s,'static unsigned ReplayTimingBucket(')
        for kind in ['kCommandCopy','kCommandTextureCopy','kCommandTextureRegionCopy','kCommandResolve','kCommandQuery','kCommandDispatch','kCommandDraw','kCommandIndirectExpand']:
            self.assertIn(kind,f)
        replay=function(s,'static void ReplayAndSubmit(')
        self.assertIn('measure-d3d12-replay-time',replay)
        self.assertIn('command_ticks[ReplayTimingBucket(command.kind)]',replay)

if __name__=='__main__': unittest.main()
