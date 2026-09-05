# Firmware-owned IO descriptors before initdata

EXP475 froze the200-leaf Windows inventory and endpoint-start PASS. Next work is deterministic offline, not another management probe. The next initdata hardware candidate is forbidden until complete HwdataA/B serialization is audited and implemented; this document first closes the separable IO prerequisite.

## Sources and ownership

Inspected current native proxyclient/m1n1/agx/initdata.py build_iomappings/build_initdata, fw/agx/initdata.py IOMapping/AGXHWDataB, agx/__init__.py IO Heap, malloc.py alignment, current retained_root/platform/power_mmio C implementations, Windows regionb_memory/regionb sources, shared UAT protection/allocator, local Asahi hw/t8103.rs. Actual EXP475 root/private/ADT launch and register receipts are saved in its hardware-window.log/contract.bin. Mu406 broker resource remains the existing4KiB range; context63/HVC/IRQ/display unchanged. Existing Windows register-read wrapper and pinned WDK contracts remain the transport primitive.

License review: native files MIT and local Asahi t8103.rs GPL-2.0-only OR MIT. Use hardware facts and independently written C serialization, not copied Rust/Python implementation. Any retained substantial external code would require its MIT notice; no such copying is planned.

EL2 owns retained root, private/system/firmware-IO mappings, table pages, publication/TLB and stopped-only teardown. Windows owns existing RAM objects and exact leases only. Windows may read firmware IO descriptor metadata, but supplies no MMIO PA, writable private root or firmware lease/unmap capability.

## IO contract

Fixed J313/T8103 G13/V13_5 inventory:25 descriptor slots, ten nonempty,77 leaves. Fields LE u64Phys/u64Virt/u32Size/u32RangeSize/u64RW. Native IO VA heap begins0xffffffa068000000,16KiB blocks; each allocation reserves align16K(Size+physicalOffset)+16KiB guard. Preserve GMGIFAF physical offset0x1000. Device AP1/UXN/OS PTEs deliberately match last native path; descriptor RW=0 for timer/temperature is metadata and is not a new PTE RO claim. No SGX SRAM on T8103.

Reuse existing shared UAT map/unmap and real descriptor verifier. Keep firmware IO records separate from Windows MappingCount/handles. Add owner-only preparation after retained ACTIVATE, with rollback-safe partial setup and stopped-only CLOSE retirement; never free MMIO backing. All firmware IO mappings validated before exposing ready manifest. Windows public MAP range/backing/permissions unchanged.

Read-only bounded manifest in unused broker offsets0x800..0xb60 (864bytes):64-byte header plus25x32 records. Header magic/version/size/chip/epoch/root/ready/count with reserved zero bytes. Immutable while active; zero/not-ready after close/reset/failure. No writes accepted. Existing wire version moves coherently to3 so the Windows consumer cannot accidentally pair new initdata behavior with an old owner. Windows validates exact version/length/chip/epoch/root/slot table/VA geometry and uses the owner manifest to encode only the existing HwdataB array at0x640..0x960. It must not manufacture mappings from these physical metadata fields.

## Offline proof and remaining gates

Tests must cover all77 real Device leaves, guard holes, private/system/root identity, concurrent200 Windows leaves, every partial allocation failure rollback, foreign UNMAP denied, stale/read-only/malformed manifest, close/restart and no MMIO free. Codec enforces exact descriptor bytes, empty slots and bounded destination; failed validation leaves destination unchanged. Candidate freeze/review/build/sign/hash follows only after later HwdataA/B fields are complete. Hardware checkpoint then initdata acceptance/progress, not repeated endpoint qualifier without new reason.
