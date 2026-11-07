# EXP1929: measure live UMD copy ownership at allocation refusal

## Observed boundary and decision

The saved normal-pool EXP1929 run reaches GravityMark D3D12 benchmark startup,
then cleanly reports OOM: 10k at 38.801 s and 2k at 19.970 s. Both wrappers kill
the uncompleted app at 106 s with WER 0. No GPU fault/reset/recovery or G4 submit
refusal receipt is present. DWM PID 1216 survives both runs. This supports MEM2
failure containment, but does not establish memory savings or benchmark fit.

Evidence root: `/Users/pavel/public_windows/.local/experiments/EXP1929-mem2-b/`.
Within `evidence/EXP1929/`:

- `gm1929a-umd.log:4066`: 128 MiB allocation refusal, General class, flags 0x2c,
  192 active slots. `:4088`: 0x730000 bytes, flags 0xf, 164 active slots;
  `:4090`: first context fault 0x20586; `:4097`: 0x740000 bytes refused.
- `gm1929b-umd.log:3867`: 128 MiB refusal, flags 0x2c, 193 active slots;
  `:3868`: retry, 162 slots; `:3870`: heap E_OUTOFMEMORY. `:4110`: final
  0x730000 refusal and `:4112`: first context fault 0x20586.
- `kmd-receipts-probe-end.txt:158`: pool v8, ReserveFailures=FloorRefusals=20,
  NewSlabFailures=CommitFailures=0. Last refusal requests 7536640 bytes with
  reserved/committed 2460549120, held 2464153600, HighMemory=0 and LowMemory=0.
  Peaks are distinct observations: committed 2568749056, reserved 2567110656.
  Normal segment is 2818572288 bytes (2688 MiB). Held includes committed.
- `gm1929a-err.txt:1`, `gm1929b-err.txt:1`: app OOM times. These are not
  measured device-removal times. The saved wrappers record timeout, not AV.

WHY THIS HYPOTHESIS:

1. The actual floor counter identifies headroom as the reservation gate; zero
   slab/commit failures makes a failed slab allocation a weaker explanation.
2. The first failed 128 MiB request occurs with 192/193 active slots and the
   slot count falls before subsequent refusals. Final pool totals omit this
   boundary, and cumulative upload bytes do not measure retained CPU copies.
3. UMD has independent canonical, private-staging and merge storage. MEM2 only
   eliminated *unmapped* merge creation, so mapped copies may remain large.

Chosen discriminator: measure that storage at the first four allocation
refusals. Do not weaken the proven T8 gate, shrink the segment, change placement,
or free exposed CPU mappings based on incomplete ownership evidence.

## Windows / AGX contract and ownership

- SDK `winsdk26100/um/d3dumddi.h:4303` defines the allocation callback's HRESULT
  boundary; `shared/d3dukmdt.h:1475` separately defines MakeResident and its
  paging/trim outputs. Allocation refusal does not authorize invalid storage.
- Primary Mesa `src/asahi/lib/agx_bo.c:354` attempts allocation, cache reuse,
  eviction, and retry. Local `mesa/winsys/agx_win32_asahi_bo.c:419` likewise
  reclaims eligible cached BOs after a clean callback refusal. Capture before
  this destroys evidence. No external implementation body is copied.
- `render-admission/src/pool_windows.c:395` requires HighMemory set and LowMemory
  clear; `src/render_pool_slab.c:82` counts actual floor refusals. This remains
  the KMD's memory protection policy. `pool_windows.c:527` records the boundary.
- `render-admission/umd/src/umd_win32_screen.c:754` obtains canonical storage;
  `:770` allocates private CPU staging; `:791` now snapshots before rollback.
  `umd_gpuva_windows.c:1027` creates a merge baseline only for a mapped GPU
  writer. `:611` holds slots during transfer; this protects the new reader by
  allowing it to exclude CopyHeld slots before inspecting merge pointers.
- UMD owns these CPU copies and receipt collection. KMD/VidMm owns residency,
  DMA backing and interrupts. m1n1/Mu retain initialization, power and exposure;
  integration owns recovery. No firmware behavior is redesigned here. Recorded
  experiment m1n1 is 0d3a75b3; repository submodule pins remain m1n1 21c3648d and
  Mu 361a2134. No live state was accessed and no submodule source was changed.

## Receipt v1

