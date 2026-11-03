# Q21b: EXP2114 blocks on a new Encoder BO's map paging fence

Continuation of [Q21](D3D12-Q21-indirect-hold.md), whose prior report ends at
`9ece9f2c1d0578c86b287409f7e841b3165c2358`. The follow-up starting snapshot is
`8e5d9dd05deac8cf885e2f8d4f48151778655139`, after the integration owner's
pre-existing Q21 CHANGES rows and EXP2114 BEFORE entry. This is the task's permitted
**required-wait finding**, not a driver fix or a hardware validation. The
earlier reclamation change remains default off. No additional switch, driver
change, probe change, package, firmware change or hardware run is introduced.

EXP2114 does not reproduce EXP2113's long `resident_before_map` interval.
It identifies a later, distinct wait: **21.560 ms in the bind's paging-fence
wait after MapGpuVirtualAddress**, for a newly created **Encoder-class BO
smaller than 1 MiB**. Its caller cannot expose the mapping as ready before
completion under the current CPU preparation and admission contract.

## Same-process timeline

Inputs are the supplied `exp2114/tool-q3pc-{std.txt,umd.log}` in
`.local/handoff/codex/q21-indirect-hold`. `decode-exp2114.py` in that handoff
reproduces `q21b-timing.json`, including line numbers, hashes, decoded words
and assertions on ordering. PID10668, TID10660, QPC frequency 24 MHz. The
reclaim option being enabled is supplied by TASK.md and the EXP2114 BEFORE
entry; these logs do not contain an independent configuration receipt or
package hash. The BEFORE entry supplies intended artifact-hash prefixes, not
a runtime identity check performed by this task.

| Boundary | QPC / decoded value | Evidence |
|---|---:|---|
| Measured pair begins | 1456546240 | stdout:30 |
| Head replay starts / ends | 1456725964 / 1456733862 | UMD:531 |
| Head counted path | path3, MaxCount2, reason0 | UMD:513 |
| Head KMD submit | 1456749188, fence5118 | stdout:32 |
| Head owner-to-app queue bridge | ordered1, owner fence24 | UMD:517 |
| Head submit-end / last profile | 1456750472 / 1456750969 | UMD:531,535 |
| Successor replay starts | 1456751350, serial9 | UMD:552,555 |
| Bind paging-wait start, estimated | 1456761293 | UMD:536 minus 21560 us |
| Head KMD completes | 1457265108 | stdout:32 |
| Bind paging-wait receipt | 1457278733; 21560 us, site1, fence low32=0x1bae | UMD:536 |
| BO creation receipt | 1457279030; MiB0, class3, create96/bind21868/associate3 us | UMD:537 |
| Successor replay ends | 1457287417 | UMD:555 |
| Successor KMD submit / complete | 1457309134 / 1457316131, fence5120 | stdout:32 |
| Successor submit ends | 1457311015 | UMD:555 |
| Exact output | vertices1048576, primitives524288; exact_outputs=PASS | stdout:35-36 |

Head residence is **21.496667 ms**. Successor submission is **1.834417 ms
after head completion** and 23.277583 ms after the head's recorded submit-end.
The actual DDI return timestamp is not recorded: its final profile precedes
the successor replay by 15.875 us. Successor replay takes 22.336125 ms; final
submit takes 0.983250 ms. The 21.560 ms wait explains about 96.5% of replay.
The wait receipt is 0.567708 ms after head completion; another 1.266708 ms
passes before next submission. Those are observed intervals, not additional
GPU-idle time or an exact OS wake-up latency.

Entry estimated by subtracting duration from receipt QPC includes diagnostic
overhead and microsecond truncation. It is not a measured callback entry.
The fence receipt records only low32; `0x1bae` must not be described as an
authenticated full 64-bit paging-fence value or confused with KMD fence5118.

## Which wait, and which allocation

Driver-relative paths below start at `drivers/apple-agx/`.

`render-admission/umd/src/umd_gpuva_windows.c:933-947` emits
`measure-gpuva-paging-wait` around `wait_paging_at`. Site1 means the
`PagingWaitToken` left by canonical `map_va` at :575; site5 is the submit
residency wait. `resident_before_map` instead calls `wait_paging_at` directly
with site2 (:529). Thus this long site1 receipt is **not MakeResident callback
time, not its preceding residency-fence wait, and not a CPU indirect count
read**. The chain is:

```
agx_bo_create -> AgxWin32GpuvaBind -> map_va
  -> resident_before_map -> MapGpuVirtualAddressCb returns mapping/fence
  -> wait_paging -> wait_paging_at -> wait_object(PagingSyncObject)
  -> pfnWaitForSynchronizationObjectFromCpuCb
```

