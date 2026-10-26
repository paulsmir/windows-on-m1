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
    def test_observed_execute_medians_include_flagged_profiles_without_inventing_exact_times(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        EXP1972's flagged but joined rows vanished from per-site summaries.
        Raw observations must cover each steady Execute, independently of
        frame sums, without calling invalid/saturated or absent samples exact.
        """
        site=4*256+29
        other=4*256+3
        log=[row('start',0,[]),
             row('measure-d3d12-execute',1,[1,1,999000,0,1]),
             row('measure-d3d12-wait',2,waits(1,site,999000)),
             row('measure-d3d12-present',3,[]),
             row('measure-d3d12-execute',4,[1,1,1000,0,2]),
             row('measure-d3d12-wait',5,waits(2,site,1000)),
             row('measure-d3d12-execute',6,[1,1,70000,0,3]),
             row('measure-d3d12-wait',7,waits(3,site,70000,flags=5)),
             row('measure-d3d12-wait-path',8,waits(3,15,80000,flags=5)),
             row('measure-d3d12-present',9,[]),
             row('measure-d3d12-execute',10,[1,1,90000,0,4]),
             row('measure-d3d12-wait',11,waits(4,site,90000,flags=2)),
             row('measure-d3d12-execute',12,[1,1,2000,0,5]),
             row('measure-d3d12-wait',13,waits(5,other,2000,flags=4)),
             row('measure-d3d12-execute',14,[1,1,3000,0,6]),
             row('measure-d3d12-present',15,[]),
             row('measure-d3d12-execute',16,[1,1,50000,0,7]),
             row('measure-d3d12-wait',17,waits(7,site,50000))]
        frames,summary=a.analyze(log,1000,include_executes=True)
        observed=summary['wait_site_medians'][str(site)]
        self.assertIn('observed_execute_median_ms',observed,
                      'flagged per-Execute observations are suppressed by frame validity')
        self.assertEqual(observed['observed_execute_median_ms'],60)
        self.assertEqual(observed['observed_execute_median_count'],1)
        self.assertEqual(observed['steady_executes'],6)
        self.assertEqual(observed['observed_executes'],4)
        self.assertEqual(observed['observed_valid_executes'],2)
        self.assertEqual(observed['observed_invalid_executes'],1)
        self.assertEqual(observed['observed_saturated_executes'],1)
        self.assertEqual(observed['observed_censored_executes'],1)
        self.assertEqual(observed['exact_executes'],2)
        self.assertEqual(observed['absent_executes'],1)
        self.assertEqual(observed['censored_absent_executes'],1)
        self.assertEqual(observed['missing_executes'],1)
        self.assertIsNone(observed['median_ms'])  # both real steady frames are invalid
        self.assertEqual(observed['median_lower_ms'],90)  # saturation retains a lower bound
        self.assertEqual(summary['wait_steady_executes'],6)
        self.assertEqual(summary['wait_steady_joined_executes'],5)
        self.assertEqual(summary['wait_steady_missing_executes'],1)
        self.assertEqual(summary['wait_steady_valid_executes'],3)
        self.assertEqual(summary['wait_steady_invalid_executes'],2)
        self.assertEqual(summary['wait_steady_censored_executes'],2)
        self.assertEqual(summary['wait_steady_unpresented_executes'],1)
        executions=summary['execute_rows']
        self.assertFalse(executions[0]['steady'])
        invalid=executions[2]
        self.assertIsNone(invalid[f'wait_{site}_ms'])
        self.assertIsNone(invalid[f'wait_{site}_lower_ms'])
        self.assertEqual(invalid[f'wait_{site}_observed_ms'],70)
        self.assertEqual(invalid[f'wait_{site}_observed_count'],1)
        self.assertIsNone(executions[4][f'wait_{site}_observed_ms'])
        self.assertIsNone(executions[5][f'wait_{site}_observed_ms'])
        self.assertTrue(executions[-1]['steady'])
        self.assertIsNone(executions[-1]['frame'])
        path=summary['wait_path_medians']['15']
        self.assertEqual(path['observed_execute_median_ms'],80)
        self.assertEqual(path['observed_invalid_executes'],1)
        self.assertIsNone(path['median_ms'])
        self.assertIn('not exact times or lower bounds',summary['wait_coverage'])

    def test_observed_samples_exclude_each_process_startup_and_unknown_absences(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A later PID's startup/tail without a Present must not bias steady
        medians, and a known zero absent site must not become a raw observation.
        """
        log=[row('start',0,[]),row('measure-d3d12-present',1,[]),
             row('measure-d3d12-execute',2,[1,1,10000,0,1]),
             row('measure-d3d12-wait',3,waits(1,3,10000)),
             row('measure-d3d12-execute',4,[1,1,20000,0,2]),
             row('measure-d3d12-wait',5,waits(2,8,20000)),
             row('measure-d3d12-present',6,[]),
             row('measure-d3d12-execute',10,[1,1,900000,0,1],pid=8),
             row('measure-d3d12-wait',11,waits(1,3,900000),pid=8),
             row('measure-d3d12-present',12,[],pid=8),
             row('measure-d3d12-execute',13,[1,1,30000,0,2],pid=8),
             row('measure-d3d12-wait',14,waits(2,3,30000),pid=8),
             row('measure-d3d12-execute',20,[1,1,800000,0,1],pid=9),
             row('measure-d3d12-wait',21,waits(1,3,800000),pid=9)]
        _,summary=a.analyze(log,1000,include_executes=True)
        observed=summary['wait_site_medians']['3']
        self.assertIn('observed_execute_median_ms',observed,
                      'per-Execute sample population has no independent accounting')
        self.assertEqual(observed['observed_execute_median_ms'],20)
        self.assertEqual(observed['observed_executes'],2)
        self.assertEqual(observed['exact_executes'],3)
        self.assertEqual(observed['absent_executes'],1)
        self.assertEqual(observed['censored_absent_executes'],0)
        self.assertEqual(observed['steady_executes'],3)
        self.assertEqual([e['steady'] for e in summary['execute_rows']],
                         [True,True,False,True,False])

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
        self.assertEqual(executes[1]['wait_29_lower_ms'],'')
        self.assertEqual(executes[1]['wait_29_observed_ms'],'1074.363')
        self.assertEqual(executes[1]['wait_29_observed_count'],'12507')
        self.assertEqual(executes[-1]['steady'],'True')

    @unittest.skipUnless(os.environ.get('AGX_EXP1972_UMD_LOG'), 'optional full EXP1972 capture')
    def test_exp1972_observed_medians_cover_flagged_steady_executes(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The real EXP1972 capture has 543 invalid profiles that hid the dominant
        readiness costs. Every steady Execute must be counted, but raw flagged
        observations must stay distinct from the unchanged valid frame medians.
        """
        _,summary=a.analyze(Path(os.environ['AGX_EXP1972_UMD_LOG']).read_text().splitlines(),
                            include_executes=True)
        self.assertEqual(summary['execute_records'],614)
        self.assertEqual(summary['wait_steady_executes'],613)
        self.assertEqual(summary['wait_steady_joined_executes'],613)
        self.assertEqual(summary['wait_steady_invalid_executes'],543)
        self.assertEqual(summary['wait_steady_unpresented_executes'],4)
        self.assertEqual(summary['wait_complete_frames'],51)
        draw=summary['wait_site_medians']['1053']
        self.assertAlmostEqual(draw['observed_execute_median_ms'],71.079)
        self.assertEqual(draw['observed_execute_median_count'],15)
        self.assertEqual(draw['observed_executes'],552)
        self.assertEqual(draw['observed_invalid_executes'],541)
        self.assertEqual(draw['absent_executes'],61)
        self.assertEqual(draw['missing_executes'],0)
        self.assertAlmostEqual(draw['median_ms'],.206)
        resolve=summary['wait_site_medians']['797']
        self.assertEqual(resolve['observed_execute_median_ms'],69.419)
        self.assertEqual(resolve['observed_executes'],541)
        self.assertEqual(resolve['observed_invalid_executes'],541)
        self.assertIsNone(resolve['median_ms'])
        for e in summary['execute_rows']:
            if e['wait_flags'] & 1:
                self.assertIsNone(e['wait_1053_ms'])
                self.assertIsNone(e['wait_1053_lower_ms'])

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
