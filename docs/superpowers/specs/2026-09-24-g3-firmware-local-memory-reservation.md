# R63: persistent 64 MiB local-memory reservation — offline design

Status: design only. No firmware, ACPI, stage-2, or KMD memory-owner change is
authorized by this document. The present G3 paging correction is independent.

## Evidence and source contract

- EXP777B in `investigation/GPU_CURRENT_STATE.md`: a live repeated StartDevice
  failed with `C000009A` while allocating the 64 MiB local slab; a subsequent
  cold boot with the same package reached Stage12. This is evidence of a
  restart-sensitive allocation failure, not evidence that any candidate fixed
  physical address is safe.
- Current KMD `memory_runtime_windows.c` sets `ADMISSION_LOCAL_BYTES` to
  `0x04000000`; `physical_memory_windows.c` requests contiguous physical
  memory through `DxgkCbCreatePhysicalMemoryObject`, then a contiguous ADL,
  maps it and translates guest IPA to host PA through m1n1 HVC. The KMD owns
  allocation and release today. The design must replace that owner together
  with the ADL/map contract; merely changing the address source would leave
  VidMm ownership ambiguous.
- Asahi `arch/arm64/boot/dts/apple/t8103.dtsi` describes GPU firmware UAT
  handoff, page-table and TTBR regions under `/reserved-memory` and names them
  in the GPU device's `memory-region`; `m1n1_windows/src/kboot_gpu.c` populates
  the same firmware regions. These are existing firmware carve-outs, **not** a
  known free 64 MiB Windows local-memory slab. Asahi is used for observable
  ownership, with no code copied.
- m1n1 `src/kboot.c` derives usable DRAM from boot args and checks reserved
  ranges. `src/hv_vm.c` tracks guest IPA→PA stage-2 mappings; the low DRAM
  window is translated. A selected carve-out must be an ordinary, continuous
  guest IPA range backed by continuous host PA within the broker's physical
  limit, with stage-2 normal-memory access. Its actual location must be
  obtained from the running boot map, then validated; no constant is chosen
  offline.
- Mu `Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c`
  already reserves framebuffer DRAM by `ReserveMemoryRegion` plus
  `BuildMemoryAllocationHob(..., EfiReservedMemoryType)` before DXE allocation.
  The J313 `J313AppleAgxAbiAdmission.asl.inc` gives APPL0002 MMIO and IRQ
  resources but no local DRAM resource. This supplies an existing Mu pattern,
  subject to a non-overlap and exact-map check for the new slab.
- UEFI 2.10 [memory allocation](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
  says firmware owns the preboot map and applications/drivers must not call
  `AllocatePages` with `EfiReservedMemoryType`. The Mu PEI reservation pattern
  is the appropriate owner. [ACPI address map](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/15_System_Address_Map_Interfaces/uefi-getmemorymap-boot-services-function.html)
  maps that type to reserved memory. Microsoft documents [translated PnP
  memory resources](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/mapping-bus-relative-addresses-to-virtual-addresses)
  and mapping a device-owned range at StartDevice. These sources do not by
  themselves prove that Dxgkrnl will construct an ADL for reserved DRAM.

## Ownership and proposed handoff

1. **m1n1, selection and stage-2.** Choose a 64 MiB aligned, continuous
   physical DRAM interval only after subtracting m1n1, Mu, framebuffer, all
   ADT/Asahi firmware carve-outs, low-window aliases and immutable recovery
   ranges. Validate guest IPA→host PA continuity, normal-memory stage-2
   mapping, broker reachability and the physical limit. Pass the selected
   guest IPA and size through a versioned firmware handoff. Selection failure
   leaves ordinary recovery boot usable and disables G3 before KMD access.
2. **Mu, OS reservation.** Validate that handoff against `PcdSystemMemoryBase`
   and size, then reserve it before DXE via the same resource/HOB mechanism as
   the framebuffer. Verify the final `GetMemoryMap` has exactly one reserved
   descriptor covering the slab and no conventional or boot-services overlap.
   Generate APPL0002 `_CRS` with one read/write memory resource for that
   *guest IPA* and a version/size marker. Do not edit the generated ASL file
   directly. The memory map stops the OS allocator; `_CRS` assigns ownership
   to the devnode. Neither alone is the whole contract.
3. **Windows KMD, borrowing.** At StartDevice, require the exact translated
   resource, size and alignment and match the firmware marker. Map it with the
   supported resource-mapping path, zero it before first use, and expose the
   existing local segment's CPU address, guest IPA and host PA only after a
   broker/HVC continuity check. Do not call the current 64 MiB physical-memory
   object allocation path for this slab. On StopDevice, quiesce GPU work,
   revoke UAT/backings and unmap the CPU view, while leaving the reservation
   owned by firmware for the next StartDevice. Scratch allocations that still
   use Dxgkrnl physical objects remain separate.
4. **Recovery.** Any handoff mismatch, map failure, overlap, or stale GPU
   lease fails closed before publishing local memory. The ordinary GPU-visible
   Code28 image stays the recovery artifact; no reserved-memory candidate is
   installed until its manifest and SHA-256 are preregistered.

## Smallest falsifiable checkpoint

First offline test: feed a synthetic boot map with framebuffer, firmware UAT
regions, low-window alias and a valid 64 MiB gap through the selector and Mu
reservation validator; assert exact non-overlap, map type, `_CRS` size and
guest-to-host continuity. Also reject one-page overlap and noncontiguous
stage-2 backing. First hardware test changes only firmware reservation while
KMD remains disarmed: record pre/post Mu memory maps and APPL0002 translated
resources, prove ordinary Windows SSH/CPU8/storage/USB and no overlap. A
separate KMD candidate then borrows the resource and performs two StartDevice
cycles. If the first firmware checkpoint fails, boot the pinned ordinary
recovery image and do not attempt the KMD candidate.
