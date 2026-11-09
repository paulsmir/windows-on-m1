# OV8: a render fully independent of the compute job before it runs ahead on both stages

Status: spec (architect, 2026-11-09). Class I (receipt) then O (waiver). Opt-in until the ABBA.

## Evidence
- Apple HD split capture (`.local/experiments/EXP2226-macos-splitd/run2/splitd-hd.csv`, frames 800-7000):
  per frame sum of GPU intervals 21.41 ms, union 18.52, compute||render overlap 2.03 ms, cross-frame 0.31.
  Two pairs carry it, in every frame, and the RENDER starts before the compute: enc 15 (1.30 ms) inside
  enc 16 (3.94 ms) = 1.46 ms/frame; enc 18+19 (0.73 ms) inside enc 20 (1.10 ms) = 0.75 ms/frame.
- Pass order: the compute encoders identify in the same order as our C0/C2/C4/C6 (C2 = enc 11+13 verified
  dispatch-by-dispatch); by position the renders are R1=enc 10, R3=enc 14, R5=enc 16(+17), R7=enc 20.
  (`tools/agx_gm_scene_align.py` GROUPS assigned renders by duration; its render sums are unaffected, its
  per-position names are not to be used.)  So Apple overlaps C4||R5 and C6||R7, and both renders run their
  whole TA+3D while the compute runs: GravityMark expresses no dependency between them.
- Ours (EXP2284 UMD receipts, steady frames): OV5 TA verdicts per render-after-compute [9, 9, 9, 0]:
  R1/R3/R5 refused for a CDM prelude (reason 9), R7 TA-independent; without the prelude rule [10, 10, 10, 0]:
  vertex-stage conflicts VB read vs compute ROOT UAV write at resource granularity with UNSIZED VB ranges
  (EXP2280 OV6b). The R7 TA waiver alone gave no fps (EXP2294-2297): its 3D still waits for C6, Apple's does not.

## Contract
- WINDOWS: ordering between the compute batch and the render is defined by the D3D12 ResourceBarriers the
  app recorded between them (and by UAV hazards inside one list). If no barrier orders a compute write
  before a render access, and no render access range intersects a compute UAV write range, the render is
  independent of that compute under D3D12 rules (same rule as OV2 ranges, direction reversed).
- AGX/ASAHI: a render job needs no wait on the compute queue at all when independent (Asahi: separate
  queues, barriers only where the user space asked); the depth-2 window already kicks a render behind a
  running compute (TA_ON_COMPUTE class) with a TA lead Barrier and, under OV5, a 3D Barrier instead.
- TRANSLATION: OV8 = TA_ON_COMPUTE successor built with NO lead Barrier on either ring (TA root alone,
  3D root alone), admitted only when the UMD flags the render FULLY independent (new flag in the render
  header / parse flag, like RENDER_TA_INDEPENDENT) and the KMD kick is past the compute-start gate.
  Completion ordering: the render may end before the compute (W4) -- Q2c already makes that safe for the
  notifier (ComputeOwnQueue); fences are per job, so the render's fence may complete first (dxgkrnl allows
  out-of-order completion across contexts only if we report in submission order: keep today's ordered
  reporting, the gain is GPU time not report time).

## Step 1 (receipt only, diag on, one GM HD run): per render-after-compute
1. full-range verdict: any render access range (all stages) vs compute UAV write ranges, with sized VB/IB
   views (OV6c: VB range = view SizeInBytes, IB likewise; VS UAV counted as a write only when the shader
   stores) -> reasons: 0 independent, 10 conflict (first pair as in OV6b), 11 unplaceable;
2. barrier verdict: does any ResourceBarrier recorded between the compute batch's last dispatch and the
   render's first draw order a resource the compute wrote (symmetric AgxD3d12JobRangeOrdered)?  8 ordered;
3. prelude verdict: does the render's CDM prelude read (indirect args/counts) a range the compute wrote?
   9p dependent, 0 independent.
Receipt: measure-d3d12-ov8 per execution, nibbles per render for (1),(2),(3). Expected from Apple's data:
R5 and R7 come out 0/0/0 (or 0 with a prelude that is independent). If R5 shows 8 (GM-D3D12 placed a
barrier), the D3D12 contract forbids the overlap and that 1.46 ms is GravityMark's own port cost: record and stop.

## Step 2 (opt-in waiver, Wom1Ov8FullIndependent=1)
UMD flags full independence (verdicts 0/0/0); KMD builds the successor without lead Barriers on both rings
and, for a render with an independent prelude, lets the prelude run on the compute queue behind the compute
job as today but does not make the TA wait for the compute job's stamp (only for its own prelude's). Gate
by the compute-start rule (EXP2293). Tests: tracker verdicts (tests/test_ov6_exact_ranges.py style), KMD
build (tests/test_ov5_ta_waiver.py style: both lead addresses zero, done pointers as plan). Hardware: 3 GM
starts stability (diag off), then ABBA against OV2 lead only. Expected up to ~2.2 ms/frame HD (~10 %) if
both pairs clear; ~0.75 ms (3.5 %) if only R7 clears.
