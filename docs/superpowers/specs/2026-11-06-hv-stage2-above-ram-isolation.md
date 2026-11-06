# HV stage-2: isolate firmware carveouts above guest RAM (J313)

Status: implemented offline, m1n1_windows `feature/hv-stage2-above-ram` 6698d0f4
(base 0d3a75b3). Default off. Hardware checkpoint HVS2ISO001 is preregistered and has
not been run.

## Problem

`HV.load_raw()` (`m1n1_windows/proxyclient/m1n1/hv/__init__.py`) maps RAM-HIGH from
`phys_base` to the end of DRAM, `ram_base + mem_size_actual` = 0xa00000000. It then
removes only the MCC TZ carveouts (`unmap_carveouts`). On J313 this leaves everything
from `mem_top` = 0x9df708000 to 0xa00000000 that is not TZ as stage-2 pass-through. That
includes the live iBoot DART translation tables that m1n1's DCP depends on.

Live evidence, from the EXP950 `normal2-contract.bin` checkpoint 3 receipt (the same
appears in the EXP2194 and EXP2196 GPU-profile receipts):

| IPA | size | kind |
|---|---|---|
| 0x9df708000 | 0x1c30000 | unmap (TZ1+TZ0) |
| 0x9e1338000 | 0x5ae8000 | **HW pass-through** |
| 0x9e6e20000 | 0x19000000 | unmap (TZ3) |
| 0x9ffe20000 | 0x1e0000 | **HW pass-through** (contains the DART tables) |

From ANSDART001-R3 (ans-native ledger): dart-dcp stream0 L1 is at 0x9fff0c000.
dart-disp0 stream0 L1 is at 0x9fff34000 and stream4 L1 at 0x9fff04000. Their L2 pages
are in the same carveouts.

## Sources inspected

- m1n1: `src/memory.c` (pmap-io-ranges with flags 0x60004016 are mapped Normal-NC; these
  are the CARV and GUAT entries), `src/dart.c` (locked and real-time L1 handling),
  `src/display.c` (the boot framebuffer is `/vram` and region-id-14),
  `src/kboot.c` (`disp_reserved_regions_t8103`: region-id-50/57/94/95, plus 73/74 for
  dcpext), `src/kboot_gpu.c` (gfx-handoff, gfx-shared-region and gpu-region become the
  uat-handoff, uat-pagetables and uat-ttbs reserved-memory nodes),
  `src/hv_autonomous_runtime.c` (the standalone runtime maps guest RAM only up to
  `hv_autonomous_resolve_ram_end()` <= mem_top, so the standalone contract already
  excludes this window), and `src/hv_agx_power_mmio.c` (gpu-region C hook, retained root
  over gfx-shared-region).
- Asahi Linux `arch/arm64/boot/dts/apple/t8103.dtsi`: all of these carveouts are
  reserved-memory. Only the device driver that owns each one uses it, through its DART.
- Mu `T810XFamilyPkg/.../MemoryInitPeiLib.c`: GOP's framebuffer is boot-args
  `video.base`, and `VirtualDisplayValidateFramebufferRange` requires it to be inside
  system RAM. `run_uefi.py` sets it to 0x85f000000, which is guest RAM. Mu reads no
  carveout or `/vram` property.
- Windows drivers: the AppleAgx KMD maps only the SGX MMIO, gpu-region
  (0x9fffb8000), handoff (0x9fff70000) and the power broker, as ACPI `_CRS` in
  `J313AppleAgx.asl.inc`. No driver references gfx-shared-region or any other carveout.

## ADT-derived ranges (ANSDART001-R3 adt.bin, sha256 d1d05756…fa733)

Window: [0x9df708000, 0xa00000000). Kept ranges (stage-2 unchanged, HW pass-through):

| range | source |
|---|---|
| [0x9e2638000, 0x9e5394000) | `/vram` = region-id-14, boot framebuffer |
| [0x9fff70000, 0x9fff74000) | sgx `gfx-handoff` = region-id-56 (ACPI resource) |
| [0x9fff78000, 0x9fffbc000) | sgx `gfx-shared-region` (52) + `gpu-region` (8, ACPI resource) |

Excluded ranges (removed from stage-2):

| range | carveouts |
|---|---|
| [0x9df708000, 0x9e2638000) | 5, 4 (TZ1/TZ0), 74, 73 (dcpext) |
| [0x9e5394000, 0x9fff70000) | 95, 94, 57, 50 (dcp_data), 98, 79, 77, 2 (TZ3), 97, 96, 80, **69, 70, 68, 17, 18, 16** (DART tables, pmap-io `CARV`), 90, 76, 55 |
| [0x9fff74000, 0x9fff78000) | no carveout (a 16 KiB hole, outside guest RAM) |
| [0x9fffbc000, 0xa00000000) | 7, 81 |