`wait_object` (:852-904) uses a zero-initialized request, hence a NULL
`hAsyncEvent` and synchronous CPU wait. The optional completed-fence shortcut
does not authorize skipping an unfinished dependency. The profile independently
names the same leaf: UMD:557 contains these **exclusive** successor buckets:

| Key | Meaning | Calls | Microseconds |
|---|---|---:|---:|
| 0x502 | Dispatch PagingFence | 2 | 21821 |
| 0x60f | Submit Flush | 2 | 459 |
| 0x61d | Submit NativeReadyEscape | 1 | 439 |
| 0x51b | Dispatch SceneEscape | 1 | 269 |
| omitted | All other exclusive buckets together | unknown | 97 |

IDs come from `mesa/winsys/agx_win32_wait.h:5-30`. The other paging wait
contributes roughly 261 us at receipt precision; the receipts do not identify
its individual fence or allocation. Missing RenderFence, MakeResident,
in-flight-window and owner-lock buckets are not zero, but **together with all
other omitted buckets fit in 97 us**, subject to receipt truncation. The
inclusive submit Flush record at UMD:559 is 1842 us across two calls and
already includes nested work; adding it to these buckets would double-count.

`mesa/winsys/agx_win32_asahi_bo.c:452-457,537-544` returns cache hits before
the BO-creation timing path. Therefore UMD:537 describes fresh allocation and
binding, not cache reuse. The receipt format is defined in
`agx_d3d12_windows.cpp:559-585`; `0x60` is **96 us**, not flags or a size.
`shared/include/apple_agx_win32_device_info.h:19` defines class3 as Encoder.
The size word is `bytes >> 20`, so zero means less than 1 MiB, not zero bytes.
The bind consumes 21.868 ms, of which the named wait consumes 21.560 ms;
roughly 308 us remain for reserve, residency, map callback and bookkeeping.
There is no >=10 ms `measure-gpuva-map-time` inside this pair.

**Exact allocation identity is unavailable.** The log contains no token,
allocation handle, GPU VA, label, flags or exact byte count for this creation.
Class3 includes both low-VA pool storage and explicit encoder storage
(`agx_win32_asahi_bo.c:428-430,550-562`). Primary Mesa `agx_batch.c:89-126`
and `src/asahi/lib/pool.c:16-42` allocate a 256 KiB low-VA pool and a 512 KiB
encoder; Windows projects those
through `native-asahi-batch-lifecycle.py:130-148` and
`build-native-asahi-state.py:6065-6066`. Either fits this receipt. Reporting
“the 512 KiB successor CDM” as a measured identity would be unjustified.

## Why reclamation cannot remove every such wait

The Q21 pass only reclaims completed submitted batches. It cannot reclaim the
head while that head is pending. Primary Mesa `agx_batch.c:206-275` releases
pool/encoder references at cleanup, and `agx_win32_asahi_bo.c:321-370` excludes
held allocations and requires exact size/flags/class matches for cache reuse.

The probe warms endpoints **serially**, waiting for each
(`agx_d3d12_probe.c:8773-8801`). That proves retired reuse, not sufficient simultaneously live
storage. In particular, a render with an indirect prelude owns a VDM encoder
plus a CDM encoder (`agx_win32_indirect_stream_emit.h:228-230`), while the next
compute batch needs a CDM encoder of its own. A sequential warm-up can reuse
two encoders; a pending render plus compute needs three. Low-VA pool storage
has an analogous overlap requirement. These are source-supported capacity
explanations, **not a measured cache inventory or a proved cache-miss cause**.
Size/flags mismatches or cache eviction are also not resolved by this trace.

EXP2114 rejects the claim that Q21's reclaim option alone removes this hold.
It does not disprove Q21's completed-storage ordering regression or establish
that the option is useless in other workloads.

## Why the current wait must remain

Microsoft's [map callback contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_mapgpuvirtualaddresscb)
defines E_PENDING as accepted but unfinished.
The [PagingFenceValue contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_mapgpuvirtualaddress)
requires completion before GPU access, allowing either CPU or GPU waiting.
**Windows does not require this particular CPU stall or synchronous
ExecuteIndirect.** The present owner design requires completed mappings for
its CPU-side preparation before it can enqueue the successor:

1. `mesa/winsys/agx_win32_gpuva.c:109-133` waits the returned mapping fence
   before marking the BO Bound. Unknown/missing completion fails closed.
2. `umd_gpuva_windows.c:2690-2698` uploads held data, calls
   `native_held_ready`, and preflights the command envelope **before**
   SubmitCommandCb at :2714. Shadow recording alone does not remove these
   pre-submit consumers.
