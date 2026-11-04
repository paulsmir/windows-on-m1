# L3: G4 parse cost under the adapter-wide G3 lock (EXP2157 follow-up)

Scope: offline source analysis only (no hardware run). Paths are relative to
the worktree. `W` = `drivers/apple-agx/render-admission/src/gpuva_g3_windows.c`,
`G` = `drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c`,
`S` = `drivers/apple-agx/shared/src/apple_agx_g4_submit.c`,
`P` = `drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c`.
Evidence labels: SOURCE = read in this tree; MEASURED = EXP2157 ledger
(investigation/EXPERIMENTS.md:68837-68842); HYPOTHESIS = estimate, not measured.

## 1. Where parse time goes

### 1.1 Access-callback calls per stream (SOURCE)
`parse_submit` (S:210-388) calls `check_access` (S:34-48) once per proven range:
- 9 process ranges, `AppleAgxG4AccessProcess` (S:268-288; count S header :37).
- 1 CPU envelope (the DMA buffer VA, <= 4096 B, S:290; max S header :38).
- v5 compute prelude: up to 6 (stream, sampler heap, helper binary/data,
  timestamp, preemption; S:184-208).
- N attachments, `AppleAgxG4AccessAttachment` (S:331-339), one per RT.
- render: timestamp + up to 19 addresses (S:91, S:96-127); 18 of them are
  1-byte probes (one page each), plus the 256 KiB occlusion heap (S:105).
Total ~30-35 + N calls per render stream; `AppleAgxG4ParseComputeEx`
(S:431-482) makes ~7.

### 1.2 Cost per call (SOURCE + HYPOTHESIS)
- `AdmissionG4GraphAccessTyped` (W:4611-4625): two window-overlap compares, then
  `AdmissionG4GraphAccess` (W:4398-4403) -> `...ContainsRangeAccessCached`
  (G:786-834). `AdmissionG4PrivateGraphAccess` (W:3550-3572) adds a
  scene-range equality check and recomputes the TVB extent for ordinal 2.
- Uncached walk = `AppleAgxGpuvaG3GraphInspectRangeAccess` (G:747-778): per
  16 KiB page (G:4) three direct array indexes `slot()` (G:57-60), no hashing,
  no list walk. Cost is linear in pages, O(1) per page. `LOOKUP_VISIT` is a
  no-op in production (G:10-12).
- CPU envelope (`AdmissionG4LogicalEnvelopeAccess`, W:4627-4661): per page one
  `LeafTableIpa` walk (G:713-724) + a 256-bucket shadow hash and chain walk +
  one logical-PTE check. 1 page per stream, so small.
- Range sizes (S header :305-349, 16 KiB pages): ranges 0/1/3/5/7/8 are
  64 KiB (4 pages); tilemap (4) and TPC (6) are ~4-16 pages at HD; range 2 is
  the TVB heap: 32 blocks x 128 KiB = 256 pages, or up to 6624 pages at
  828 blocks (rc80 comment W:4393-4397). Attachments: an HD RGBA8 target is
  ~500 pages and a 16-bit-float one ~1000.
- HYPOTHESIS: the page walks of the TVB heap and the attachments cover nearly
  all of the parse time. At HD that is roughly 7-10k page walks for each parse
  that misses the cache. The 1-page probes add up to ~35 walks.

### 1.3 Duplicate walks outside the parser (SOURCE)
- `AdmissionG4ResolveOutput` (W:4758-4782), called from preflight (W:6175)
  and submit (W:5996), walks attachment 0 again with the *uncached*
  `AppleAgxGpuvaG3GraphContainsRangeAccess`. The parse has just proven that
  same range through the cached path.
- BeginJob `AdmissionG3OutputAccessibleFor` (W:3460-3471) walks it a fourth
  time, again uncached. `AdmissionG3OutputMatchesLocalFor` (W:3429-3456) runs
  one TranslateVa per page.
- So the colour target is walked about 6 times per job, at least 3 of them
  without any cache.