The DART-table carveouts together cover [0x9ffeac000, 0x9fff5c000).

## Ownership

| concern | owner |
|---|---|
| DCP/DART init, runtime, IRQ, DMA | m1n1 (host). The guest has no DCP driver. |
| boot framebuffer, `/vram` | kept mapped, as requested. run_uefi points GOP at guest RAM. |
| AGX handoff and gpu-region | the guest KMD through ACPI. Kept unchanged. |
| gfx-shared-region | m1n1 retained-root broker. Kept, so the AGX contract does not change. |

## Implementation

- `proxyclient/m1n1/hv/stage2_policy.py`: pure `above_ram_policy(adt, mem_top,
  dram_end, video_base, video_size)`. It returns sorted, 16 KiB-aligned keep and exclude
  lists that cover the window exactly. Every exclude range is labelled with the
  carveout ids it overlaps.
- `HV.unmap_above_ram()` calls `del_tracer(..., "RAM-HIGH")` for each excluded range
  and prints the policy into the launch log.
- `HV.load_raw(..., isolate_above_ram=None)` turns isolation on only for `True` or
  `WOM1_HV_ISOLATE_ABOVE_RAM=1`. The default stays legacy because an XNU guest owns DCP
  and legitimately maps these regions. `run_uefi.py` is unchanged. To reproduce the old
  behaviour, leave the variable unset.

## Tests

`m1n1_windows/tests/hv_stage2_above_ram_test.py`: 13 tests, GREEN. The fixture
`tests/data/j313_adt_above_ram.json` holds only the carveout map, `/vram`, the sgx
regions and the DRAM pmap-io-ranges, so no serials or MACs are included. The tests
check:

- the exact ranges;
- that the window is fully partitioned;
- that every CARV page and every non-kept carveout is excluded;
- that `pt_update()`, replayed against a fake proxy, leaves the kept pages and all guest
  RAM bit-identical to legacy and unmaps every other page;
- that the legacy sequence equals the EXP950 receipt;
- that the extra stage-2 records fit the 64-entry launch-contract receipt (51 on
  GPU-profile launches, and an overflow aborts the launch);
- that the fixture matches the saved ADT when it is available.

RED check: with `del_tracer` skipped, the isolation test fails at 0x9e1338000.

## Follow-ups (not changed here)

1. **gpu-region C hook is overwritten.** In the EXP2194 and EXP2196 receipts, the
   `hv_map_hook` at 0x9fffb8000 (attributes 0x3) is recorded *before* the RAM-HIGH HW
   map of [0x9ffe20000,+0x1e0000), which replaces it. The guest therefore reaches the
   real gpu-region page, not `hv_agx_retained_gpu_region`. This change preserves that
   behaviour (single variable). The AGX owner should decide whether it is intended.
2. **Turn the default on.** After HVS2ISO001 is validated, `run_uefi.py` should pass
   `isolate_above_ram=True`.
3. **gfx-shared-region.** It is guest-unused by source. It could be excluded after an
   AGX-owner check.

## Addendum 2026-11-06: iBoot carveouts inside guest RAM

