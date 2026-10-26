"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Top-N omission is not zero wait; old captures are unknown, multiple Executes
must accumulate, and per-site medians must exclude each process's first frame.
Query Begin/End/Resolve must remain separate, including interleaved Executes.
"""
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('analyzer',ROOT/'tools/agx_submission_analyze.py')
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
def row(tag,q,words,pid=7,tid=1):
    return f'{tag} hr=0x00000000 pid={pid} tid={tid} session=0 qpc={q} '+ ' '.join(f'{x:08x}' for x in words)
def waits(serial,site,us,omitted=0,flags=0):
    return [1,serial,omitted,flags,site,1,us,0,0,0,0,0,0,0,0,0]

class WaitAnalyzer(unittest.TestCase):
    def test_multiple_executes_censoring_and_queries(self):
        site=3*256+3 # Resolve / render fence
        log=[row('start',0,[]),row('measure-d3d12-present',10,[]),
             row('measure-d3d12-execute',20,[1,9,60000,2000,1]),
             row('measure-d3d12-wait',21,waits(1,site,40000)),
             row('measure-d3d12-query-time',22,[1,1,1,3000,1,200,1,40000]),
             row('measure-d3d12-execute',23,[1,9,10000,2000,2]),
             row('measure-d3d12-wait',24,waits(2,site,10000)),
             row('measure-d3d12-query-time',25,[1,2,2,4000,2,500,2,10000]),
             row('measure-d3d12-present',110,[]),
             row('measure-d3d12-execute',120,[1,9,8000,2000,3]),
             row('measure-d3d12-wait',121,waits(3,1,1000,omitted=5000)),
             row('measure-d3d12-query-time',122,[1,3,1,1000,1,100,1,1000]),
             row('measure-d3d12-present',210,[])]
        frames,summary=a.analyze(log,1000)
        self.assertIn('wait_site_medians',summary,'wait receipts are not analyzed')
        self.assertIsNone(frames[0]['wait_profiled_executes'])
        self.assertEqual(frames[1]['wait_profiled_executes'],2)
        self.assertEqual(frames[1][f'wait_{site}_ms'],50)
        self.assertIsNone(frames[2][f'wait_{site}_ms'])
        self.assertEqual(frames[2][f'wait_{site}_lower_ms'],0)
        self.assertEqual(frames[2]['wait_omitted_ms'],5)
        self.assertEqual(frames[1]['query_begin_ms'],7)
        self.assertAlmostEqual(frames[1]['query_end_ms'],.7)
        self.assertEqual(frames[1]['query_resolve_count'],3)
        self.assertEqual(summary['wait_site_medians'][str(site)]['exact_frames'],1)
        self.assertEqual(summary['wait_site_medians'][str(site)]['median_ms'],50)
        self.assertEqual(summary['wait_site_medians'][str(site)]['median_lower_ms'],25)

    def test_bad_and_partial_receipts_are_not_exact(self):
        log=[row('start',0,[]),row('measure-d3d12-present',1,[]),
             row('measure-d3d12-execute',2,[1,1,1000,0,1]),
             row('measure-d3d12-execute',3,[1,1,1000,0,2]),
             row('measure-d3d12-wait',4,waits(1,3,100)),
             row('measure-d3d12-present',5,[])]
        frames,summary=a.analyze(log,1000)
        self.assertIn('wait_profile_complete',frames[1],'missing Execute coverage is not reported')
        self.assertFalse(frames[1]['wait_profile_complete'])
        self.assertIsNone(frames[1]['wait_3_ms'])
        self.assertIsNone(frames[1]['query_begin_ms'])
        # An old capture remains analyzable, without invented zero waits.
        old,_=a.analyze(log[:3]+[log[-1]],1000)
        self.assertIsNone(old[1]['wait_profiled_executes'])

    def test_delayed_receipts_join_exact_execute_across_present(self):
        log=[row('start',0,[]),row('measure-d3d12-present',1,[]),
             row('measure-d3d12-execute',2,[1,1,1000,0,41]),
             row('measure-d3d12-present',3,[]),
             row('measure-d3d12-execute',4,[1,1,2000,0,42]),
             row('measure-d3d12-wait',5,waits(41,3,1000)),
             row('measure-d3d12-query-time',6,[1,41,1,111,0,0,0,0]),
             row('measure-d3d12-present',7,[]),
             row('measure-d3d12-wait',8,waits(42,3,2000)),
             row('measure-d3d12-query-time',9,[1,42,1,222,0,0,0,0])]
        frames,_=a.analyze(log,1000)
        self.assertEqual(frames[1]['wait_3_ms'],1)
        self.assertEqual(frames[2]['wait_3_ms'],2)
        self.assertAlmostEqual(frames[1]['query_begin_ms'],.111)
        self.assertAlmostEqual(frames[2]['query_begin_ms'],.222)
        missing,_=a.analyze(log[:-2],1000)
        self.assertFalse(missing[2]['wait_profile_complete'])
        self.assertIsNone(missing[2]['wait_3_ms'])

    def test_duplicate_execute_and_orphan_companions_are_not_complete(self):
        log=[row('start',0,[]),row('measure-d3d12-present',1,[]),
             row('measure-d3d12-execute',2,[1,1,1000,0,41]),
             row('measure-d3d12-wait',3,waits(41,3,1000)),
             row('measure-d3d12-present',6,[])]
        with self.assertRaisesRegex(ValueError,'Execute serial'):
            a.analyze(log[:3]+[row('measure-d3d12-execute',3,[1,1,1000,0,41])]+log[3:],1000)
        frames,summary=a.analyze(log[:4]+[row('measure-d3d12-wait',4,waits(42,3,1000))]+log[4:],1000)
        self.assertEqual(summary['wait_orphan_executes'],1)
        self.assertFalse(frames[1]['wait_profile_complete'])
        self.assertIsNone(frames[1]['wait_3_ms'])

    def test_path_overlap_flags_and_top_n_zero_rounding(self):
        site=3*256+10
        log=[row('start',0,[]),row('measure-d3d12-present',1,[]),
             row('measure-d3d12-execute',2,[1,1,1000,0,1]),
             row('measure-d3d12-wait',3,waits(1,3,500,flags=4)),
             row('measure-d3d12-wait-path',4,waits(1,site,800)),
             row('measure-d3d12-present',5,[])]
        frames,summary=a.analyze(log,1000)
        self.assertEqual(frames[1][f'wait_path_{site}_ms'],.8)
        self.assertEqual(summary['wait_path_medians'][str(site)]['median_ms'],.8)
        self.assertEqual(frames[1]['wait_3_ms'],.5)
        bad,_=a.analyze([x.replace('00000004 00000003','00000001 00000003') if 'measure-d3d12-wait ' in x else x for x in log],1000)
        self.assertFalse(bad[1]['wait_profile_complete'])
        self.assertIsNone(bad[1]['wait_3_lower_ms'])

if __name__=='__main__':unittest.main()
