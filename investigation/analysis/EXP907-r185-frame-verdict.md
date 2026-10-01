# EXP907 — resident COPY_QUERY admission and the remaining Present boundary

## Verdict

EXP907 ran once. The original Windows boot was
`2026-10-01T00:04:38.3303920Z`; the final window receipt was
`00:17:13.8433898Z`, uptime **755.512997 seconds**, with Code0, eight CPUs,
DWM1220 and Explorer5148 alive. No kernel stop or TDR was observed in that
window. All three late measurements read the currently latched DCP surface
and found zero pixels. The stable-window checkpoint passed; the desktop's
nonzero-scanout goal **did not**.

R185 is the sole driver behavior variable. R186 adds bounded same-DWM-context
receipts, without changing capability, submission, completion or reset decisions.
Crash/trace limits and launch disk checks are diagnostic/recovery controls.
Firmware and Mu were reused byte-exact; there is no new firmware behavior fix.

The decisive remaining observation is not merely an early failed render:
**DWM itself eventually calls DXGI `pfnPresentCb` successfully for an allocation
whose armed KMD context has completed render work, but global KMD Present and
virtual-Present counters remain zero.** This preserves the callback-to-KMD
scheduling/admission question. It does not justify a guessed cap or queue fix.

## Build and offline gates

- Build4, source ZIP `71d863ef`, returned BUILD PASS but KMD analysis had
  C6387 and C28251. It was never staged. The diagnostic-wide review covered
  nullable adapters/SAL inheritance, bitfield narrowing, local initialization,
  integer casts and print formats. Only the two reported SAL causes changed.
- Build5 source commit `3459b98f07ea282832a796c49d5ffdbdafe3349b` and native
  archive commit match. Pinned WDK26100 ARM64 KMD and UMD each report
  **zero warnings, zero errors**. Build log SHA is
  `5269c655d14ed42c30b218e5f81da6065f520841663c19004bccf18bdd2a0a11`.
- INF/SYS/UMD/CAT match the builder receipt and sealed hardware manifest.
  Catalog membership and the unchanged test signer were verified. Builder
  test-root trust remains `UnknownError`; guest stage verifies trusted signatures.
- Both preserved PDBs independently match host size/SHA and PE RSDS GUID/age
  against PDB stream1, age1. Proofs and native archive inputs are retained.
- Final broad discovery: 1187 tests, 15 failures, 38 errors, two skips.
  Exact failing/erroring IDs were compared against the preserved 16F/39E
  accepted baseline log used for EXP906 (`exp905-suite.log`, unchanged reference).
  There are **no new IDs and no FAIL/ERROR class changes**. Only the two serial
  busy-port/lock contention IDs disappeared. The temporary historical-query
  profile16/64 failures were corrected by using the archived implementation for
  archived predicate receipts; current R185 replay remains GREEN. Focused
  query/frame/disk-gate/change-ledger tests pass.
- Semantic literal audit: 75 files, 667/667 MATCH, no unresolved literal.
  Hardware manifest SHA `8cfc1b3b837dffea427d4d523010e5ebe342562727c9f440fd8d9a7948670182`;
  readiness SHA `c32e9922179fe3ba97a41f46848d7908624faef27f758f0ab7581932772bf017`.

The sealed query helper had a host-script namespace error: it expected
`package_sha256` in a hardware manifest whose package pins are in
`artifact_sha256`. This was found before launch. The package and sealed guest
payload were not mutated after staging. Separately preregistered host-only
`EXP907-symbols/poll-frame-corrected.py` SHA
`a6da422c710a6b6a8abfffb81f64f52dd7efb6ec7049b7104fb74448e10561c2`
validates the sealed query source and corrects only those two diagnostic lookups
in encoded read-only execution. Its polls, rather than the refusing packaged
helper, supply the authoritative KMD measurements.

## Matched late scanout

These are **120/300/600 seconds after the exact D589 latch**, not an early
EXP807/808 snapshot relabelled as late. All belong to the original Windows boot.

| Since latch | HV uptime ms | Broker/current DCP swap | Surface IOVA | Sampled PA | Match | Nonzero / pixels |
| --- | --- | --- | --- | --- | --- | --- |
| 120000 ms | 256553 | 10 / 10 | 0x102a0000 | 0x8e0110000 | same_surface=1, in_pool=1 | 0 / 4096000 |
| 300000 ms | 436553 | 10 / 10 | 0x102a0000 | 0x8e0110000 | same_surface=1, in_pool=1 | 0 / 4096000 |
| 600000 ms | 736553 | 10 / 10 | 0x102a0000 | 0x8e0110000 | same_surface=1, in_pool=1 | 0 / 4096000 |

