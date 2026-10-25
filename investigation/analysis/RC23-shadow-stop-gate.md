# RC23: explicit shadow request refusal; spill bridge incomplete

Base 27c3c712e2615db21c58641499013875845a5dfd, offline codex/rc23.
The requested production shadow bridge is not implemented. This change uses
TASK.md's explicit refusal fallback. It does not reduce locked RAM or make
GravityMark's aperture overflow usable. No hardware run is justified by this
branch alone.

## Decision and observed contract

Read RC14/18/19/20 designs and RC18/19/20/22 reports, then inspected the current
production allocator/pool/paging/query paths. Paths below use
D = drivers/apple-agx/render-admission, W = .local/reference/wdk26100,
A = .local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi.

- D/src/render_pool_slab.c:187 provides atomic private ranges, but those are
  locked raw physical slots, not a pageable content owner or broker grant.
- D/src/pool_windows.c:279 acquires MmWriteCombined MDLs. :330 maps normal
  slots into the advertised pool window via MAP_AT. That path gives VidMm
  ownership of window offsets; a shadow cannot steal an empty advertised slot.
- D/src/gpuva_g3_paging_windows.c:1230 maps generic system IPAs with MmCached.
  Sending raw private slab addresses through it would introduce a mismatched
  cache mapping. Copying address fields is not sufficient integration.
- D/src/gpuva_g3_paging_windows.c:973 resolves original paging addresses,
  :1022 encodes deferred operations, :1260 executes them. D/src/paging_windows.c:340
  executes queued records in order. Copy-in while building page tables can
  run before an earlier queued fill/transfer and publish stale contents.
- D/src/gpuva_g3_windows.c:1455 authenticates direct NativeIpa == GuestIpa.
  Removing that proof requires canonical allocation/range/generation ownership
  with authoritative copy-out, not just a second address.
- D/src/pool_windows.c:653 drains slabs, then :658 frees the containing runtime.
  The private drain guard alone is insufficient if a future production caller
  has outstanding leases on shutdown or uncertain revocation.
- W/d3dkmddi.h:4693 and :4741 define update allocation/offset and virtual
  transfer addresses. They do not authorize replacing VidMm-owned PFNs with
  raw locked MDLs without ordered lifetime/content handling.
- A/pgtable.rs:39 defines 16-KiB UAT leaves; A/mmu.rs:1200 and :1221 use DMA
  ranges and native alignment. Linux's native-page backing contract is not a
  guarantee about adjacent Windows 4-KiB PFNs.

The first external contract not established here is gate 2: private raw-page
broker authorization and matching CPU/GPU cache attributes. m1n1 and Mu are
uninitialized gitlinks (21c3648d32423def46086e258e0995c737dc9284 and
361a2134b2f742f331f8d75ae5f60ad4f7f413f1). Their current implementations and
current generated ACPI/launch grants cannot be verified from those directories.
No other worktree, network, or live machine was accessed. This is a limit on
what this change establishes, not proof that the complete bridge is impossible.

Gate 1 cannot safely publish an eligible aperture allocation ahead of gate 2:
UpdatePageTable is not an allocation OOM/retry boundary, and merely acquiring
private leases at CreateAllocation would retain additional locked memory with
no usable, coherent backing. Therefore this branch implements admission refusal
at that fallible boundary, not production aperture admission or unused storage
acquisition. Gates 3–5 (copies, generations, teardown) remain unimplemented.

## Implemented contract

D/src/pool_windows.c:800 reads Wom1SysmemShadow. Only REG_DWORD, length four,
value one requests it; absence, malformed values and registry failures default
off. :936 snapshots the request at PASSIVE pool startup before opening the
broker window. No allocation-time registry reads or live policy mutation.
D/include/render_admission.h:618 holds the internal adapter request bit.

D/src/allocation_windows.c:453 returns STATUS_NOT_SUPPORTED for validated,
explicit GPU-only General/CpuVisible=0 creates when the request is set, before
allocating an object, reserving pages or writing allocation output. This uses
an existing failure status at the existing fallible CreateAllocation boundary.
It applies with or without a pool and takes precedence when Wom1SysmemSpill=1
is also present. No implicit fallback falsely reports shadow availability.
Default requests, CPU-visible buffers and other classes retain existing behavior.
The existing create-DDI receipt records the returned status; no new receipt-only
experiment or diagnostic mechanism is introduced.

Gate 6 is consequently a recognized request with an explicit refusal, not a
working enablement switch. No native coverage check, capability, firmware,
signer, pool size, or recovery path changed. No external code copied.

VidMm owns placement/originals; KMD owns storage, mappings, copies and native
publication; UMD owns residency/submission holds. m1n1 owns stage-2/grants and
Mu owns boot reserves/ACPI. Initialization, IRQ, power and recovery ownership
are unchanged. There is no new assisted/standalone launch contract.

## Verification and remaining checkpoint

The new test executes the actual allocation body: on the original code the
eligible shadow case succeeded instead of refusing (behavioral RED). The real
registry reader was absent (missing-function RED). GREEN covers both switches,
pool presence, classes, CPU visibility and several sizes, with no object or
reservation and unchanged output on refusal. Existing real resolver/encoder/
mapper/executor replay continues to prove original endpoints and queued byte
ordering in both directions, including real atomic private leases. It does
not prove shadow activation or eviction. No new shadow paging test is claimed:
there is no shadow paging implementation to execute. See handoff REPORT.md for
exact test receipts, commits and remaining unknowns.

After an integration change establishes gates 1–5 and passes the pinned WDK
build, the requested candidate is **HEAD plus the completed fix**, with:

- Wom1GpuFaultInfoRead=1
- Wom1SysmemShadow=1
- Wom1PoolHalfSize=1

First checkpoint: measured scattered originals for one GPU-only allocation;
queued fill, two aliases, GPU write, ordered copy-out/eviction, exact readback,
remap, capacity refusal and acknowledged teardown. Capture owners/generations,
copy fences, original/native coverage, retained bytes and grant revocation.
Then GravityMark D3D12 **2k, then 10k**, measuring **no reject-native-allocation,
available physical RAM, and scene presents/min**, plus correctness and DWM
survival. The half-pool size is 1344 MiB on the supplied 6.28-GB machine versus
2016 MiB with Fraction=3; lower pool capacity alone does not prove less total
RAM if private shadows remain locked. Account for both originals and shadows.

WHY THIS HYPOTHESIS: RC22's saved RAM accounting identifies little headroom;
UAT requires aligned contiguous native leaves; the current paging executor
uses original bytes after record construction. An ordered, evictable authority
bridge addresses these constraints; an always-resident duplicate does not.
Integration must preregister exact hashes and preserve known-good recovery,
then collect evidence before exact-package GPU-visible cleanup. No experiment
was performed or preregistered here. This branch with Shadow=1 is expected to
refuse allocation, so do not launch the benchmark as a performance A/B yet.
