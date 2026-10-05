# R149 — EXP862 next boundary, offline verdict

Task: execute `NEXT_TASK_R149.md` on the integration branch containing
R148 Flush `0f51f3f5` and EXP862 ledger `318e3fc9`. No package, Air access,
launch, reboot or firmware change was performed. Existing dirty m1n1/Mu trees
were preserved. All 536 EXP862 source files, 18 guest originals and four
package artifacts were independently hash-verified.

## Findings and independent corrections

1. **USC coordinates are deterministically inconsistent.** Pinned producer
   ordering identifies ordinal14 as `Bg.Usc` for the reference packet shape;
   the hardware packet and field tag were not captured. That inferred field
   belongs to a non-EXEC Encoder-class pipeline-pool slab, not shader machine
   code or R137's KMD-private backing. Its exact hardware BO base/token is
   unknown. The broader defect does not depend on this inference: UMD maps
   LOW_VA BOs below 4 GiB and sets `shader_base=0`, while parser and firmware
   work expand USC offsets from `0x1100000000`. Residency cannot create that
   missing high mapping. Real Attach/Bind/native encoding/parser replay is
   RED at ordinal14/`0x11002b0140`, then GREEN with the same packed offset.
   The correction places USC BOs inside the existing execution window and
   uses that base for native encoding. A shared constant keeps UMD, parser
   and builder consistent; bounds and ownership checks remain enforced.
2. **QUERY v3 proves absent valid provenance, not the cause.** Canonical KMD
   object `0xffff9388623cf3a0`, token `0x40000fc0`, 65536 bytes is validated
   GPU-local/non-CPU-visible storage. `ResidentGroupAvailable` says the array
   exists; all four valid bits for `0x20000..0x23fff` are clear at generation
   690. Neither exact BO class nor PID nor preceding leaf lifecycle is
   captured. The two matching ETL token lifetimes are 12288-byte software
   allocations and are excluded. No mapping fix or guard relaxation follows.
   This remains separate from submit generation474 on the same root.
3. **CPU0's module and SSH-loss cause remain unidentified.** The exact DLL/PDB
   lacks a same-boot load base/process association. ETL contains no image-load
   provider and no frame in the sampled PC's 64 KiB window. Its actual events
   stop at09:26:56, not the09:39:15 header end. The unchanged `iar` field counts
   SGIs; independent INTID18 acknowledgement times advance. The saved ELR is
   insufficient to distinguish a user loop from repeated EL2 entry.
4. **Rejected-work error propagation is a separate confirmed defect.** A
   failed Submit callback returns before the render-fence wait; clean rollback
   marks the batch rejected and its poll returns immediately. Nevertheless,
   Windows Flush could report S_OK while native Mesa had faulted the context.
   The existing BatchFinish failure label now sets `Backend.Failed`; it does
   not set the ownership-uncertainty flag `Gpuva.Terminal`. The real frontend
   replay changes from no error callback to `SetError(E_FAIL)`, with safe
   rollback/release preserved. This is not a proven SSH-stall remedy.

Detailed source contracts, evidence, and limits:
`R149-submit.md`, `R149-query.md`, `R149-stall.md`,
`R149-stall-error-plan.md`. OPEN tandem dispositions: `R149-review.md`.

## Rank and proposed EXP863

USC placement is the strongest demonstrated cause of the current mapping
rejection. Failure publication is the first experiment priority because it
can test correct failure handling without enabling a new GPU execution path.
QUERY lifecycle attribution is next only when its exact ordered identity and
mapping evidence exist. A CPU0 UMD-spin hypothesis is unproven and does not
justify changing a timeout or firmware interrupt policy.

WHY THIS HYPOTHESIS:

- EXP862 records KMD submission rejection; the current source's successful
  rollback leaves the Windows failure flag clear while native Mesa faults.
- Real frontend/BatchFinish/submission/rollback/FlushStatus replay reproduces
  the resulting false S_OK and proves the isolated error-publication change.
- The change preserves the known USC rejection and resource ownership rules,
  separating failure handling from advancing GPU acceptance or shared-memory
  retirement. An SSH improvement is not assumed.

Propose **EXP863: error publication only on exact EXP862 source `fd603f30`**,
with its pool change and QUERY v3 retained. Select only the independent R149
error-publication commit. The combined integration HEAD also contains the USC
and R148 Flush fixes and is therefore not that single-variable candidate.
Use the identical R143 firmware/caps/signer/recovery contract.

Primary checkpoint: the unchanged first USC rejection, followed for the same
device by `flush-state` showing backend failure and frontend `reject-seterror`,
with no additional native submission from that terminal device. Missing
callback/Flush-return identity is inconclusive. SSH availability, memory trend
and CPU state are secondary observations; no improvement alone proves which
instruction stalled. Collect evidence before exact-package cleanup and restore
ordinary GPU-visible Code28; retain immutable hidden recovery for loss of SSH.

The deterministic error-publication correction should precede USC acceptance
progress and the separate pending R148 Flush experiment. There is no identified
"stall fix" to claim must precede Flush. Separately, the accepted-work
`Poll(0)` path can call a synchronous wait and needs a real unsignaled-fence
timeout/lifetime regression before it is changed; EXP862 does not establish
that path as its stall. Do not use the false-S_OK correction as timeout proof.

This is a proposal, not preregistration: no EXP863 package, artifact hash,
install command or hardware authorization exists in this task.

## Verification

`investigation/evidence/R149/summary.json` records final commands, full-suite
baseline identities, compile results, hashes and limitations. The two new
host regressions pass ASan/UBSan and independent review; current QUERY receipt
replays retain both page profiles. Native ARM64 compiler analysis uses Clang
where MSVC `/analyze` is unsupported, with only baseline-proven warning
exceptions. Portable changed UMD/KMD sources also pass MSVC ARM64
`/W4 /WX /analyze`. Software-only status is `implemented`; no hardware
validation of either R149 correction is claimed.
