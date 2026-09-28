"""Offline counterexamples for EXP859 guard53; no production source edits.

Uses the existing real-body KMD/graph/wire/m1n1 replay. Only the scenario is
changed: omit its artificial root bind, then deliver the real SetRoot DDI.
This demonstrates possible causes, not the unidentified hardware request.
"""
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests"))
import g3_vidmm_replay as replay

original_generate = replay.generate

PROBE = r'''
  /* Paging populated VidMm's root, but the context has not received SetRoot. */
  assert(p->Graph.RootIpa==p->BootstrapIpa && c.GpuvaG3RootIpa==0);
  assert(state.UnpublishedGroups[ADMISSION_MEMORY_LOCAL_SEGMENT]==0);
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(a.G3CopyQueryFailurePredicate==53);
  puts("R146 populated local root before SetRoot: QUERY53");
  DXGKARG_BUILDPAGINGBUFFER flush={0};
  flush.Operation=DXGK_OPERATION_FLUSH_TLB;
  flush.FlushTlb.hProcess=p;
  flush.FlushTlb.RootPageTableAddress.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;
  flush.FlushTlb.RootPageTableAddress.SegmentOffset=0x10000;
  expect_ok("R146 owned inactive root flush",AdmissionGpuvaG3BuildPagingBuffer(&a,&flush));
  assert(last_flush_receipt.Branch==5 && p->Graph.Slot==0);
  assert(p->Graph.RootIpa==p->BootstrapIpa);
  a.G3CopyQueryFailureClaim=0;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(a.G3CopyQueryFailurePredicate==53);
  puts("R146 successful inactive-root FLUSH_TLB: still QUERY53");
  DXGKARG_SETROOTPAGETABLE root={0};
  root.hContext=&c;root.NumEntries=8;
  root.Address.SegmentId=ADMISSION_MEMORY_LOCAL_SEGMENT;
  root.Address.SegmentOffset=0x10000;
  AdmissionDdiSetRootPageTable(&a,&root);
  assert(!p->Poisoned && !c.GpuvaG3Poisoned && c.GpuvaG3RootIpa==p->Graph.RootIpa);
  expect_ok("R146 after real SetRoot DDI",AdmissionDdiEscape(&a,&escape));
  puts("R146 identical publication after SetRoot: QUERY success");
  q->ProcessGeneration=q->MappingGeneration=0;
  /* Full read-only groups remain readable: QUERY has no read-bit predicate. */
  for(UINT i=0;i<16;++i) ptes[i].ReadOnly=1;
  expect_ok("R146 read-only local publication",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));
  expect_ok("R146 read-only QUERY",AdmissionDdiEscape(&a,&escape));
  puts("R146 read-only local full range: QUERY success");
  q->ProcessGeneration=q->MappingGeneration=0;
  for(UINT i=0;i<16;++i) ptes[i].ReadOnly=0;
  expect_ok("R146 restore writable",AdmissionGpuvaG3BuildPagingBuffer(&a,&x));
  /* A second state, with selected root but a different requested VA, has
   * the exact same 16-byte diagnostic. No production acceptance is changed. */
  q->GpuVa=0x20000;a.G3CopyQueryFailureClaim=0;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(a.G3CopyQueryFailurePredicate==53);
  puts("R146 selected root, absent requested VA: QUERY53");
  q->GpuVa=0x10000;
  /* Another independent cause: whole-range QUERY extends beyond publication. */
  allocation.Object.Description.Size=0x10010;a.G3CopyQueryFailureClaim=0;
  assert(AdmissionDdiEscape(&a,&escape)==STATUS_INVALID_PARAMETER);
  assert(a.G3CopyQueryFailurePredicate==53);
  puts("R146 selected root, unpublished allocation tail: QUERY53");
  allocation.Object.Description.Size=0x10000;
  a.G3CopyQueryFailureClaim=0;a.G3CopyQueryFailurePredicate=0;
  a.G3CopyQueryFailureStatus=0;query_registry_writes=0;query_registry_flushes=0;
'''


def generate(*args, **kwargs):
    source = original_generate(*args, **kwargs)
    scenarios = (ROOT / "tests/g3_vidmm_replay_scenarios.c").read_text()
    system = (ROOT / "tests/g3_system_lifetime_cases.c").read_text()
    bind = "  assert(AppleAgxGpuvaG3GraphBindRoot(&p->Graph,s->BrokerIpa));"
    assert system.count(bind) == 1
    system = system.replace(bind, "  /* R146: retain bootstrap until SetRoot DDI. */")
    copy = (ROOT / "tests/g3_r145_copy_cases.c").read_text()
    marker = "  escape.pPrivateDriverData=q;escape.PrivateDriverDataSize=sizeof(*q);"
    assert copy.count(marker) == 1
    copy = copy.replace(marker, marker + "\n" + PROBE)
    scenarios = scenarios.replace('#include "g3_system_lifetime_cases.c"', system)
    scenarios = scenarios.replace('#include "g3_r145_copy_cases.c"', copy)
    return source.replace('#include "g3_vidmm_replay_scenarios.c"', scenarios)


if __name__ == "__main__":
    assert os.environ.get("G3_REPLAY_R145") == "1"
    assert os.environ.get("G3_REPLAY_PROFILE") in ("16", "64")
    replay.generate = generate
    replay.main()
