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

    def test_empty_progress_and_decision_attribution(self):
        spec=importlib.util.spec_from_file_location('rc',TOOL)
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        empty=struct.pack('<4I',1,18448,0,0)+bytes(18432)
        self.assertEqual(m.decode(empty,42)['sample_state'],'no sample')
        self.assertTrue(hasattr(m,'decode_decisions'))
        size=20+72*4+128*40
        def wire(records,overwritten=0,next_index=None):
            hist=[0]*72
            for r in records: hist[r[0]]+=r[5]
            return (struct.pack('<5I',1,size,len(records),len(records) if next_index is None else next_index,overwritten)+
                    struct.pack('<72I',*hist)+b''.join(struct.pack('<10I',*r) for r in records)+bytes((128-len(records))*40))
        # Same-process warmup excluded; overlapping interval explicitly partial.
        r=[[66,0,99,0,42,5,10,0,20,0],[68,29,101,102,42,3,100,0,120,0],
           [65,0,201,202,43,1,110,0,110,0]]
        out=m.decode_decisions(wire(r),42,105,130)
        self.assertEqual(len(out['decisions']),1)
        self.assertEqual(out['decisions'][0]['render_fence'],101)
        self.assertEqual(out['decisions'][0]['detail'],29)
        self.assertTrue(out['decisions'][0]['partial_interval'])
        self.assertEqual(out['histogram']['66']['count'],5)
        with self.assertRaises(ValueError):m.decode_decisions(wire(r)[:-1],42)
        with self.assertRaises(ValueError):m.decode_decisions(wire(r,next_index=128),42)
        full=[[66,0,i,0,42,1,i,0,i,0] for i in range(128)]
        wrapped=m.decode_decisions(wire(full,5,5),42)
        self.assertEqual(wrapped['decisions'][0]['render_fence'],5)
        self.assertEqual(wrapped['decisions'][-1]['render_fence'],4)
        self.assertEqual(wrapped['overwritten'],5)

    def test_typed_pair_classes_and_stamp_domains(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        R/C evidence must not certify TA or prelude waits by reusing the old
        render/compute stamp meanings, or combine observations across classes.
        """
        spec=importlib.util.spec_from_file_location('rc',TOOL)
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        def sample(running, successor, barrier, observed):
            s=dict(Stage=4,RFence=99,CFence=100,ExpectedR=0x200,ExpectedC=0x300,
                ProcessId=42,Valid=8191,RFlight=1,CFlight=2,CFlags=3,
                WaitLow=4096,WaitValue=0x200,EventCount=5,PreviousThreshold=4,
                RunningKind=running,NextKind=successor,Barrier=barrier,
                Words=[5,0x100,observed,0,0,0,0,0,0,0,0,0x100,5])
            return s
        def wire(samples):
            packed=b''.join(struct.pack('<39I',*[s.get(n,0) for n in m.FIELDS],
                *s['Words'],s['RunningKind'],s['NextKind'],s['Barrier']) for s in samples)
            return struct.pack('<4I',2,19984,len(samples),0)+packed+bytes(19968-len(packed))
        for running,next_kind,barrier in [(1,3,3),(3,1,1),(1,1,2),(2,1,2),
                                         (1,2,3),(3,2,4),(3,3,4),(2,2,3)]:
            before=sample(running,next_kind,barrier,0x100)
            after=sample(running,next_kind,barrier,0x200)
            out=m.decode(wire([before,after]),42)['pairs'][0]
            self.assertEqual(out['evidence'],'published-progress')
            self.assertEqual(out['running_kind'],running)
            self.assertEqual(out['next_kind'],next_kind)
            self.assertEqual(out['barrier'],barrier)
            self.assertIn('successor_first_fw_before',out['samples'][0]['values'])
            for changed in [dict(after,Barrier=0),dict(after,RunningKind=0),
                            dict(after,CFlight=0),dict(after,CFlight=1),dict(after,RFlight=2),
                            dict(after,CFlight=3),dict(after,RFlight=3),dict(after,WaitValue=0x100)]:
                self.assertTrue(all(p['evidence']=='inconclusive'
                    for p in m.decode(wire([before,changed]),42)['pairs']))
            moved=after['Words'][:];moved[11]=0x300
            out=m.decode(wire([before,dict(after,Words=moved)]),42)
            self.assertEqual(out['pairs'][0]['evidence'],'inconclusive')
        for flights in [(1,1),(2,2),(0,2),(1,0),(3,2),(1,3)]:
            invalid_before=dict(sample(1,1,2,0x100),RFlight=flights[0],CFlight=flights[1])
            invalid_after=dict(sample(1,1,2,0x200),RFlight=flights[0],CFlight=flights[1])
            self.assertEqual(m.decode(wire([invalid_before,invalid_after]),42)['pairs'][0]['evidence'],
                             'inconclusive')
        # A distinct exact flight or emitted barrier address starts a new domain.
        a=sample(1,1,2,0x100);b=sample(1,1,2,0x200)
        for changed in [dict(b,CFlight=3),dict(b,RFlight=3),dict(b,WaitLow=8192)]:
            self.assertEqual(m.decode(wire([a,changed]),42)['pairs'][0]['evidence'],'inconclusive')

    def test_typed_decisions_preserve_pair_identity(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Admission diagnostics must retain actual classes instead of labelling
        every predecessor render and every successor compute.
        """
        spec=importlib.util.spec_from_file_location('rc',TOOL)
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        size=20+72*4+128*52
        hist=[0]*72;hist[0]=1
        record=[0,0,99,100,42,1,10,0,10,0,3,1,1]
        data=(struct.pack('<5I',2,size,1,1,0)+struct.pack('<72I',*hist)+
              struct.pack('<13I',*record)+bytes(127*52))
        out=m.decode_decisions(data,42)['decisions'][0]
        self.assertEqual((out['running_kind'],out['next_kind'],out['barrier']),(3,1,1))
        self.assertEqual((out['predecessor_fence'],out['successor_fence']),(99,100))

    def test_chained_pairs_join_each_original_terminal_once(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Adopting C as the head of C/R replaces RcCurrent before C's terminal;
        the earlier R/C pair must still join C, without hiding duplicate reports.
        """
        spec=importlib.util.spec_from_file_location('rc',TOOL)
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        def sample(rf,cf,running,next_kind,barrier,**extra):
            return dict(Stage=3,RFence=rf,CFence=cf,ExpectedR=rf*256,
                RunningKind=running,NextKind=next_kind,Barrier=barrier,
                ProcessId=42,QpcLow=100,Words=[0]*13,**extra)
        first=sample(99,100,1,3,3)
        second=sample(100,101,3,1,1)
        r_done=dict(first,Stage=6,TerminalFence=99,Status=0)
        c_done=dict(second,Stage=6,TerminalFence=100,Status=0)
        last_done=dict(second,Stage=6,TerminalFence=101,Status=0)
        def wire(samples):
            packed=b''.join(struct.pack('<39I',*[s.get(n,0) for n in m.FIELDS],
                *s['Words'],s['RunningKind'],s['NextKind'],s['Barrier']) for s in samples)
            return struct.pack('<4I',2,19984,len(samples),0)+packed+bytes(19968-len(packed))
        def decode(samples):
            return m.decode(wire(samples),42,90,110)['pairs']
        samples=[first,r_done,second,c_done,last_done]
        pairs=decode(samples)
        self.assertEqual([p['exact_report_order'] for p in pairs],[True,True])
        self.assertEqual([[r['fence'] for r in p['reported']] for p in pairs],
                         [[99,100],[100,101]])
        for altered in [samples+[c_done], [first,second,c_done,last_done],
                        [first,r_done,second,last_done],
                        [first,r_done,second,dict(c_done,ProcessId=43),last_done],
                        [first,r_done,second,dict(c_done,QpcLow=111),last_done],
                        [first,second,c_done,r_done,last_done]]:
            self.assertFalse(decode(altered)[0]['exact_report_order'])
        # A stale matching terminal before pair capture is not part of the pair.
        stale=dict(c_done,RFence=97,CFence=98,ExpectedR=97*256)
        self.assertTrue(all(p['exact_report_order'] for p in decode([stale]+samples)[1:]))
        failed=decode([first,r_done,second,dict(c_done,Status=5),last_done])
        self.assertEqual(failed[0]['reported'][-1]['status'],5)
        self.assertEqual(failed[1]['reported'][0]['status'],5)

    def test_v3_idle_ring_is_independent_and_both_rings_are_decoded(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The v3 reserved ring must expose idle observations/overwrites without
        obscuring retained probe decisions or misreading archived v2 bytes.
        """
        spec=importlib.util.spec_from_file_location('rc',TOOL)
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        hist=[0]*72;hist[64]=1;hist[66]=30000
        probe=[64,7,99,100,42,1,10,0,10,0,3,1,1]
        idle=[66,0,300,0,43,30000,11,0,30010,0,0,0,0]
        data=(struct.pack('<5I',3,8640,1,1,0)+struct.pack('<72I',*hist)+
              struct.pack('<13I',*probe)+bytes(127*52)+struct.pack('<3I',1,1,0)+
              struct.pack('<13I',*idle)+bytes(31*52))
        out=m.decode_decisions(data,42,5,15)
        self.assertEqual(len(out['decisions']),1)
        self.assertEqual(out['decisions'][0]['reason'],64)
        self.assertEqual(out['histogram']['66']['count'],30000)
        self.assertEqual(out['histogram']['66']['last_retained']['process_id'],43)
        self.assertEqual(out['idle_overwritten'],0)
        self.assertEqual(m.decode_decisions(data,43,20,30)['decisions'][0]['count'],30000)
        for offset,value in [(6964,33),(6968,32),(6972,1),(6976,64),(308,66)]:
            bad=bytearray(data);struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):m.decode_decisions(bad,42)

    def test_v4_real_producer_retains_fence_endpoints_and_partial_counts(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A v4 aggregate spans changing fences and interleaved observations.
        Decode actual C wire bytes without claiming an exact-pair history or
        attributing the entire count to a narrower QPC filter; keep v1-v3 ABI.
        """
        import subprocess
        import tempfile
        spec=importlib.util.spec_from_file_location('rc',TOOL)
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'t.c';exe=Path(d)/'t'
            c.write_text(r'''#include "render_rc_progress.h"
#include <stdio.h>
int main(void) {
 ADMISSION_RC_DECISIONS r={0};AdmissionRcDecisionsInitialize(&r);
 AdmissionRcDecisionTyped(&r,0,0,99,100,42,10,3,1,1);
 AdmissionRcDecisionTyped(&r,64,0,200,201,123,11,1,1,2);
 AdmissionRcDecisionTyped(&r,0,0,101,102,42,20,3,1,1);
 AdmissionRcDecision(&r,66,0,300,0,123,21);
 AdmissionRcDecision(&r,66,0,301,0,123,22);
 return fwrite(&r,1,sizeof(r),stdout)==sizeof(r) ? 0:1;
}
''')
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-Wconversion','-ftrivial-auto-var-init=pattern','-I',
                str(ROOT/'drivers/apple-agx/render-admission/include'),str(c),
                '-o',str(exe)],check=True)
            data=subprocess.check_output([str(exe)])
        out=m.decode_decisions(data,42,15,25)
        self.assertEqual(out['version'],4)
        self.assertEqual(len(out['decisions']),1)
        r=out['decisions'][0]
        self.assertEqual((r['first_predecessor_fence'],r['first_successor_fence']), (99,100))
        self.assertEqual((r['last_predecessor_fence'],r['last_successor_fence']), (101,102))
        self.assertEqual((r['count'],r['first_qpc'],r['last_qpc'],r['partial_interval']), (2,10,20,True))
        self.assertEqual(m.decode_decisions(data,42,21,25)['decisions'],[])
        idle=m.decode_decisions(data,123)['decisions'][-1]
        self.assertEqual((idle['first_predecessor_fence'],idle['last_predecessor_fence'],idle['count']), (300,301,2))
        for offset,value in [(7988,33),(7992,32),(7996,1),(8000,64),(308,66)]:
            bad=bytearray(data);struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):m.decode_decisions(bad,42)