3. `gpuva_g3_windows.c:1603-1632` validates the live graph and logical/native
   PTE identity during native-ready; :5975-6001 parses the compute envelope
   against the graph. An unfinished fresh map has no equivalent ready proof.
4. Pinned SDK `winsdk26100/um/d3dumddi.h:3838-3850` specifies that a NULL
   async event blocks the CPU. A context GPU wait exists at :3859-3876, but
   cannot order those earlier CPU operations. This driver's SubmitCommand
   structure (:3999-4043) has no paging-fence field.

Moving the wait to submit merely moves this dependency. Removing it, marking
the BO ready early, reusing a busy cached BO or bypassing native-ready would
break ownership/admission. A GPU-side replacement needs a design for deferred
CPU publication/validation and pending-map lifetime, not a wait deletion.

The KMD currently waits for process JobInFlight and LeaseToken to clear before
page-table update/TLB mutation (`gpuva_g3_paging_windows.c:1663-1697,1812-1847`).
This makes a head-dependent mapping delay plausible. **No EXP2114 KMD paging
receipt or stack was supplied**, so the 21.560 ms cannot be uniquely divided
between that loop, VidMm/paging scheduling and wake-up delay. The exact UMD
wait leaf is established; the exact kernel subcause is not. The prelude-pair
gate is still downstream of successor arrival, not the cause of this stall.

Asahi/UMD own reusable storage and CPU preparation; VidMm owns placement and
paging; KMD owns mapping publication, admission, DMA/interrupts/recovery.
m1n1/Mu/ACPI initialization, power and mapping protocols are unchanged. No
live inspection is authorized or performed; supplied EXP2113/2114 observations
are the launch references. No external code is copied.

## Next falsifiable checkpoint, integration owner only

Do not run the same single-pair test again expecting a different result from
early reclamation. The next narrow question is whether **simultaneous storage
demand**, rather than delayed cleanup, forces this first fresh map.

WHY THIS HYPOTHESIS:

1. With reclaim enabled, the measured successor still creates an Encoder BO
   and spends 21.560 ms on its map fence while the head is pending.
2. Existing warm-ups retire each endpoint before the next; they do not create
   the storage demand of pending RP followed by C.
3. The source requires distinct pending encoders/pools, while all omitted
   exclusive wait categories together are only 97 us in the successor.

For a separately preregistered probe experiment, add **one unmeasured RP/C
pair using the same owner, resources and command topology**, enqueueing its
successor before waiting for the pair's final app fence. Retire the pair and
restore sentinels/query state, then run the unchanged measured pair. The sole
variable is warm-up topology (depth one -> the actual RP/C demand), not a
driver allocation special case. Keep reclaim on and Prelude off as in EXP2114.

Collect exact BO identity/size/flags/label and cache hit/miss/held facts on this
behavior run, plus map/residency fences and complete UMD/stdout/KMD receipts.
Do not add a production allocation rule from Q3PC's size, label or draw count.
If the second pair admits its successor before head completion with no long
fresh bind, that supports a bounded, general owner storage-preparation design.
If it still maps, identify the allocation and cache refusal before proposing
such a design. Preserve waits on every genuine cache miss and never reuse
head-held storage. No concrete owner behavior fix is justified by these logs
alone; this task deliberately supplies the required-wait finding instead.

Require exact pixels/compute output, IA vertices1048576/primitives524288,
path3/reason0, no removal/reset, and `head_submit < next_submit < head_complete`
with a PID/fence-matched pending candidate. Early arrival does not establish
prelude execution overlap. Pin source/package/firmware/recovery hashes and
native-build/link every ABI before that separate experiment; retain ordinary
GPU-visible exact-package rollback, dump-first on failure. No hardware action
is part of this task. GravityMark HD/FHD versus EXP2110 5949/4803 follows only
after an actual owner change passes its checkpoint. No performance gain is
claimed from this analysis or by treating this overlapping wait as GPU idle.

## Offline verification

Fresh focused run: **70 tests passed**, including the real projected reclaim
selection/cleanup, real BO cache, paging failure/ownership, GPUVA ordering,
indirect stream/count/statistics, submission window and ABI/native-owner
coverage. Full command: `cd tests && python3 run_against_baseline.py`:

```
ran 846 tests in 336 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

`q21b-targeted.log` and `q21b-baseline.log` in the handoff preserve fresh
results. The six known failures are the same as the initial Q21 report;
`known_failures.txt` is unchanged. No new code defect or behavior change was
implemented, so no new RED/GREEN or native UMD build is claimed. The earlier
24/24 local Windows-target object checks remain historical evidence, not a
new MSVC/link result. Native x86/x64/ARM64 build/link gates remain with the
integration owner before any subsequent package. Hardware/guest/builder and
other worktrees were untouched; EXPERIMENTS/CHANGES are integration-owned.