Sequence2 and hash `ff4a55d94eaa2325` are unchanged. CPU-readable views have
`cache_clean=0`; that limitation remains, but these are valid current-surface
measurements and all KMD Present counters also stay zero.

## Same-context frame evidence

Read-only polls began at `00:06:34Z` and repeated approximately every13 seconds.
Every authoritative poll verifies exact INF/SYS/probe hashes, Code0, autologon,
and original boot before/after. The eight-entry context capacity is bounded;
later unarmed contexts must not be inferred from earlier entries.

Examples of DWM1220 / graph process3:

| KMD context suffix | Allocation / canonical VA | Query | Last render submission | Recorded completion |
| --- | --- | --- | --- | --- |
| 87a75780 | c0001540 / 7c0000 | SUCCESS, 16 resident4KiB pages | fence1205, C000000D, branch9 | none |
| 83918800 | 80003340 / e40000 | SUCCESS, 16 resident4KiB pages | fence1848, C000000D, branch9 | none |
| 839064c0 | 80000680 / 14c0000 | SUCCESS, 16 resident4KiB pages | fence2131, C000000D, branch9 | none |
| 83905980 | 400004c0 / 1b80000 | SUCCESS, 64 resident4KiB pages | fence3262, C000000D, branch7 | fence2886 |
| 83467100 | c00044c0 / 5ea0000 | SUCCESS, 16 resident4KiB pages | fence31348, SUCCESS | fence31348 |

Full KMD pointer for the last row is `ffffd78e83467100`.
UMD frame-arm receipts correlate its source allocation/VA with runtime context
`000001b1351e2710`. Three later DWM Present tickets use source `c00044c0`,
canonical VA `5ea0000`, that runtime context, `DrawTerminal=0`, and
`pfnPresentCb` returns S_OK. The before/after tickets report monitored render
fences submitted=completed at **37, 45 and 51**. Those synchronization-object
values are **not** KMD submission IDs and must not be compared numerically with
31348. Diagnostic last-completed state is not a timestamped proof that the
entire dxgkrnl scheduler queue was empty at the precise callback instant.

There are54 matched successful polls; last is00:17:39Z. Last precollection global
non-Present submits: **65117** (64062 was the earlier00:17:31 check). Global Present BLT,
flip and virtual-Present are zero throughout polling. SetVidPnSourceAddress=1
and CommitVidPn=1 are initial setup, not late DWM presentation.

### What is established and what is not

1. R185's current 4KiB resident-page CPU-copy contract admits multiple real DWM
   queries. This is consistent with the deterministic RED/GREEN fix; the old
   EXP906 allocation's exact resident state was not measured, so that historical
   failure is not retroactively proven to have been this same defect.
2. Several DWM render submissions still fail at branch9 or7. In current source,
   branch9 covers G4 parsing **and output-resolution failure** before queueing;
   branch7 covers envelope admission/private-scene state. The bounded frame
   receipt does not carry every downstream parse detail. The global first G4
   failure is not automatically the DWM failure and must not be substituted.
3. Other same-PID contexts complete rendering, including the context correlated
   with successful DXGI callbacks. This rejects the blanket explanation that
   DWM never submits/completes anything. Earlier rejected contexts alone do not
   prove why these later callbacks fail to yield `DxgkDdiPresent`.
4. A later DWM COPY_QUERY at handle80000640/VA0xc0000 fails in the UMD log.
   The global first query receipt is predicate57 (invalid resident PTE), but is
   graph process8/contextffffd78e87a87d40/handle400003c0/VA0x20000, not that DWM
   query. Do not manufacture a same-allocation predicate correlation.
5. TDR receipt `captured=0`: the initiating-packet question was not exercised.
   BLT destination/IPA/copy fields are zero because no KMD Present was entered.
   There is no valid BLT-destination-versus-DCP comparison in this experiment.
   Raw initial source-address0x1500110000 and DCP PA0x8e0110000 are different
   address domains, not evidence of a bad destination by numeric inequality.

## Crash attribution and recovery evidence

