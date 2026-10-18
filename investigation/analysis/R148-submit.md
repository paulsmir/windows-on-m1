# R148 offline submit and QUERY ownership audit

2026-09-28. Scope: EXP861 only, current source only. No hardware, package,
firmware changes, or commits performed by this audit. The first audit made no
production changes; the explicitly requested diagnostic continuation below
implements the separately reviewable QUERY v3 extension. The systematic-debugging
skill was applied to trace the failed access to its producer before proposing a
fix. Parent owns the implementation and final verification.

## Findings

**Confirmed source defect:** `AgxWin32AsahiBatchFinish` omits both
`batch->pool.bos` and `batch->pipeline_pool.bos` from its explicit residency set.
The real function includes command, VDM, rodata, attachments, and
`batch->bo_list`. The native pool owns its own BO array; it does not insert every
pool slab into that bitset. Consequently a pool-only BO reaches neither
MakeResident nor `transfer_held` (the staging-to-canonical upload). A mapped or
reserved GPUVA is insufficient to establish either property.

The new production-body replay deterministically reaches the same first parser
failure as EXP861: `ParseUnmapped=3`, render ordinal 12, VA `0x270e40`, one-byte
read. It observes only four resident allocations (command, VDM, rodata, color),
where the single-slab setup requires six including the two pool BOs. Evidence:
`investigation/evidence/R148-submit/pool-residency-red.txt`.

This establishes a UMD defect and a causal reproduction of the observed
boundary. It does **not** prove the unrecorded hardware BO identity or exclude
an additional KMD mapping defect. The later QUERY receipt is distinct.

## Ordinal and backing identification

EXP861 `g4-decoded.json`: header v3 (480 private bytes with 280 native bytes),
root `0x9d1d50000`, mapping generation 440, process generation 1, process owner
5, render ordinal 12, VA `0x270e40`, read 1, GraphPresent 0. A 280-byte native
packet contains one 24-byte attachment plus its 8-byte header and the
240-byte render plus its 8-byte header.

The parser numbers successful/nonzero access attempts across the whole packet:

| Access | Ordinal |
| --- | --- |
| Nine kernel-private ranges | 0–8 |
| CPU command envelope | 9 |
| One color attachment | 10 |
| VdmCtrlStreamBase | 11 |
| First following nonzero render address | 12 |

For the pinned producer this identifies **IspScissorBase** by source inference:
helper binaries, when nonzero, are expanded by the parser to the USC interval
starting `0x1100000000`, incompatible with `0x270e40`. Helper data is a BO base
and is populated together with its helper binary; GPUVA BO bases are 64-KiB
aligned, incompatible with the observed interior address. The producer next
assigns `isp_scissor_base` from `agx_pool_upload_aligned(&batch->pool, ..., 64)`.
That pool allocation returns a BO base plus a cursor, and scissor is nonzero.
The native render packet itself was not captured, so the receipt alone does not
contain a field-name tag; the identification relies on this pinned producer
contract, not on interpreting ordinal 12 as structure field 12.

The backing is therefore a slab owned by `batch->pool.bos`, class General,
native flags 0. Windows `agx_bo_create` rounds its size/alignment to 64 KiB.
`AdmissionUmdScreenCreateClassBufferImpl` gives it a GPU-local canonical
allocation (`CpuVisible=0`) and a separate CPU-visible staging allocation.
`map_va` maps the canonical handle and stores its returned VA in
`CanonicalGpuVa`; CPU pool uploads write staging. `make_resident` translates
the BO token to the canonical handle and marks the same slot CopyHeld;
`transfer_held` subsequently copies staging to that canonical GPUVA.

**The actual EXP861 BO token, allocation handles, slab base, slab size, and
suballocation offset are not present in the receipt.** A 256-KiB native slab
can start before `0x270000`; do not infer that base by rounding the failed VA.
The replay uses a synthetic slab base of `0x270000` and offset `0xe40` to
reproduce the observation, then a different VA/offset in its rollover case.
Those values are test inputs, never proposed acceptance conditions.