`AdmissionUmdScreenMemoryRefusal` in `umd_win32_screen.c:600` uses stack storage,
a device-local four-snapshot budget, and the existing slot lock. It does not
allocate, call the runtime, wait for paging, or touch GPU state. Trace emission
occurs after unlocking. Successful allocation behavior is unchanged. The budget
is per device lifetime (zero-initialized with ADMISSION_UMD_DEVICE).

All rows have 16 dwords and the existing PID/TID/QPC trace prefix. Common dwords
0..5 are version=1, OwnerCookie low/high, request Token low/high, sequence 1..4.
The identity is PID + OwnerCookie + Token + sequence. Rows may interleave.

| Row | Dwords 6..15 |
|---|---|
| `measure-memory-refusal` | ClassId, Flags, requested bytes low/high, stage, pending flags, active slots, transitional exclusions, unknown-size exclusions, copy-held exclusions |
| `measure-memory-storage` | category, slots, bytes low/high, mapped slots, mapped bytes low/high, unmapped slots, unmapped bytes low/high |

Stages: 1=AllocateCb failure or missing canonical handle; 2=private staging
allocation after successful canonical allocation. Pending bits: 1=canonical,
2=private staging, 4=borrowed staging. Pending storage is outside category totals.
Categories: 1=canonical handle spans, 2=private staging, 3=merge baseline.
Each snapshot has exactly one header and three category rows, including zeros.

Counts/spans are **logical sizes**, not physical residency, working set, exact
VirtualAlloc commit including rounding, or a total process footprint. Canonical
spans can include borrowed/aliased storage. Categories overlap; do not sum them
with KMD reserved/held/committed. No winsys EXP1207 shadow, shader/compiler host
allocation, OS/runtime overhead, other process, or removed-but-pending owner is
inventoried. Mapped is current slot state, not a promise an unmapped copy can
safely be freed. Transitions/CopyHeld/unknown sizes are explicitly excluded, so
a census with omissions is a lower bound. Copy-held exclusion avoids racing
merge-pointer publication during transfer without changing the producer.

Decode saved traces:

```sh
python3 tools/decode_umd_memory.py /path/to/gm-next-umd.log
```

The decoder joins interleaved identities, preserves 64-bit values, validates
subset totals, and rejects missing/duplicate/incompatible records. Missing rows
must never become a zero-memory measurement.

## Smallest hardware checkpoint (integration only)

Rebuild the UMD and every TU using `umd_internal.h` consistently for all packaged
ABIs; normal package/signing gates apply. This is a UMD diagnostic change, with
no KMD source/header or Mesa projection edit. Keep the normal 2688 MiB segment,
T13 staging, C75c, T8, caps and firmware. Set **Wom1GpuFaultInfoRead=1**; ensure
**Wom1PoolHalfSize is absent/0**. No new device switch is needed.

Use existing short profile, one bounded GravityMark D3D12 2000-asteroid,
1280x720 windowed run (`gm-run.ps1 -Api d3d12 -Asteroids 2000 -Fullscreen 0
-Timeout 90`) with a fresh tag. Set APPLE_AGX_UMD_TRACE_FILE to its output;
existing APPLE_AGX_UMD_REFUSALS_ONLY=1 and APPLE_AGX_UMD_TRACE_SYNC=1 work.
No APPLE_AGX_UMD_DDI_TRACE switch is required for these rows. Collect at first
refusal and stop; the existing wrapper's bounded kill remains the fallback.

Expected diagnostic signal: up to four complete snapshot groups before
reject-screen-create, stage 1 if the KMD headroom boundary repeats; compare the
first and post-cache-reclaim snapshot where available. Retained large unmapped
private/merge spans support a separate lifetime/reclamation investigation.
Mostly mapped copies instead require preserving pointer lifetimes. Small copies
shift the next target to canonical live set and non-UMD physical/commit pressure.
Capture process private commit/working set and OS available/commit budget near
the failure if integration collection supports it; this receipt alone cannot
prove the cause of OS HighMemory becoming clear.

Failures: missing/malformed receipts when the instrumented callback/private
allocation failure occurs; changed return/cleanup behavior; AV, GPU fault/reset,
DWM restart or corrupt output. Clean OOM still fails workload completion. If no
allocation refusal occurs, the receipt hypothesis is inconclusive. Preserve KMD
v8, raw UMD log, WER/dump and liveness evidence before ordinary exact-package
GPU-visible recovery. Use the established emergency fallback only if needed.
Native Windows compilation, diagnostic overhead and workload memory attribution
remain unknown until this checkpoint; this offline task produces no package.
