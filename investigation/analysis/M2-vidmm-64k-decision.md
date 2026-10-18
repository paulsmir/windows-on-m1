# M2: when this guest's VidMm maps system memory with 64 KiB pages

Date: 2026-10-12. Source: the guest's own `C:\Windows\System32\drivers\dxgmms2.sys`
(ARM64, 1,108,992 B, sha256 177d4436e0423f299d196aac8758045376ec38ab168e4e259b89005f048a567b),
read symbol-free with capstone (scratchpad helpers armdis2.py, dataxref.py, blxref.py). VidMm
lives in dxgmms2.sys; dxgkrnl.sys has no MinimumPageSize/RecommendedPageSize logic. Microsoft
documents only that system memory is managed at 4 KB, that `SysMem64KBPageSupported` means "the
driver provides memory segment management at 64KB" (WDDM 2.1), and that VidMm picks 64 KB pages
from alignment, size and segment ([Support for 64KB Pages], [GpuMmu Model], [DXGK_GPUMMUCAPS]).
The WDDM 3.x `MinimumPageSize`/`RecommendedPageSize` union over `Alignment` is typed by the
comment as `DXGK_PAGE_SIZE`; the header defines it as `DXGK_PAGESIZE` (d3dkmddi.h:11082: 4KB 0,
8KB 1, 16KB 2, 64KB 4, ... 16MB 12), which also types `DXGK_SEGMENTDESCRIPTOR5.SlabSize`. Read
as the union, the KMD's Alignment 0x10000 is Minimum 4 KB / Recommended 8 KB (R104); EXP836 found
page-size hints refused for Mesa class allocations (R105 PageMode).

## Facts (addresses are RVAs in this image)

1. Global configuration (0xabec8, RtlQueryRegistryValues on
   `HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\MemoryManager`): `Use64KPages`
   (default 0, stored at 0x605a4), `ExpandTo64KBAllocationSizeThreshold` (default 0x400000 =
   4 MiB, at 0x605a8), plus PagingQueueFenceIncrement, RestrictToPreferredSegment,
   BudgetThreshold, AlwaysDecommitOnOffer, LazyDecommitChunkSizeMB, DxgMms2OfferReclaim,
   LargifyUpgradeThreshold{Percent,Bytes}.
2. Per-adapter configuration (0x70060): absolute path = the adapter registry path dxgkrnl returns
   (interface call, type 2) + `\MemoryManager`; values MaxLocalSegmentSize,
   MaxNonLocalSegmentSize, SelfRefreshVramForceEvictionTimer{DC,AC}, **`Supports64KBPages`**
   (default 0 -> physical-adapter byte +0x24 bit 0) and `EnablePromotion` (bit 1). On the guest
   the APPL0002 driver key is `Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0001`, mirrored at
   `Control\Video\{2B8CB696-C60B-11F1-8982-C8A4E6B2EEB0}\0000`; no `MemoryManager` subkey
   exists under either (read 2026-10-12, EXP1234 guest).
3. Allocation creation (function 0x87710; arguments are already unpacked: segment set, preferred
   segments, WDDM2 flags; `[adapter+0x20]` is compared with 0x5023 =
   DXGKDDI_INTERFACE_VERSION_WDDM2_0). The "64 KB pages" allocation flag (bit 0x100 of the
   allocation's dword +0x1c, 0x88b9c-0x88c28) is set when
   - a forcing per-call flag is set (`[..+0x48]` bit 0), or all of:
   - `Use64KPages != 0` **or** the adapter's `Supports64KBPages` (+0x24 bit 0);
   - an adapter capability bit 14 is clear (`[[adapter+0x18]+0xc38]`; bit 14 of
     DXGK_GPUMMUCAPS is OpportunisticSysMem64KBPageSupported -- labelled inference);
   - the allocation's segment set contains an aperture segment (0xa0268 output bit 1:
     a segment whose flags & 0x1001 != 0);
   - the cached GpuMmu caps (`[physical adapter+0x1e0]`) have bit 9 =
     **SysMem64KBPageSupported**, or `Use64KPages == 2`;
   - size is a multiple of 64 KiB, or size > ExpandTo64KBAllocationSizeThreshold;
   - and not w24: w24 = 1 for a CpuVisible allocation (flags bit 0) whose set contains a
     non-aperture CPU-visible segment (0x84ab8: flags & 0x6004 without 0x1001 -- our local
     segment 2) unless a section/backing pointer is supplied, or when allocation flag bit 21
     is set.
     Bit 21 is set (0x889e0) from bit 12 of the incoming WDDM2 flags, which
     DXGK_ALLOCATIONINFOFLAGS_WDDM2_0 names Reserved02 ("repurposed LinkMirrored"), not
     AccessedPhysically (bit 15); this KMD never sets it.