The G4 receipt's four zero LogicalIpa/Segment/Flags values are not proof of
four present-but-invalid shadow entries. `AdmissionG4SnapshotFailure` returns
early when a parent, shadow, or LogicalPtes array is absent, leaving the same
zero fields. It also examines LogicalPtes, while COPY uses ResidentPtes.

## QUERY53 remains a separate unresolved boundary

EXP861 `query-decoded.json`: selected root and context root `0x9d1d50000`,
SetRoot 1/1, bootstrap `0x9d2f58000`, mapping generation **623**, query
`[0x20000,0x30000)` (65536 bytes), first missing VA `0x20000`, native level 2,
index 8, reason/component `leaf-absent`. This is later than G4 generation 440;
same root is insufficient to equate the allocations or mapping state.

Current capture code already checks the four ResidentPtes corresponding to a
missing 16-KiB native leaf. Any valid member changes `leaf-absent` to
`leaf-not-published`. Therefore this QUERY excludes a partial group with at
least one valid resolved ResidentPte. Remaining cases include all four invalid
or absent ResidentPtes/shadow provenance. Its record cannot distinguish a
range never made valid from one subsequently invalidated.

`transfer_held` only invokes QUERY for CopyHeld slots, and `make_resident` sets
CopyHeld on exactly the canonical-token residency set before invoking
MakeResident. `AgxWin32GpuvaSubmit` waits the returned paging fence before
calling the submission callback, which performs the upload. Thus this QUERY
is for an allocation already selected for the residency path. Omitted pool
enumeration alone does not explain it. Its canonical/staging handles and BO
label were not recorded, so do not call VA `0x20000` rodata or a pool allocation.
No KMD mapping fix or publication bypass is justified by these data.

Smallest useful diagnostic extension, if QUERY persists after the deterministic
UMD correction:

1. Persist the **first UMD copy failure** only: runtime device/context identity,
   slot token/serial, canonical and staging handles, class, flags, byte length,
   CanonicalGpuVa, upload/download phase, MapGpuVA HRESULT/returned VA/fence,
   the MakeResident attempt sequence/HRESULT/requested and returned count,
   paging fence and wait result, CopyHeld/SubmissionHolds, QUERY HRESULT.
   Retain only the last successful mapping/residency metadata in each already
   bounded slot, and one durable failure record per device. No allocation
   enumeration or streaming trace is needed.
2. Extend the existing first-failure KMD QUERY snapshot under its existing lock:
   request allocation handle, resolved allocation identity/size, explicit
   `ResidentGroupLookupFound`, and the four resident entries' flags, segment,
   IPA, allocation identity and allocation offset. Distinguish unavailable
   shadow provenance from four zero entries explicitly. These are observations
   only and cannot change guard 53 or any other guard.

These fields distinguish incorrect canonical pairing, failed/pending residency
or wait, missing shadow, and a complete-but-unpublished group. They still do
not establish never-valid versus invalidated history from four zeros. If that
distinction remains the only unresolved question, add a separately bounded
last-touch provenance observation at the leaf update owner, not an invented
conclusion from current zeros. A small global ring must report overflow and
cannot claim an update never happened after wrap. Prefer existing ETW evidence
for allocation residency transitions before introducing persistent per-PTE
history. This limitation is deliberate: the available receipt cannot uniquely
attribute QUERY to a KMD update/eviction defect.

## Minimal owning-layer fix and verification contract

Production path: `drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c`.
After native command construction has finished allocating, enumerate every BO
in both pool arrays and feed it through the existing `add_bo` token-dedup path.
Increase the reference capacity by both dynamic array element counts with
checked integer addition/multiplication. Do not use only `transient_bo`, which
omits old slabs after rollover. Do not scan command bytes or use a fixed
address/size whitelist. Preserve low-VA/execute/read-only flags and canonical
allocation identity; pool BOs follow their existing creation policy.

