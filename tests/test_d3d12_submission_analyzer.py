"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Late completion receipts belong to their recorded QPC interval, not the frame
which drains them. Overlapping GPU stages must not be double-counted, absent
or invalid samples must stay unknown, and loss/duplicates must be visible.
"""
from pathlib import Path
import importlib.util
import unittest
ROOT=Path(__file__).resolve().parents[1]

class SubmissionAnalyzer(unittest.TestCase):
    def test_late_intervals_and_unknowns(self):
        path=ROOT/'tools/agx_submission_analyze.py'
        self.assertTrue(path.exists(),'No completion-aware analyzer exists')
        spec=importlib.util.spec_from_file_location('analyzer',path)
        a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
        def row(tag,q,words): return f'{tag} hr=0x00000000 pid=7 tid=1 session=0 qpc={q} '+ ' '.join(f'{x:08x}' for x in words)
        log=[row('start',100,[]),row('measure-d3d12-present',200,[]),row('measure-d3d12-present',300,[]),
             row('measure-d3d12-replay-window',300,[110,0,190,0,200,0,1000,0]),
             row('measure-gpu-completion',320,[1,0,123,0,1,2,0,120,0,250,0,1000,0]),
             row('measure-gpu-stages',320,[1,0,1000,0,1100,0,0,0,0,0])]
        frames,summary=a.analyze(log)
        self.assertEqual(len(frames),2)
        self.assertAlmostEqual(frames[0]['outstanding_ms'],80)
        self.assertAlmostEqual(frames[1]['outstanding_ms'],50)
        self.assertAlmostEqual(frames[0]['cpu_outstanding_overlap_ms'],70)
        self.assertAlmostEqual(frames[0]['gpu_busy_estimate_ms'],50)
        self.assertAlmostEqual(frames[1]['gpu_busy_estimate_ms'],50)
        self.assertAlmostEqual(frames[0]['gpu_cpu_overlap_estimate_ms'],40)
        self.assertEqual(summary['completion_records'],1)
        _,dup=a.analyze(log+log[-2:]);self.assertEqual(dup['completion_records'],1)
        unknown,_=a.analyze(log[:4]);self.assertIsNone(unknown[0]['gpu_busy_estimate_ms'])
        _,lost=a.analyze(log+[row('measure-gpu-coverage',330,[76,0,300,0,5,0,0,0,0,39,0])])
        self.assertEqual(lost['overwritten_adapter_records'],5)
        coverage=row('measure-gpu-coverage',330,[76,0,300,0,5,0,0,0,0,39,0])
        _,dedup=a.analyze(log+[coverage,coverage])
        self.assertEqual(dedup['overwritten_adapter_records'],5)
        coverage2=row('measure-gpu-coverage',340,[80,0,304,0,6,0,0,0,0,42,0])
        _,joined=a.analyze(log+[coverage,coverage2])
        self.assertEqual(joined['overwritten_adapter_records'],9)
        _,bad=a.analyze(log[:-1]+[row('measure-gpu-stages',320,[1,0,1200,0,1100,0,0,0,0,0])])
        self.assertEqual(bad['invalid_or_missing_stages'],1)
        with self.assertRaisesRegex(ValueError, 'reset'):
            a.analyze(log+[row('measure-gpu-coverage',330,[1,0,1,0,0,0,1])])
        with self.assertRaisesRegex(ValueError, 'sequence'):
            a.analyze(log+[row('measure-gpu-completion',340,[1,0,456,0,2,2,0,210,0,300,0,1000,0])])
        self.assertEqual(a.union([(1,4),(2,5),(8,9)]),[(1,5),(8,9)])
        self.assertEqual(a.overlap([(1,4),(2,5)],[(3,6)]),2)
        # Two render stages overlap: report their union (80), not sum (120).
        render=[row('start',100,[]),row('measure-d3d12-present',300,[]),
            row('measure-gpu-completion',320,[1,0,123,0,1,1,0,100,0,250,0,1000,0]),
            row('measure-gpu-stages',320,[1,0,1040,0,1100,0,1020,0,1080,0])]
        f,_=a.analyze(render);self.assertAlmostEqual(f[0]['gpu_busy_estimate_ms'],80)

if __name__=='__main__': unittest.main()