### 1.4 Range-proof cache: consulted, but structurally low hit rate (SOURCE)
- Consulted on every path that goes through `AdmissionG4GraphAccess`, i.e. on
  all three parses (preflight W:6162, submit W:5966, BeginJob W:3828/3844/3868),
  for both callbacks. It is not used by the CPU envelope check, ResolveOutput
  or OutputAccessibleFor.
- Only ranges >= 64 pages (1 MiB) are cached (`PROOF_MIN_PAGES`, header
  apple_agx_gpuva_g3_graph.h:63; G:796-798). Only 4 entries
  (`PROOF_ENTRIES`, h:61), filled round-robin (G:832). A hit does not refresh
  the entry (G:799-804).
- Consequence: within one parse the large ranges are the TVB heap plus every
  attachment >= 1 MiB. If a stream has more than 4 of them, FIFO replacement
  under a cyclic access pattern hits 0% on every pass. Even with 4 or fewer, the
  order is preflight A, submit A, preflight B, submit B, ... and BeginJob A
  runs later on the worker. Other jobs' insertions evict A's proofs before
  BeginJob A runs (HYPOTHESIS: GravityMark passes with different RT sets).
- Invalidation (G:63-81): every leaf mutation in a leaf table a proof crosses
  drops that proof, even a purely additive one. One leaf table covers 32 MiB of
  VA (2048 x 16 KiB). Every parent change (G:452-495), root move (G:433-448)
  and table-level reuse (G:400) drops *all* proofs (`table_ipa=0`).
- Unrelated paging therefore does not wipe the whole cache per frame. Only a
  topology change does, or a PTE update inside the same 32 MiB windows as the
  TVB heap or an RT.
- MEASURED: 42656 BuildPagingBuffer holds against 30295 BeginJobs, so about
  1.4 paging operations per job. How many of them are topology changes, and
  how many hit the proven windows, has not been measured.

## 2. What each parse protects against (SOURCE)

| Parse | Bytes parsed | Protects |
|---|---|---|
| (a) Preflight escape (W:6185-6226 -> 6126-6183) | the stack copy `q` of the escape buffer (W:6202), kernel memory | Advisory admission before `SubmitCommandVirtual`: it tells the UMD whether the stream (header, lease, scene state, mappings, output) would be accepted, while a refusal can still be handled ("R147: SetRoot only guaranteed before scheduled execution", W:6220). It is not bound to the later submit: the UMD may submit different bytes. |
| (b) Submit (W:5966-5972) | `args->pDmaBufferPrivateData` = the UMD private data in the DMA private buffer; the native command lives here, after the header (S:292), not in the GPU DMA buffer | The authoritative accept/reject for this submission: structure, lease vs scene, all ranges mapped/writable now, output resolution. The view is then copied into the kernel image (`AdmissionBackendImageBindG4Submission`, render_backend_image.c:292-398; `G4Command` memcpy at :372) or into the pending entry (W:4981-5004), after the lock is released (W:6037). |
| (c) BeginJob (W:3812-3871) | the KMD-owned image: `image->G4Header` (recomposed V2, render_backend_image.c:366) + `G4Command`, or a V5 header rebuilt from them (W:3818-3829) | Mapping changes between submit and execution: VidMm can update or unmap PTEs while the job waits (deferred queue, previous job). It runs under the same lock as JOB_BEGIN, which pins the graph (W:3911-3914, comment W:3937-3940). It also re-validates the bytes actually executed against what was bound. |

Treating `pDmaBufferPrivateData` as kernel memory: the code reads it with
plain `RtlCopyMemory`, with no probe and no `__try` (W:5900, 5903). The parser
also keeps pointers into it that are used after the lock is released (Bind).
That is safe only because dxgkrnl provides a kernel copy (HYPOTHESIS: it
follows the documented WDDM submit model; check
`DXGKARG_SUBMITCOMMANDVIRTUAL` before relying on it). TOCTOU on user-writable
memory is therefore not what (c) guards. (c) guards mapping drift and the
image, so it is the only check of the graph state at execution time.