No fresh kernel, LiveKernelReports or WER dump source exists after EXP907 stage
in the hidden inventory. System1001,41 and6008 are empty for the experiment;
System1074 records the intended stage shutdown and recovery restart.
Application WER delivers older queued BlueScreen/LiveKernelEvent reports,
including 0x119/0x193, and these are not EXP907 crashes. ApplicationFrameHost
has an access violation; keep it separate from DWM and the KMD boundary.

The original collector's four-hour lookback copied two **prior** small kernel
dumps. Their file headers, not report-delivery times, identify:

- `093026-27875-01.dmp`: PAGEDU64, ARM64, 0x116 with parameters
  ffffd8842621f010/fffff802bcd39e80/ffffffffc0000483/3 — exact EXP906 attribution.
  Source LastWrite Sep30 21:28:00Z, before EXP907 stage.
- `093026-13515-01.dmp`: PAGEDU64, ARM64, 0x101 with parameters
  18/0/ffffa08164fe3980/3. Source LastWrite Sep30 21:41:54Z, also before stage.

Original16 files+manifest17/17 were independently size/SHA verified on the host
before ordered restart. Hidden8 files+manifest9/9 passed separately. Final
authoritative recheck comprises **62** checks: original17, hidden9 and nine
completed periodic sets36. Final host manifest SHA:
`a05495ced5d9c5fc71f52ec338a17fed5e9999b27b395506b42ebc0906e3593e`.
The separately frozen hidden current ETL165937152B/hashc0f2244e passed its own
host gate SHA `77ef6519318e51672b637a8ed1148f2fc347fb6b2ca2879b3720d8847832a774`.
Original256MiB ETL/hash8f9f56b6 remains authoritative for the original boot.

Two process deviations are explicitly recorded, not hidden:

- A parent-stop race left an in-flight17:35 periodic snapshot with no completed
  copy-log entry. An additional consolidated check asserted, but a shell missing
  `set -e` continued the already-gated original restart. The original17 gate had
  passed; the extra consolidated check had not. Its trailing receipt source was
  later gone after the child completed duplicate cleanup. That redundant set is
  explicitly excluded from authoritative evidence; no verdict relies on it.
  Subsequent recovery commands run fail-closed.
- The first hidden deletion gate refused all deletion because the current ETL
  changed and was locked. It was frozen and separately hostverified before retry;
  the old expected hash was not reused to authorize deleting changed data.

Hidden Code45/autologon1/stopped exact oem5 was verified before cleanup.
All six exact dump/ETL paths matched host size/SHA before deletion, restoring
C free5135282176 ->5575036928B. Sources were deleted **before** ordinary restart.
Diagnostics were removed at00:32:44; exact oem5 package/devnode/certificate/service
cleanup at00:34:18 reported staged0/SYS0/UMD0/arms0 and ordered ordinary restart.
No package was removed from a live Code0 guest.

Ordinary recovery is durable. Immutable GPU-visible boot
`2026-10-01T00:35:31.2618250Z` passed identity checks at00:35:58 and00:38:32,
and disk/policy checks at00:36:12 and00:38:45: exactly one inert APPL0002
Code28, staged package0, arms0, SYS/UMD/service/signer0, diagnostics0, eight CPUs,
two disks OK, USB5, TermService Running, autologon1/password present. Final
C free **5582598144B**, above4GiB, and kernel dump2/overwrite1 persist.
No physical intervention or forced reset was needed. These receipts verify the
recovery contract, not actual RDP/input interaction or a nonzero desktop.

## Next causal target

Continue offline at the live DXGI callback-to-KMD scheduling/admission boundary,
using the correlated sourcec00044c0/VA5ea0000/context above and the actual callback
arguments, allocation/swapchain type and companion WDDM contract. Separately
reproduce branch9 parsing/output resolution against admitted4KiB resident input
before declaring another granularity defect. A confirmed deterministic defect
needs its owning-layer RED/GREEN; no synthetic producer, cap probe, guessed
completion, new hardware run or DWM-crash linkage follows from this result alone.

## Evidence locations

- `.local/experiments/EXP907-frame-build5/`: sealed build/package/readiness,
  heartbeat, late DCP `full.log`, corrected frame polls, original/hidden evidence,
  final host gates, cleanup and ordinary durability receipts.
- `.local/experiments/EXP907-symbols/`: preserved PDB/native provenance,
  PE/PDB identity proof, durable baseline/final suite logs and hash-pinned
  supplemental read-only/control scripts.
- `investigation/EXPERIMENTS.md`: original BEFORE/AFTER, every build result,
  disk cleanup, exact hashes and recovery corrections.
