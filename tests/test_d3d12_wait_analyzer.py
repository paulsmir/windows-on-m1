"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Top-N omission is not zero wait; old captures are unknown, multiple Executes
must accumulate, and per-site medians must exclude each process's first frame.
Query Begin/End/Resolve must remain separate, including interleaved Executes.
"""
import importlib.util
import os
from pathlib import Path
import csv
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('analyzer',ROOT/'tools/agx_submission_analyze.py')
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
def row(tag,q,words,pid=7,tid=1):
    return f'{tag} hr=0x00000000 pid={pid} tid={tid} session=0 qpc={q} '+ ' '.join(f'{x:08x}' for x in words)
def waits(serial,site,us,omitted=0,flags=0):
    return [1,serial,omitted,flags,site,1,us,0,0,0,0,0,0,0,0,0]

class WaitAnalyzer(unittest.TestCase):
    def test_exp1970_counts_invalid_and_unpresented_executes(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        EXP1970's valid-frame count hid joined invalid Executes and silently
        dropped four trailing Executes; counting them must not invent timing.
        """
        log=(ROOT/'tests/fixtures/exp1970_wait_excerpt.txt').read_text().splitlines()
        frames,summary=a.analyze(log)
        self.assertEqual(len(frames),3)
        self.assertIn('execute_records',summary,'joined and trailing Executes are not accounted')
        self.assertEqual(summary['execute_records'],7)
        self.assertEqual(summary['execute_framed_records'],3)
        self.assertEqual(summary['execute_unpresented_records'],4)
        self.assertEqual(summary['wait_joined_executes'],7)
        self.assertEqual(summary['wait_missing_executes'],0)
        self.assertEqual(summary['wait_valid_executes'],6)
        self.assertEqual(summary['wait_invalid_executes'],1)
        self.assertEqual(summary['wait_censored_executes'],2)
        self.assertEqual(summary['wait_unpresented_executes'],4)
        self.assertEqual(summary['wait_joined_frames'],2)
        self.assertEqual(summary['wait_complete_frames'],1)
        _,detailed=a.analyze(log,include_executes=True)
        executes=detailed['execute_rows']
        self.assertEqual([e['serial'] for e in executes],[42,43,819,820,821,822,823])
        self.assertEqual([e['frame'] for e in executes],[1,2,3,None,None,None,None])
        invalid=executes[1]
        self.assertTrue(invalid['wait_join_complete'])
        self.assertEqual(invalid['wait_flags'],5)
        self.assertFalse(invalid['wait_profile_complete'])
        self.assertIsNone(invalid['wait_29_ms'])
        self.assertIsNone(invalid['wait_29_lower_ms'])
        self.assertIsNone(invalid['query_resolve_ms'])
        self.assertEqual(executes[-1]['wait_22_ms'],0)
        self.assertNotIn('wall_ms',executes[-1])

    def test_execute_export_preserves_missing_censored_and_duplicate_receipts(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Execute accounting must not turn missing/top-N sites into exact zeroes
        or count a repeated identical companion as another Execute.
        """
        receipt=row('measure-d3d12-wait',4,waits(42,3,1000,flags=4))
        log=[row('start',0,[]),row('measure-d3d12-execute',1,[1,1,1000,0,41]),
             row('measure-d3d12-present',2,[]),
             row('measure-d3d12-execute',3,[1,1,2000,0,42]),receipt,receipt,
             row('measure-d3d12-execute',5,[1,1,3000,0,43]),
             row('measure-d3d12-wait',6,waits(43,8,2000))]
        frames,summary=a.analyze(log,1000)
        self.assertEqual(len(frames),1)
        self.assertIn('execute_records',summary,'unpresented Executes have no coverage report')
        self.assertEqual(summary['execute_records'],3)
        self.assertEqual(summary['wait_joined_executes'],2)
        self.assertEqual(summary['wait_missing_executes'],1)
        self.assertEqual(summary['wait_duplicate_receipts'],1)
        self.assertEqual(summary['wait_receipts'],2)
        _,detailed=a.analyze(log,1000,include_executes=True)
        missing,censored,valid=detailed['execute_rows']
        self.assertFalse(missing['wait_join_complete'])
        self.assertIsNone(missing['wait_3_ms'])
        self.assertIsNone(missing['wait_3_lower_ms'])
        self.assertIsNone(censored['wait_8_ms'])
        self.assertEqual(censored['wait_8_lower_ms'],0)
        self.assertEqual(valid['wait_8_ms'],2)

    def test_execute_csv_includes_tail_without_fabricated_frame(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The CLI must actually export post-Present Executes with their serial
        and empty frame association, rather than silently omit them.
        """
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'executes.csv'
            result=subprocess.run([sys.executable,str(ROOT/'tools/agx_submission_analyze.py'),
                str(ROOT/'tests/fixtures/exp1970_wait_excerpt.txt'),
                '--execute-csv',str(output)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            with output.open() as stream:
                executes=list(csv.DictReader(stream))
        self.assertEqual(len(executes),7)
        self.assertEqual(executes[-1]['serial'],'823')
        self.assertEqual(executes[-1]['frame'],'')
        self.assertEqual(executes[1]['wait_29_ms'],'')

    @unittest.skipUnless(os.environ.get('AGX_EXP1970_UMD_LOG'), 'optional full EXP1970 capture')
    def test_exp1970_full_capture(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        All 823 saved EXP1970 Executes must reconcile, including 776 invalid
        profiles and four post-Present records, without accepting bad timings.
        """
        frames,summary=a.analyze(Path(os.environ['AGX_EXP1970_UMD_LOG']).read_text().splitlines(),
                                 include_executes=True)
        self.assertEqual(len(frames),812)
        self.assertEqual(summary['execute_records'],823)
        self.assertEqual(summary['execute_framed_records'],819)
        self.assertEqual(summary['execute_unpresented_records'],4)
        self.assertEqual(summary['wait_receipts'],823)
        self.assertEqual(summary['wait_joined_executes'],823)
        self.assertEqual(summary['wait_missing_executes'],0)
        self.assertEqual(summary['wait_valid_executes'],47)
        self.assertEqual(summary['wait_invalid_executes'],776)
        self.assertEqual(summary['wait_censored_executes'],818)
        self.assertEqual(summary['wait_unpresented_executes'],4)
        self.assertEqual(summary['wait_duplicate_receipts'],0)
        self.assertEqual(summary['wait_orphan_executes'],0)
        self.assertEqual(summary['wait_joined_frames'],811)
        self.assertEqual(summary['wait_complete_frames'],35)
        self.assertEqual(len(summary['execute_rows']),823)
        self.assertEqual(sum(e['query_resolve_ms'] is None for e in summary['execute_rows']),776)

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