## 3. Soundness of elision

A skipped re-computation is sound only if every input to the parse result is
provably identical. The inputs (SOURCE):
- the bytes;
- `dma_va`/`dma_bytes`;
- the callback and its context;
- graph leaves, parents and root (`MappingGeneration`, bumped at G:446, 494,
  674; P:876, 1042);
- `LogicalPtes` (CPU envelope);
- `process->PrivateVa` and `TvbVa` (W:4616-4621);
- for private scenes: `scene->Storage.Ranges`, `scene->HeapBlocks` or
  `p->PrivateManager.Blocks`, and `p->TvbVa` (W:3555-3569);
- `Graph.Uncertain` and `Created`.

### 3a. BeginJob re-parse when the kernel copy is unchanged and generation equal: SOUND, with three fixes
1. **Key on the image, not the context.** `context->GpuvaG3MappingGeneration`
   is written at bind (W:6078, 5077, 5299, 5327) and overwritten by BeginJob
   (W:3941). With depth 2 (successor, `GpuvaG3SecondFence`) a later bind or
   BeginJob of the same context can replace it, which would compare against
   the wrong job's parse. Store `ParseGeneration` (and the keys below) in
   `ADMISSION_BACKEND_IMAGE` at bind, for the `BoundFence` it belongs to.
   Today the snapshot is taken in the same lock section as the parse (W:6008,
   5494, 5689), so its value is correct; only where it is stored is wrong.
2. **Close the generation gaps.** `AppleAgxGpuvaG3GraphRegisterTable`
   (G:390-430) retires and reuses tables without `++MappingGeneration`.
   `AdmissionG3ResetTableShadow` (P:145-163) and
   `AdmissionG3RetireSystemSubtree` (P:113-128) zero `LogicalPtes`, which the
   CPU-envelope check reads, without a direct bump. The generation has to
   cover every write to state that the callbacks read; otherwise the CPU
   envelope elision is unsound. Bump it there, or prove that no linked table
   can reach those paths.
3. **Key the non-graph inputs too:** `RootIpa`, `ProcessGeneration`,
   `PrivateVa`, `TvbVa`, and for private scenes the scene pointer +
   `Storage.Generation` + `HeapBlocks` (fixed at submit, W:6013) +
   `PrivateManager.Blocks`.

Must still run (cheap checks, not re-computation): `BoundFence`, lease/scene
lookup, `AdmissionG4PrivateGeometry`, CommandVa/Bytes vs context (W:3852-3855),
the V5 prelude compare (W:3836-3837; the result is identical when the image is
unchanged), and refusals 1-3 and 5-8 (W:3889-3914). The `_MSC_VER` draw
snapshot (W:3954) reads `g4_view.Render`, so the view must come from the image
(offset into `G4Command`) or from a callback-free structural parse.
The generation-equal hit rate is not measured yet: the rc81 receipt
(W:3933-3937) counts it, and with 1.4 paging operations per job it may be low.

### 3b. Submit parse when its bytes equal the preflight-validated bytes: SOUND only with a stored copy and the full key
- Preflight keeps nothing. Its result belongs to the escape (W:6221-6226).
  To skip the submit parse, the KMD would have to keep a per-context record
  `{full private bytes (<= sizeof q.Private), CommandVa, CommandBytes, parse
  key from 3a.3, MappingGeneration, scene pointer/generation, ResolveOutput
  packet}`.
- At submit, compare the whole buffer with `memcmp`, under the lock. A hash
  alone is not a proof: a collision would admit unvalidated bytes.
- Differences that must be part of the key: preflight finds its scene by a
  stricter state predicate (W:6150-6156), while submit uses
  `AdmissionG4FindPrivateScene`/`...Resubmission` (W:5911-5915). Resubmissions
  must never hit the record. Preflight computes the TVB extent with
  `HeapBlocks==0`, i.e. `PrivateManager.Blocks`, and submit sets `HeapBlocks`
  from the same value (W:6013), so `Blocks` must be in the key. Root and
  Uncertain are checked at W:5889-5895.