Borrow the pool references while the batch owns their lifetime. The existing
Gpuva.Held residency set survives until render retirement; after failure it is
evicted using the same reference set. Normal batch cleanup still owns pool BO
unreference. Enumeration failure must reject the batch and preserve existing
private-scene/command release behavior. No KMD, m1n1, Mu, caps, signer, or
recovery change is needed for this correction.

Regression files added by this audit:

- `tests/test_g4_mesa_pool_residency_replay.py`: extracts the complete current
  production Finish, Poll, Abort, Release and required helpers; compiles with
  actual `agx_win32_gpuva.c` and `apple_agx_g4_submit.c`, ASan/UBSan and
  `-Wall -Wextra -Werror`.
- `tests/g4_mesa_pool_residency_replay.c`: models VidMm canonical residency and
  staging upload callbacks, with two independent pools, rollover slabs,
  aliases across pools/rodata/bitset, a changed VA/offset, retirement, and
  submit-rejection unwind. Empty pools, NULL pool entries, invalid pool
  identity, and overflow independently at both dynamic count additions are
  also exercised. The parser is production, not reimplemented.

RED command: `PYTHONPATH=tests python3 -m unittest tests/test_g4_mesa_pool_residency_replay.py`.
Expected corrected result: all pool slabs resident/uploaded once, parser
acceptance in the model, same exact-set eviction on retire or rejection,
command/private scene release, no premature pool unreference. After the
parent's production correction, this exact command passed all eight modeled
scenarios with ASan/UBSan; `pool-residency-green.txt` records the result.
Parent owns native ARM64 build, full suite, and ledger workflow.

## Inspected primary contracts and ownership

- Mesa pinned reference `/Users/pavel/public_windows/.local/reference/mesa`,
  commit `9aa1215f878b504f66159dd2ead4c7973142126e` (matches source lock):
  `src/gallium/drivers/asahi/agx_pipe.c` render producer around 1550–1603 and
  field writes around 1391–1421; `agx_batch.c` pool init/cleanup;
  `src/asahi/lib/pool.c` and `pool.h` BO array lifetime/cursor allocation.
- Current source projection `native-asahi-batch-lifecycle.py` replaces native
  submission with AgxWin32AsahiBatchFinish, retaining native producer/pools.
  `native-asahi-graph-capture.py` and build-native-asahi-state.py were checked
  for pool/source overlays; they do not insert all pool BOs into bo_list.
- Asahi Linux reference commit `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`:
  `drivers/gpu/drm/asahi/queue/render.rs` forwards isp_scissor_base into
  firmware job parameters; `mmu.rs::map_at` explicitly owns GEM mapping and
  VM lifetime; `arch/arm64/boot/dts/apple/t8103.dtsi` declares G13G MMIO,
  mailbox, power domain and UAT reserved memory. No register value or power
  change is inferred here.
- m1n1 checkout HEAD `20d55f4c9532df4bcd6bdb8a04ad01089c453672`:
  `hv_agx_local_reserve.c` validates identity-mapped carveout ownership;
  `hv_agx_gpuva_v5.c::hv_agx_gpuva_v5_update_leaf` accepts all-zero or complete
  four-page 16-KiB leaf groups, validates backing ownership, and publishes
  descriptors. It does not infer residency from UMD commands.
- Mu checkout HEAD `0dac68712f3bc520d46f88af84a430cfb916dfc4`:
  `Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc`
  exposes APPL0002, coherent resources, validated local reserve receipt and
  interrupt resource. It is not the owner of process GPUVA allocations.
- KMD `gpuva_g3_windows.c` parser access/snapshots/QUERY, shared
  `apple_agx_gpuva_g3_graph.c` graph walk, `gpuva_g3_paging_windows.c` shadow
  ownership, and UMD `umd_gpuva_windows.c` plus `umd_win32_screen.c` mapping,
  canonical/staging, residency, copy and lifetime ownership were inspected.