4. Allocation alignment is validated as a power of two ("Allocation alignment (%u) must be power
   of two", 0xba9b4) and rounded to the page boundary (0x88984): this VidMm reads the union as
   `Alignment` on these paths.

## Consequences for this driver

- The KMD declares SysMem64KBPageSupported (R134) but the adapter never opted in through
  `MemoryManager\Supports64KBPages`, so no allocation ever got 64 KB system pages: this explains
  EXP854/EXP1004 (no Use64KBPages update) and the EXP1025/EXP1026 hypothesis (GPU-accessed
  aperture buffers backed by scattered 4 KiB pages, unpublished UAT leaves, hang).
- The opt-in is an adapter registry value VidMm reads itself; the INF can write it with
  `HKR,MemoryManager,Supports64KBPages,%REG_DWORD%,1` (software key; to be confirmed on the
  guest after install that the type-2 path sees it).
- Eligible allocations: aperture in the segment set, 64 KiB-multiple size (our KMD sets
  Alignment 0x10000), not CPU-visible-with-a-CPU-visible-local-segment. Today only aperture-only
  shapes (class0 CPU-visible) qualify; UMA placement (non-CPU-visible GPU allocations with an
  aperture in their set) is the M2 change that makes GPU allocations eligible.
- KMD gap: a Use64KBPages leaf update publishes complete native 16 KiB groups but zeroes the
  logical (CPU-envelope) PTEs (f7c300d6), so the CPU paging executor and the G4 parser cannot
  resolve 64K-mapped VAs. The envelope already exists (16 scaled 4 KiB entries per 64 KiB PTE).

Two size mechanisms therefore exist: the WDDM 2.1 one decoded above (adapter opt-in +
SysMem64KBPageSupported -> 64 KB PTEs, conditions known) and the WDDM 3.2 one (segment
`SlabSize`, allocation Minimum/RecommendedPageSize), whose VidMm handling is not decoded here.
M2 follows the first; the second stays a labelled alternative.

## M2 ladder

- M2-a (offline, deterministic): 64K leaf updates keep the scaled 4 KiB envelope in LogicalPtes;
  test: a VA mapped by a 64K update resolves to ipa + offset (RED on f7c300d6 behaviour).
- M2-b (hardware, one variable): the INF opts the adapter into 64 KB system pages. Observe
  Use64KBPages segment-0 updates and complete groups; nothing else changes.
- M2-c: UMA placement of non-CPU-visible GPU allocations (aperture in the set), then the
  EXP1026 workload without a hang.
- M2-d: m1n1 broker system-backing capacity (v5: 4096 backings = 64 MiB) -> range grants (v6).
- M2-e: shrink the local reserve to firmware + scanout + tables + private needs.

[Support for 64KB Pages]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages
[GpuMmu Model]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model
[DXGK_GPUMMUCAPS]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps

## Addendum 2026-10-13: system memory is not a home for GPU-accessed BOs (EXP1240-EXP1244)

Hardware evidence, in order:

- EXP1240-EXP1242: with CPU-visible class BOs aperture-only (M2-c) VidMm issued 64 KB PTEs and the
  KMD published them, and VA-only render targets work end to end once two validators stopped
  requiring CPU bytes (ad894162, bdd1e258). But CS refused its renders (G4 Parse: attachment or
  render envelope Unmapped) and the compute copy hung (queue timeout, ResetFailed -> 0x116).
- EXP1244 (`Wom1G3SystemShapes`, groups attributed through UpdatePageTable `hAllocation`): every
  system-memory shape is a CPU-visible Mesa class BO; VidMm maps them almost always with 4 KB
  PTEs (64 KB groups ~1%, the documented single-PTE-mode conversion) and backs every shape partly
  with scattered 4 KB pages (0.003% to 37% of a shape's 16 KiB groups). The KMD's
  `DXGK_ALLOCATIONINFO` is identical for all class BOs, so the backing VidMm chooses is not a
  function of the description (VidMm also exposes `LargifyUpgradeThreshold*`, i.e. 4 KB first,
  larger later -- labelled inference).

Conclusion: the AGX UAT maps 16 KiB pages only; VidMm-managed system memory gives no 16 KiB
contiguity guarantee, and the KMD cannot request one for aperture-resident allocations. M2-c
placement is reverted (6e728357); the VA-only output fixes stay (an evicted render target needs
them).

M2 continues through a memory segment whose backing the KMD owns:

- `DXGK_SEGMENTFLAGS.PopulatedFromSystemMemory`: "the memory segment is actually populated from
  system memory that the driver allocated during initialization" (Microsoft d3dkmddi docs). The KMD
  takes the backing from Windows at start (16 KiB-aligned contiguous runs) and returns it at stop:
  the memory stays OS RAM when the GPU is not running, the size is chosen per machine (fraction of
  RAM, registry override) and nothing in m1n1 is machine-specific.
- A `CpuVisible` memory segment is addressed by the CPU at `CpuTranslatedAddress + offset`, which
  needs one physically contiguous range; scattered runs need `SupportsCpuHostAperture`
  (DxgkDdiMapCpuHostAperture) instead. First step: one contiguous range (or a few segments) taken
  early at start; the host-aperture variant only if contiguous allocation proves too small.
- The firmware/RTKit/scanout/table objects stay in the m1n1 reserve
  (`PopulatedByReservedDDRByFirmware`), which then shrinks to their needs (old M2-e).
- Robustness independent of placement: refuse (not hang) a submission that references an
  unpublished BO range, and implement TDR reset (0x116 today).
