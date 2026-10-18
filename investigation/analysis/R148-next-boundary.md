# R148 — pool residency, shared retirement, and QUERY provenance

Offline execution of NEXT_TASK_R148.md, based on EXP861/source8f337257 and
ledger7bdf1a13. No Air access or linked/signed package. All18 saved guest originals
and536 package source inputs were SHA-verified before changes; see
`investigation/evidence/R148/inputs.json`. Existing nested m1n1/Mu dirty state
is preserved. EXP861 ordinary Code28 remains the last accepted recovery.

## Evidence and corrections

1. **Committed growth:** the sampled rise668442624→4464254976 is GPU commitment,
   not physical resident RAM. Saved state at~700.7s still has1925180KiB free
   physical memory, and no collected Resource2004 record. Exhaustion and the
   later SSH loss remain unattributed. ETL ends before the first149s heartbeat:
   sequential512MiB capture filled, despite zero reported event loss. It cannot
   account for the entire later478.6s growth interval.
2. **Specific retention:** early ETL shows DWM's long-lived consumer retaining
   89 shared globals/376045568B after their producer-device epochs ended.
   Most globals did retire:16306 of16897. Source identifies a missing steady-state
   deferred-retirement drain in the Mesa frontend Flush; existing Windows
   shared tests invoked the internal drain manually, hiding this omission.
   This proves a contract defect, not attribution of every later committed byte.
3. **Ordinal12:** it is a dynamic access ordinal. Pinned producer/parser analysis
   identifies IspScissorBase after the nine private ranges, envelope, attachment
   and VDM. Scissor belongs to a batch-pool slab, a General-class canonical local
   BO with separate CPU staging. Both batch and pipeline pool BO lists were
   omitted from MakeResident and upload. Actual hardware slab base/token/size
   are absent from the receipt; rounding0x270e40 does not identify the BO base.
4. **QUERY:** later VA0x20000/65536, root0x9d1d50000, generation623, nativeleaf2/
   index8 is separate from submit generation440. Its canonical slot already
   belongs to the residency transaction. v2 excludes a partially valid resolved
   resident group, but cannot distinguish all-invalid resident entries from
   missing private shadow provenance. No KMD mapping relaxation is justified.
5. **Task-summary correction:** DWM had no recorded crash; Explorer AV at683.55s
   followed the627.55s checkpoint. SearchHost AV is in edgehtml. Neither faulting
   module proves the allocation or SSH-loss cause.

Detailed reproducible forensics: `R148-memory.md`, `R148-submit.md`; current
OPEN review dispositions: `R148-review.md`.

## Independent changes

- Pool fix: enumerate every slab from both Mesa pools through the existing
  allocation deduplication path, with checked reference capacity. Existing
  MakeResident→paging fence→copy→submit→render fence→Evict ownership remains.
- Shared-retirement fix: successful public Mesa Flush collects native-retired
  presentation resources and drains eligible deferred shared closures, including
  empty flushes. Registered native BOs, pending submissions and failed callbacks
  retain their holds. No fabricated completion or premature deallocation.
- QUERY diagnostic: first-failure v3 adds canonical/request allocation identity
  and size plus explicit resident-group availability; legacy receipts remain
  decodable. No guard, status, mapping, allocation or history policy changes.

UMD owns both corrected contracts. VidMm retains allocation placement, scheduling
and logical page-table ownership; KMD retains shadow publication and validation.
Asahi reference owns pool/VM lifetime directly. m1n1 grant/root/GPU ownership and
Mu reserve/ACPI ownership are unchanged, as are IRQ/DMA/power/recovery. Primary
sources and the before-edit plan are recorded in the two detailed reports and
`docs/superpowers/plans/2026-09-28-r148-pool-residency.md`.

## Proposed EXP862 — one semantic variable

WHY THIS HYPOTHESIS:
- EXP861's first failed render address is produced in a Mesa batch pool.
- The current adapter omits the independently owned pools from residency/upload.
- Real BatchFinish+GpuvaSubmit+parser replay reproduces ordinal12/0x270e40 with
  the omission and advances after complete pool enumeration.

Propose **pool residency/upload membership only**, on EXP861's exact R143
firmware/caps/signer/recovery. Select the isolated pool commit on the EXP861
source base when constructing the candidate. The shared-retirement fix is an
independent change and must not be mislabeled an inseparable atomic contract;
validate it separately unless a later explicit direction authorizes a bundle.
QUERY v3 is also a separate diagnostic commit, available for a separately
specified diagnostic candidate. The final combined branch is not itself the
single-variable EXP862 candidate.

Expected checkpoint: submit passes the old scissor access or emits a precise
new first rejection. Absence of a receipt, Code0, or a synthetic replay alone
is not GPU completion. Keep QUERY snapshots separate by root/generation.
For the later retirement test, trace a bounded late interval as well as startup
so the sequential512MiB cap cannot censor the whole measured growth period.
Collect evidence before exact-package cleanup, preserving accepted Code28 and
emergency hidden recovery. No build/install commands or artifact hashes exist
for EXP862 yet; this proposal is not preregistration or hardware authorization.

## Verification

Final commands, failure identities, compiler exceptions, source hashes and
commit references are recorded in `investigation/evidence/R148/summary.json`.
Status is implemented offline; no new hardware validation is claimed.

Implementation commits: pool `ac4731fec1fad664bf1001ccefbb81c21897ade6`;
shared Flush `0f51f3f5ff556b14071187e96e0ccbb8606f3609`;
QUERY v3 `c239965f9b8ec2aa85f32b2b98df4286d428e124`.
Final implementation tree `1d270953723ffe2c472a84be6e77a8962bb7d8d2`.
All three CHANGES rows are implemented, with empty hardware-result fields.

Validation: full1159/154.848s preserves all53 baseline failure/error identities
(15F/38E/2S); no new failures. All114 KMD TUs and the UMD bridge pass MSVC
ARM64 W4/WX/analyze. Native batch and generated frontend compile/analyze with
baseline-proven Clang warning exceptions (pointer/sign and unused-parameter,
respectively); Clang's analyzer substitutes for unsupported MSVC /analyze.
Windows x64 real public Flush ABI replay passes; full native two-device test
was edited to use pfnFlush but not built/executed in this task. Host pool and
QUERY use sanitizers; lifetime and public Flush tests preserve pending/failure
holds. Independent review accepted all three changes. Ledger schema2 passes.