- Official Microsoft pages read 2026-09-28:
  [GPU virtual memory](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-memory-in-wddm-2-0),
  [driver residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/driver-residency-in-wddm-2-0),
  [GpuMmu scenarios](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/examples).
  Virtual submissions do not carry legacy allocation/patch lists; UMD owns
  residency selection, while VidMm owns mapping/paging scheduling through KMD.
  The missing object must be added to UMD residency/upload ownership, not
  artificially mapped during KMD parser validation.

No external source code was copied into the repository. The replay consists
of local production functions plus project-owned callback/type shells.

## Smallest proposed hardware checkpoint (not authorized by this audit)

WHY THIS HYPOTHESIS: (1) pinned native producer stores the first rejected field
in the batch pool; (2) current UMD never enumerates either pool for residency
or staging upload; (3) the production-body replay reproduces ordinal12 with
that omission and a VidMm-contract model.

EXP862 single behavior variable: complete batch pool residency/upload set,
after software GREEN and normal independent review. Success at this boundary
is absence of the same pool-access rejection and advancement to the next
KMD checkpoint; no GPU fence, TA/3D, or display success follows merely from
parser admission. A remaining QUERY53 must be reported separately, with its
own root/generation. Keep the EXP861/R143 launch/firmware contract and known
ordinary-Code28 recovery artifacts. Parent owns preregistration and hardware
authorization; no launch was attempted here.

## Authorized continuation: minimal QUERY v3 diagnostic

The parent subsequently requested the smallest bounded diagnostic instead of
the broader optional UMD/PTE proposal above. Implemented only this subset:

| ABI field | Offset / value | Meaning |
| --- | --- | --- |
| v2 prefix | 0–143 | Unchanged layout; Version=3, Bytes=168 |
| RequestAllocationHandle | 144, u64 | Numeric runtime handle from copied request |
| CanonicalAllocationIdentity | 152, u64 | Numeric KMD allocation identity used by resident PTE provenance |
| CanonicalAllocationBytes | 160, u64 | Validated allocation byte length |
| ResidentGroupAvailable | Flags bit 64 | Failed native leaf's ResidentPtes lookup succeeded |
| CanonicalAllocationAvailable | Flags bit 128 | Local canonical allocation validation completed under lock |

Capture receives the existing allocation pointer, initialized to NULL. The
canonical identity and size are read only under the QUERY mutex after guards
34–41 validate allocation ownership, runtime handle, magic, class, non-CPU-
visible and local type. The acquired Dxgk handle reference remains live until
after capture and mutex release. Request identity can be recorded earlier if
the fixed-size request was copied; an error before copy leaves it zero.
The group-available flag is set by the existing leaf-absent provenance lookup;
neither its old reason reclassification nor any admission guard is changed.
Registry persistence still happens only after releasing the allocation
reference and uses the immutable first snapshot. No new allocation, history,
ring, UMD callback, or live hardware action is introduced.

`tools/decode_g3_copy_query_failure.py` accepts legacy v1/16 and v2/144 unchanged
and current v3/168, rejecting wrong lengths and version-specific unknown flags.
`tests/test_g3_copy_query_receipt.py` and the existing real KMD/VidMm/broker
replay cover both 16-KiB and 64-KiB profiles, all-invalid resolved resident
group versus absent shadow provenance, partial group, canonical identity and
size, copied-but-invalid early handle, pre-copy rejection, and the complete
168-byte first-record immutability test. Existing guard/status regression
cases continue through the same real-body replay.

RED was recorded before production changes in
`investigation/evidence/R148-submit/query-v3-red.txt`: unsupported v3 decoder
and old v2 capture in both profiles. GREEN is
`investigation/evidence/R148-submit/query-v3-green.txt`, command
`PYTHONPATH=tests python3 -m unittest tests/test_g3_copy_query_receipt.py`;
both tests pass, with both profile replays under ASan/UBSan. Native ARM64
build, full suite, independent review, and commit/ledger work remain the
parent's responsibility.

This observation identifies the actual canonical request for possible ETW
correlation and resolves missing provenance versus an all-invalid resident
group. **It cannot prove eviction history or that an all-invalid group was
never mapped.** No QUERY ownership defect is claimed fixed.
