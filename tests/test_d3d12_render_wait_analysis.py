"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Inclusive InflightSlot/Flush costs overlap exclusive RenderFence. Adding them
or interpreting a missing top-four entry as zero invents wait attribution.
The bound must join one Execute and one phase and leave omitted time unknown.
"""
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import agx_render_wait_analyze as analysis

def record(items, omitted=0):
    words=[1,7,omitted,4 if omitted else 0]
    for key,count,us in items:words += [key,count,us]
    return words+[0]*(16-len(words))

class RenderWaitAnalysis(unittest.TestCase):
    def test_nested_wait_is_bounded_not_added(self):
        wait=record([(0x403,2,900),(0x40f,8,100),(0x303,1,500)],100)
        path=record([(0x40f,8,1100),(0x40a,2,800)])
        r=analysis.slot_bound(wait,path,4)
        self.assertEqual(r['render_calls'],2)
        self.assertEqual(r['slot_calls'],2)
        self.assertEqual(r['render_in_slot_lower_us'],595)
        self.assertEqual(r['render_in_slot_upper_us'],801)
        self.assertEqual(r['render_lower_us'],900)

    def test_missing_path_is_censored_and_phase_specific(self):
        wait=record([(0x403,2,500),(0x40f,1,80),(0x303,1,700),(0x603,1,90)],200)
        path=record([(0x318,4,700),(0x30a,3,600),(0x60f,1,90),(0x40f,1,80)],60)
        r=analysis.slot_bound(wait,path,4)
        self.assertEqual(r['slot_calls'],0)  # observed, not true total
        self.assertEqual(r['slot_upper_us'],61)
        self.assertEqual(r['render_in_slot_lower_us'],0)
        self.assertEqual(r['render_in_slot_upper_us'],61)

    def test_parser_joins_thread_serial_and_counts_delta_field(self):
        def line(tag, words, thread=2):
            return tag+' hr=0 pid=1 tid='+str(thread)+' qpc=150 '+ ' '.join(f'{v:08x}' for v in words)+'\n'
        wait=record([(0x403,2,900),(0x40f,1,100)])
        path=record([(0x40a,2,800)])
        lines=[line('measure-d3d12-execute',[0,0,0,0,7]),
               line('measure-d3d12-replay-window',[110,0,140,0,150,0,1000,0]),
               line('measure-d3d12-wait',wait),line('measure-d3d12-wait-path',path),
               line('measure-d3d12-query-time',[1,7,3,10,3,1,3,100]),
               line('measure-perf-reason',[1,5,999,100,0x0078797a])]
        summary=dict(pid=1,first_qpc=100,last_qpc=200,qpc_hz=1000,frames=1,executes=1)
        r=analysis.analyze(lines,summary)
        self.assertEqual(r['phases']['draw']['totals']['render_calls'],2)
        self.assertEqual(r['query_commands']['resolve_count'],3)
        self.assertEqual(r['perf_reason_deltas_reported_in_window'],{'zyx':5})
        wrong=lines.copy();wrong[3]=line('measure-d3d12-wait-path',path,thread=3)
        with self.assertRaises(ValueError):analysis.analyze(wrong,summary)

    def test_bad_profiler_flags_rejected(self):
        wait=record([(0x403,1,50)]);wait[3]=1
        with self.assertRaises(ValueError):analysis.slot_bound(wait,record([]),4)

if __name__=='__main__':unittest.main()