- What remains: the structural checks (scene, geometry, lease compare
  W:5973-5977, attachment count), the scene/fence state transitions, and the
  failure snapshot when a check fails.

## 4. Proposals, ranked by expected saved lock-hold time (HYPOTHESIS ranks)

**P1. Make the proof cache effective on all three parses (largest, every job).**
- (a) Use `ContainsRangeAccessCached` in `AdmissionG4ResolveOutput`
  (W:4775) and `AdmissionG3OutputAccessibleFor` (W:3468). This removes 2-3
  uncached RT walks per job.
- (b) Raise `PROOF_ENTRIES` from 4 to about 32 and replace by LRU (refresh
  the entry on a hit). Proof memory is 160 B each, so 5 KiB per graph.
- (c) Optional: do not invalidate on a purely additive leaf change, where the
  leaf is absent before, or writable before and still writable after. A proof
  only asserts "present (and writable)". `UpdateLeaf` already distinguishes
  these cases (G:592-604).
- Invariant: for every graph state and query,
  `Cached(q) == InspectRangeAccess(q)` (it returns only a bool, and invalid
  states are never cached, G:792-794, 826-833).
- Test: extend `tests/apple_agx_gpuva_g3_range_proof_test.c`
  (tests/test_g3_range_proof_cache.py) with a differential fuzz that runs random
  leaf/parent/root/register mutations and queries and asserts Cached == Inspect
  after each step. Add a cyclic test: 6 large ranges proven twice, then the
  second pass costs O(1) lookups per range.
- Expected: removes most of the ~7-10k page walks from the passes that now miss
  (preflight + submit + BeginJob). That could be most of the 92/149/42 us means
  (HYPOTHESIS). Confirm with LOOKUP_STATS in the replay first.

**P2. Elide the BeginJob re-parse on an image-keyed generation hit.**
- Changes: 3a.1-3 (store the key in the image, bump the generation in
  RegisterTable/ResetTableShadow/RetireSystemSubtree), then skip only the
  `AppleAgxG4Parse*Ex` call when the key matches.
- Invariant: if the image is unchanged since bind and the key is equal, the
  parse returns the same result as at submit, which was Ok.
- Test: g4_submit_virtual_replay (tests/g4_submit_virtual_replay.c). For every
  mutation kind between submit and BeginJob (leaf remap/unmap/RO, parent, root,
  table reuse, shadow reset, TVB growth, scene release), assert that the elided
  and full paths give identical refusal codes. Assert that each such mutation
  changes the key.
- Saves up to the 42 us mean, times the measured MAPPING_UNCHANGED fraction.
  After P1 it saves less, because BeginJob would then mostly hit the cache.

**P3. Submit reuses the preflight result when bytes and key are identical.**
- Changes: the per-context record and full `memcmp` from 3b.
- Invariant: the same inputs (bytes, VA, key, scene object/state class) give
  the same parse and the same ResolveOutput.
- Test: a replay that runs preflight and then submit with (i) identical bytes,
  (ii) one byte changed, (iii) an intervening PTE update, (iv) a
  `PrivateManager.Blocks` change, and (v) a resubmission. Only (i) may skip,
  and every outcome must equal that of the full parse.
- Saves the parse share of the 149 us submit hold. Ranked last: it needs more
  state and per-context storage, and P1 already cuts most of the cost.

Not a parse fix, but the owner fix named in EXP2157 (W lock sites :3805, :5887,
:6206 against :4240): graph-scoped locking would keep DWM's completion from
waiting behind GravityMark's validation. P1-P3 shorten the hold; they do not
remove the cross-process serialization.

Next offline step: build the replay with `-DAPPLE_AGX_G3_LOOKUP_STATS` on an
HD GravityMark-shaped stream (TVB 828 blocks, 3-5 RTs). Count `slot()` visits
per parse before and after P1(a)+(b). That checks the HYPOTHESIS in 1.2
before any hardware run.
