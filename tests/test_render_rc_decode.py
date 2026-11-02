"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Correct output or driver stamps alone must never be reported as firmware R/C
overlap. A truncated, stale, overflowing, moving or completed-C sample must be
inconclusive; require a preceding below-target FW sample for the same pair/PID.
"""
import importlib.util
from pathlib import Path
import struct
import unittest
ROOT=Path(__file__).resolve().parents[1]
TOOL=ROOT/'tools/agx_rc_progress.py'

class RcDecode(unittest.TestCase):
    def test_correlated_progress_and_negative_controls(self):
        self.assertTrue(TOOL.exists())
        spec=importlib.util.spec_from_file_location('rc',TOOL)
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        start=dict(Stage=1,RFence=99,CFence=100,ExpectedR=0xffffff00,ProcessId=42,
                   Valid=8191,Words=[4,0x200,0xfffffe00,0x100,0,0,0,0,0,0,0,0x200,4])
        end=dict(start,Stage=3,ExpectedC=0x300,EventCount=5,PreviousThreshold=4,
                 RFlight=1,CFlight=2,CFlags=3,WaitValue=0xffffff00,WaitLow=4096,
                 Words=[5,0x200,0x100,0,0,0,0,0,0,0,0,0x200,5])
        def wire(samples,overflow=0):
            packed=b''.join(struct.pack('<36I',*[s.get(n,0) for n in m.FIELDS],*s['Words']) for s in samples)
            return struct.pack('<4I',1,18448,len(samples),overflow)+packed+bytes(18432-len(packed))
        def verdict(samples,overflow=0):
            return m.decode(wire(samples,overflow),42)['pairs'][0]['evidence']
        committed = dict(end, Words=[5,0x200,0xfffffe00,0,0,0,0,0,0,0,0,0x200,5])
        staged = dict(end, Stage=2, CFlags=0, Words=[4,0x200,0x100,0,0,0,0,0,0,0,0,0x200,4])
        self.assertEqual(verdict([start,end]),'inconclusive')
        self.assertEqual(verdict([start,staged,end]),'inconclusive')
        staged_before = dict(staged, Words=[4,0x200,0xfffffe00,0,0,0,0,0,0,0,0,0x200,4])
        self.assertEqual(verdict([start,staged_before,staged,end]),'staged-progress')
        self.assertEqual(verdict([committed,end]),'published-progress')
        self.assertEqual(verdict([end]),'inconclusive')
        self.assertEqual(verdict([dict(start,Words=end['Words']),end]),'inconclusive')
        for key,value in [('Valid',8190),('CFlags',1),('WaitValue',123),('CFlight',0)]:
            self.assertEqual(verdict([committed,dict(end,**{key:value})]),'inconclusive')
        for index,value in [(2,0xfffffe00),(11,0x300),(12,6)]:
            words=end['Words'][:];words[index]=value
            self.assertEqual(verdict([committed,dict(end,Words=words)]),'inconclusive')
        self.assertEqual(verdict([committed,end],1),'inconclusive')
        self.assertEqual(m.decode(wire([start,end]),43)['pairs'],[])
        with self.assertRaises(ValueError):m.decode(wire([start,end])[:-1],42)
        with self.assertRaises(ValueError):m.decode(bytes(18448),42)
        # An earlier warmup in the same process cannot prove the measured pair.
        warm1=dict(committed, QpcLow=10);warm2=dict(end, QpcLow=20)
        measured1=dict(start, RFence=101, CFence=102, QpcLow=110)
        measured2=dict(end, RFence=101, CFence=102, QpcLow=120)
        only=m.decode(wire([warm1,warm2,measured1,measured2]),42,100,130)
        self.assertEqual(len(only['pairs']),1)
        self.assertEqual(only['pairs'][0]['evidence'],'inconclusive')
