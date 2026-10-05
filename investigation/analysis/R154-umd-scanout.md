# R154: EXP867 UMD failures and scanout evidence

Offline evidence pass, 2026-09-28. No Air access, package change, firmware change,
production edit, or hardware run. Scope is EXP867 source
`335031768e44505d33fcbc91b4899fdf13b8fb60`; the integration tree's additional
deferred-Flush change was excluded from this interpretation. Recovery ETL was
not used.

## UMD classification

The final `hardware-evidence/umd.log` and `umd-final-after-loss.log` are identical:
17,986 lines, SHA-256
`90a6017e34fb9a021875a29c4fa40f9e99e757428d03f0ea449c20c998dd2c59`.
`live-umd.log` is an exact byte prefix, 17,055 lines / 511 SetError records.
Use the final file once; do not add these overlapping counts.

| DDI / generated source line | HRESULT | Count | Share of logged SetError records |
|---|---|---:|---:|
| DrawIndexedInstanced / Draw.cpp:297 | E_FAIL (80004005) | 335 | 61.3553% |
| ResourceCopyRegion / Resource.cpp:1321 | E_FAIL (80004005) | 210 | 38.4615% |
| DrawIndexed / Draw.cpp:206 | E_FAIL (80004005) | 1 | 0.1832% |

These are error-composition rates, **not failure probability per DDI call**.
Attempt counts and timestamps are unavailable. Normal diagnostics have a
128-record budget per loaded UMD instance; `reject-` records bypass it
(`umd_runtime_device.c:38–66`). DWM PID1224 and Explorer PID536 each have exactly
128 normal records. Thus neither elapsed error rates nor total successful
DDI/Present counts can be reconstructed from this log.

| OS PID | ResourceCopyRegion | DrawIndexedInstanced | DrawIndexed | Total |
|---|---:|---:|---:|---:|
| 536 (Explorer) | 133 | 128 | 0 | 261 |
| 5996 | 18 | 93 | 0 | 111 |
| 6616 | 13 | 92 | 0 | 105 |
| 1224 (DWM) | 42 | 0 | 1 | 43 |
| 1216 | 4 | 19 | 0 | 23 |
| 4408 | 0 | 1 | 0 | 1 |
| 5444 | 0 | 1 | 0 | 1 |
| 8536 | 0 | 1 | 0 | 1 |

The generated files on the builder were read without modification and their
hashes matched `audit-inputs/native-prepared-result.txt`:

- Draw.cpp: `74aad530a92aa12a838363fe9192d791f93d8dfcb518240e09903cb061055f52`.
  Lines206/297 propagate failure from `AgxD3d10WindowsFlushRetire`, before
  `ResolveState` and the new indexed `draw_vbo`. These 336 records concern
  draining a preceding draw/batch; they do not mean 336 new draws reached KMD.
- Resource.cpp:
  `24fc43101b109b6c2fd206b9b1470611bff028bc2f06a061af45e33f1b353aaf`.
  Line1321 reports `AgxD3d10WindowsFlushStatus` after texture `blit` and `flush`.
  All210 records are this path, not buffer-copy rejection or shape validation.

The generation bodies are in `build-native-asahi-state.py:2282–2336` and
`:2908–2994`. `agx_d3d10_windows.cpp:498–509` returns native transaction failure,
or `LastScreenError`, or fallback E_FAIL when the backend/device is terminal.
The first retained Explorer failure has a direct local explanation:
`umd.log:507–510` shows `any_faults=1`, `Backend.Failed=1`, no current batch,
and `LastScreenError=S_OK`, followed by ResourceCopyRegion E_FAIL. The fallback
branch therefore explains that observation. `agx_win32_gpuva_batch.c:306–313`
sets `Backend.Failed` after native submission preparation/submission failure;
that marker does not distinguish which preceding operation failed. A sticky
failure can produce repeated later copy errors. Do not generalize that one
snapshot into the initial cause of every indexed-draw error or every process.

There are also 2,601 `g4-native-map-va-cb` records with 8000000A (E_PENDING).
`umd_gpuva_windows.c:97–104` explicitly accepts E_PENDING, retains the paging
fence, and returns the asynchronous-result code. They are not 2,601 failed maps
and are excluded from the SetError count.

**Relation to stalled fence36357 is unproven.** These UMD refusals lack scheduler
fence, process-owner token and timestamps. Neither first EnvelopeState nor
QUERY-NoRoot can be joined to a particular SetError by these records. Repeated
terminal errors and missing rendered content are compatible with a failed
submission/retirement boundary, but do not identify the accepted packet that
remained active. The active packet's dump destination is 16KiB at
PA0x8e19a0000/GPUVA0x110cea0000 with all VisibleDestination fields zero
(`active-packet.txt`), distinct from the sampled primary below. This does not
establish the history of other packets or primaries.

## Scanout and Present

`full.log:2503–2506` contains positive evidence:

1. Scanout request sequence2, poolIPA0x8e0000000, offset0x130000,
   PA0x8e0130000, DCP IOVA0x102c0000, 4,096,000 pixels, nonzero0,
   hash`ff4a55d94eaa2325`.
2. DCP A408 APPLIED swap_id10, then **exact D589 latch swap_id10**.
3. Delayed snapshot of the same sequence/address, same all-zero result.

These are two snapshots of **one scanout request**, not two completed rendered
frames. The DCP latch progressed for the sampled black surface. A theory that
this particular request never reached DCP or never latched is contradicted.
Physical screen visibility/input were unobserved. The snapshots perform no
cache cleaning (`cache_clean=0`; `hv_agx_power_mmio.c:260–310`), and do not prove
the contents of every render target or every instant of the run.

The ordinary primary route in `scanout_windows.c:833–938` accepts a valid
SetVidPnSourceAddress allocation and queues the panel surface. It does not
require a completed native-render receipt. Accordingly, the black latch alone
does not establish a completed render-to-primary. Microsoft specifies that
SetVidPnSourceAddress is used both for mode changes and MMIO flips; the missing
original event context prevents distinguishing those here. See
[Microsoft's DDI contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_setvidpnsourceaddress).

The retained UMD log has no `g4-present-enter`, `g4-present-exit` or
`present-callback` record. This is **not evidence that Present never ran**:
all three are budget-limited normal diagnostics. One Explorer presentation
allocation/import is logged successfully; it is not a completed presentation.
The source chain is `AgxD3d10WindowsPresentationSubmit` →
`AgxWin32AsahiContextFlushForPresent` → `AgxD3d10WindowsFlushStatus` →
`AdmissionUmdSubmitPresent` → runtime `pfnPresentCb`
(`agx_d3d10_windows.cpp:679–699`, `agx_win32_asahi_scene.c:316–332`,
`umd.c:647–669`). Native context faults can prevent reaching the callback,
but the retained records do not prove that happened on a specific Present.

Verdict: **no completed render-to-primary is established; “primary never
rendered” is not established either**. The positive black-surface latch puts
the strongest remaining explanation upstream of useful primary content or
its correlation to presentation, rather than failure of this DCP latch.
Do not fix a Present/DCP contract from this evidence. Continue the parent
investigation of accepted fence36357 versus idle native backend; a source/dump
join to that packet is the discriminating evidence, not another count of UMD
refusals. Machine-readable counts are in `R154-umd-summary.json`.
