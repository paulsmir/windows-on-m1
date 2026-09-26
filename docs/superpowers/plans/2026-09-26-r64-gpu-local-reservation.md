# R64 GPU Local Memory Reservation Implementation Plan

**Goal:** Reserve one 64 MiB J313 GPU local-memory slab before DXE and let the KMD borrow it across StartDevice cycles.

**Spec:** `docs/superpowers/specs/2026-09-24-g3-firmware-local-memory-reservation.md`.

**Observed contract:** EXP822B failed StartStage4 when `DxgkCbCreatePhysicalMemoryObject` requested 67,174,400 bytes (`0xC0000017`); the unchanged R105 matrix did not run. The frozen ordinary Code28 profile remains the recovery artifact. The live J313 ADT (`.local/experiments/EXP476-initdata-inputs/j313-live.adt`) reports DRAM `0x800000000+0x200000000`. Mu logs report framebuffer `0x85F000000+0xFA0000` and low-window backing `0x8A0100000+0x3FF00000`. The ADT carveout map includes large overlapping entries, including region-id-24 across most guest RAM; it cannot be treated as a flat exclusion list. m1n1 standalone maps guest RAM identity at stage 2 and low RAM through a translated window.

**Inspected primary sources:** Asahi `arch/arm64/boot/dts/apple/t8103.dtsi` GPU `memory-region`; m1n1 `src/kboot.c`, `src/hv_autonomous_runtime.c`, `src/hv_vm.c`, `src/hv_agx_power_mmio.c`, and generated J313 layout; Mu `MemoryInitPeiLib.c`, `J313GuestLayout.dsc.inc`, `AcpiPlatform.c`, and generated APPL0002 ASL; KMD `physical_memory_windows.c`, `memory_runtime_windows.c`, `lifecycle.c`; UEFI PI 1.9 HOB, UEFI 2.10 memory services, ACPI 6.5 address map, and Microsoft `MmMapIoSpaceEx` / translated resource documentation.

**Ownership:** m1n1 chooses and verifies a normal, continuous stage-2 IPA/PA interval from the current boot layout; Mu validates it against the PEI system-memory HOB, punches the hole and emits `EfiReservedMemoryType`; ACPI assigns the same IPA to APPL0002 through `_CRS`; KMD maps and zeroes the assigned resource, checks HVC continuity, and borrows it until StopDevice. m1n1 owns stage-2 and recovery from mapping faults. Mu owns the OS reservation. KMD owns runtime GPU mappings, interrupts and quiescence, never the physical pages.

**Differences:** Asahi's UAT/TTB/handoff reserved regions are firmware state, not the Windows 64 MiB slab. Current m1n1 maps guest RAM but has no slab selection/marker. Current Mu reserves framebuffer and low-window backing but no AGX local slab. Current KMD owns a Dxgkrnl contiguous object with an extra alignment unit; borrowing an aligned 64 MiB resource removes that allocation path only for local memory, leaving scratch allocations unchanged.

**Firmware handoff:** Extend the existing read-only synthetic power-broker page with a versioned local-memory receipt at an unused offset after the current `0xC00..0xC3F` hardware-data receipt. Include validity, guest IPA, size, and stage-2 PA. Mu PEI reads it and rejects any mismatch; generated ACPI `_CRS` reads the same validated receipt to return a 64 MiB memory resource. No static slab address enters a generated header or ASL. A missing receipt returns the existing APPL0002 resources and KMD fails closed before local memory publication.

### Task 1: m1n1 selector and read-only receipt

- [ ] Test a synthetic J313 map with framebuffer, firmware, low alias, RAMDisk, a valid aligned gap, one-page overlap, and noncontiguous stage-2 backing; verify a selected gap only when every exclusion and physical limit passes.
- [ ] Implement a pure selector that consumes the current boot layout and relevant firmware carveouts, then verifies each 16 KiB stage-2 leaf before publishing the receipt. Use existing J313 layout values as input, never an offline slab base.
- [ ] Add read-only broker registers and a host test that rejects writes and an absent/invalid selector result.
- [ ] Run targeted m1n1 tests and incremental m1n1 build; commit this independently reviewable change with a CHANGES.csv row.

### Task 2: Mu HOB and ACPI resource

- [ ] Test PEI range validation against the actual system-memory HOB, framebuffer, firmware, low backing and RAMDisk, including an exact one-page overlap rejection.
- [ ] Read the versioned broker receipt in PEI, validate it, then call `ReserveMemoryRegion` and `BuildMemoryAllocationHob(..., EfiReservedMemoryType)` before DXE; fail closed if receipt or HOB validation fails.
- [ ] Generate APPL0002 `_CRS` from the receipt using a separate template for the valid local range; retain ordinary resources when no valid receipt exists. Verify compiled AML exposes one read/write 64 MiB resource with exact guest IPA and a version marker.
- [ ] Run Mu host tests, AML verification and R83 Mu build; commit Mu gitlink and CHANGES.csv row after verification.

### Task 3: KMD borrow and lifecycle

- [ ] RED test: StartDevice with a translated 64 MiB resource must not call `DxgkCbCreatePhysicalMemoryObject` for local memory; scratch allocation remains Dxgkrnl-owned. Reject wrong size/alignment, missing marker and noncontiguous HVC translation.
- [ ] Add a borrowed physical-allocation kind in `physical_memory_windows.c`: map the translated resource through `MmMapIoSpaceEx`, zero it, translate every 16 KiB leaf through the existing HVC path, and unmap without destroying firmware ownership.
- [ ] Construct the existing local `APPLE_AGX_MEMORY_OBJECT` from the aligned borrowed range, and preserve StopDevice quiescence, UAT revoke and unmap order. Emit a durable StartDevice receipt with IPA, PA, size and borrowed owner.
- [ ] Run host replay and affected KMD tests, then R85 ARM64 build and sign/hash gate; commit implementation and CHANGES.csv row.

### Task 4: one cold full-owner verdict

- [ ] Preregister exact m1n1/Mu/KMD build commands, commits, dirty hashes, manifests, SHA-256, recovery artifact, expected StartDevice receipt, R105 matrix and evidence paths in `investigation/EXPERIMENTS.md` before touching Air.
- [ ] Verify pinned SSH and proxy/vUART endpoints. Stage exact package without live bind, perform the R106.1 ordered guest reboot and post-reboot hash/package/arm preflight, then cold full-owner boot.
- [ ] Confirm borrowed-reserve StartDevice receipt before running the unchanged R105 D3DKMT matrix in the same boot. Use the first PASS row as verdict; if all GPUVA rows fail, collect KD evidence as R105 specifies.
- [ ] Collect dump/ETL/receipts first, run ordered exact cleanup, verify ordinary Code28, append the hardware after-entry and update compact state only for a changed proven boundary.

**Smallest falsifiable checkpoint:** PEI reports exactly one `EfiReservedMemoryType` 64 MiB slab; APPL0002 translated resources report that same IPA; KMD StartDevice reports `BorrowedFirmwareReserve` without a local `DxgkCbCreatePhysicalMemoryObject` call. Any mismatch stops before GPU access and restores the immutable GPU-hidden image only if ordinary Code28 cannot be recovered.