m1n1_windows `feature/hv-stage2-above-ram` 23a01cb6 adds this, opt-in via
`WOM1_HV_UNMAP_RAM_CARVEOUTS=1`. Scope came from the ANS session (ce5aee349, "carveouts
inside the guest window").

### Inventory (ANSDART001-R3 ADT; identical in R2, a separate boot)

There are 13 carveouts inside guest RAM [0x850000000, 0x9df708000): ids 19, 23, 158, 159,
163, 165, 167, 176, 179, 180, 181, 182, 190. Together they are 0xc4a8000 bytes (~196 MiB)
inside the span 0x860500000..0x86f518000. region-id-24 = [0x802078000, 0x9df708000) is
iBoot's boot-args kernel range, a descriptor rather than a reservation. The policy skips
it by geometry: it covers the whole guest window.

### Owners: UNKNOWN (offline sources exhausted)

- ADT: every `region-name-id-*` is empty. No property in any node references a PA in
  0x86xxxxxxx. Every coprocessor `segment-ranges` and every nub `region-base` (ANS = TZ3,
  DCP/DCPEXT above mem_top, AOP/PMP/SMC in SRAM, SIO/ISP/ANE/AVE/GFX images below
  0x802078000) lies elsewhere.
- macOS 15.7.8 kernelcache: the only literal carveout id is `region-id-55`, AppleARMBootPerf
  "profile handoff", which is above mem_top. Nothing names the 13.
- Asahi m1n1 `kboot.c dt_set_memory()`: Linux usable memory is
  [boot phys_base, phys_base + mem_size). The only reserved-memory nodes are display
  (region 50/57/94/95/73/74 and /vram), ISP heap, SIO fwdata, SEP firmware, ASC firmware
  segments and GPU. **Asahi Linux therefore uses all 13 carveouts as ordinary RAM.** This
  is broad field evidence that no iBoot-started coprocessor that Linux leaves running
  (SMC, PMP, AOP, ANS) writes them at runtime. It is not proof: Linux does not run every
  coprocessor the way macOS does, and silent corruption would not necessarily be noticed.
- m1n1 already distrusts them: `local_reserve_carveouts_allow()` (AGX local reserve V2)
  rejects any candidate in a carveout other than region-id-24. However,
  `hv_agx_retained_backing_allowed()` (GPU retained backing) does not check carveouts.

### Which Windows launches expose them today

| Mu image | 0x860000000..0x8a0000000 | effect on the 13 |
|---|---|---|
| exp392 recovery (EXP950 normal, HVS2ISO001 ctl/iso) | `reserving preloaded RAMDisk window` (unconditional) | EfiReserved, so Windows never uses them. Stage-2 still maps them. |
| GPU line since EXP1234 (EXP2194/EXP2196 logs) | `window is system memory` | **Windows conventional RAM.** VidMM may hand the pages to the GPU, and m1n1 retained backing allows them. |

So the exposure is real only on the GPU line, which has done ~1000 launches since
2026-10-12 with no attributed corruption. That is weak negative evidence, and I did not
analyse it further (no archaeology pass).

### Fix layer and ordering

The guest firmware owns "what the OS may use", and stage-2 enforces it. Both are needed,
in this order:

1. **Mu (primary fix).** In `T810XFamilyPkg/.../MemoryInitPeiLib.c`, next to the existing
   framebuffer and cpm-impl-reg reservations: for i in 0..255, read
   `/chosen/carveout-memory-map` `region-id-<i>`. Skip entries that cover the whole system
   memory range (the kernel descriptor). For each remaining entry, `ReserveMemoryRegion`
   plus `BuildMemoryAllocationHob(EfiReservedMemoryType)` its page-aligned intersection
   with system memory, merging gaps of <= 16 KiB exactly like the HV, so that
   reserved ⊇ unmapped. This also takes the pages out of VidMM, which removes the GPU DMA
   exposure without touching the AGX path. Cost: ~196 MiB less Windows RAM.
2. **Stage-2 (enforcement).** `WOM1_HV_UNMAP_RAM_CARVEOUTS=1` only with a Mu that
   satisfies 1. The exp392 recovery Mu already reserves a superset (the whole window), so
   the stage-2 half can be tested on hardware before the Mu change. Enabling it with the
   GPU-line Mu and no Mu fix would make Windows fault on its first allocation there.
3. **Launcher.** A preloaded `--ramdisk` writes over the carveouts (window
   0x860000000+1 GiB). `unmap_ram_carveouts(occupied=...)` refuses it. `run_uefi.py` should
   pass its ramdisk, framebuffer and low-memory windows when it adopts the flag. The
   current layout's windows are disjoint from the carveouts (tested); only a payload-bearing
   ramdisk conflicts, and a payload would need the window moved above 0x86f518000.
4. **ANS DMA exclusion (ANS session)** uses the same list (`in_ram_carveouts`, 2 merged
   ranges, or 6 exact with `merge_gap=0`).
5. Optional defence in depth (AGX owner): add a carveout check to
   `hv_agx_retained_backing_allowed()`.

### Receipt budget

Each separate unmapped range costs about 2 records against HV_STAGE2_STATE_MAX_MAPPINGS =
64 (an overflow aborts the launch in `capture_contract`). Six exact ranges would cost
51 + 15 = 66 on the GPU profile, which overflows. Absorbing the four 16 KiB RAM gaps
(64 KiB) gives 2 ranges: +7 total with above-RAM isolation, i.e. 56 records on the normal
profile and 58 on the GPU profile. The simulation is anchored on real receipts: HVS2ISO001
ctl = 49 and iso = 52, which is the simulated +3. If more ranges are ever needed, raise
the receipt capacity in m1n1 C, which is a separate binary change.

### Tests (21/21 GREEN)

- exact and merged ranges;
- the kernel descriptor is skipped for both phys_base 0x850000000 and 0x818000000;
- the current layout windows are disjoint from the carveouts;
- a ramdisk payload window is refused;
- an `pt_update()` replay unmaps the carveouts and absorbed gaps and leaves all other RAM
  pages identical;
- receipt budget <= 64;
- default off.

The fixture holds only carveout, vram, sgx and pmap values. adt.bin is never committed.
