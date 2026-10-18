# R150 offline investigation plan

Authorized: execute NEXT_TASK_R150.md on integration/ad04-windows-compiler at
96da01f0. No Air, firmware changes, package, install or hardware experiment.
Starting implementation commits 0f51f3f5,38308fa4,3a7b6cc9 remain integrated;
EXP864 executed source a9ecd3ea without 0f51f3f5. Dirty submodules preexist.

1. Verify original hashes; inspect EXP864 full kernel dump with matching PDB,
   live141/minidump and ETL provenance. Identify KMD adapter/process/queue,
   scheduler/preemption fences and broker evidence. An absent rejection is
   not acceptance evidence.
2. Read primary Asahi completion/fault paths, m1n1 broker and Mu ACPI, then
   Microsoft reset/preemption/completion contracts. Record inspected paths,
   owner differences and checkpoint in R150-contracts.md. Parallel read-only
   contract and ETL investigations use disjoint report/evidence files.
3. Relate QUERY24 by real source and matching process identity. Rank causes
   once; stop offline passes when no new observation can distinguish them.
4. If a deterministic current-owner defect is proved, prerecord its exact
   change/replay, observe RED, implement minimal correction, observe GREEN,
   run affected ARM64 /W4 /WX /analyze and full suite against15F/38E/2S.
   Otherwise propose the smallest discriminating receipt; do not invent a fix.
5. Independently review findings; update current boundary and next gates only
   if changed, commit reviewable work, append CHANGES.csv with commit SHA and
   implemented status, validate ledger and commit bookkeeping.

Current sources inspected: EXP864 manifest/results, minidump/watchdog analyses;
scheduler_windows.c (preempt, completion, reset), gpuva_g3_windows.c
(PrivateReset), apple_agx_scheduler.c. Remaining source/doc inventory belongs
in the contracts report before any behavior edits. Reset currently refuses
active private work without a hardware-stop/TLB proof. Runtime ownership is
KMD bookkeeping and WDDM notification, m1n1 broker for GPU lease/firmware work,
Mu for reserved-memory/ACPI exposure. Exact live state is taken exclusively
from saved EXP864 observations; offline work cannot refresh ADT/registers.

Smallest checkpoint: establish submitted/started/completed/preempted fence
identity from saved dump and ETL. If unavailable, propose one diagnostic-only
EXP865 change preserving EXP864 behavior and R143 firmware. Recovery remains
exact-package evidence-first cleanup then ordinary GPU-visible Code28; hidden
image is emergency rollback. No run is authorized by this plan.

Review focus: same-boot pointer joins; symbols/source provenance; reset status
is not hang cause; no fake fence completion; no recovery claims without GPU
quiescence.

## Evidence-selected correction

Full kernel dump (SHA256 c29d41e871c9ecc97cd2147ec037dcc4b2667b8bab430213816a810b211d404e)
with matching package864 private symbols resolves the boundary: node0/engine0
active149, completed148, preempt1 waiting for149; broker slot1/job lease held;
backend Ready owner63, pending job empty, first-run queue state unchanged.
Actual G3 worker overwrites backend owner with1 before real validation. The
worker's two initializer sites both use63 in G3; B1 alone uses1. Source audit
R150-contracts.md establishes hardware slot selection is independently1 in
render_backend_image.c, g4_builder and GraphBeginJob. Keep owner63; remove only
the G3 override. Do not weaken owner validation or change VM slot/caps/reset.

Replay must extract real worker construction and real start/reset initializer
blocks, call production backend validation, observe callback reachability and
retain foreign-owner rejection. Existing G4 materializer replay must preserve
slot1. Compile affected backend_platform_windows.c ARM64 /W4 /WX /analyze
with pinned WDK and hash-verified persistent inputs; full suite once. EXP865
proposal is this owner fix alone atop exact EXP864, excluding integrated Flush
retirement. Hardware validation remains separately authorized.
