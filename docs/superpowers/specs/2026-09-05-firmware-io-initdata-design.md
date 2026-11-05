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

### Complete HwdataA/B profile input contract after EXP476

Live read-only ADT2026-09-05T12:57:03Z SHA7e2a944d7b2d0900209cfe11be94e2b8012ef497c8c2951111216018a734dbfe confirms one power zone(target30000/offset100/filter6875), no gpu-se overrides and no CS/AFR tables. These absences are requirements to verify at every owner boot, not assumptions. Current v2 snapshot omits this information.

Generate full A0x421c/B0x1884 bytes offline by reusing native constructors and extracting their existing production performance patch block into a shared Python helper with identical behavior. No kernel floating point and no second hand-written mathematical implementation. Profile generator consumes current raw ADT and pins native constructor/helper source identities. Generated header contains complete bytes,32-byte profile identity and canonical raw expected SGX inputs with exact presence/length/data. Include existing33 scalar names,7 geometry/performance properties,9 SE names,15 power-zone names and CS/AFR presence/content. Verify chip8103/board26/os firmwareV13_5 separately in EL2. Unknown/malformed or changed input yields no ready profile receipt; no silent default substitution.

EL2 compares each exact raw property before permitting PREPARE, then exposes a read-only64-byte profile receipt at broker+0xc00:magic/version/size/chip,epoch/root,32-byte identity. Windows requires the current active epoch/root and generated identity, copies profile bytes into its EXISTING HwdataA/B objects, patches only timestamp arena base atB+0x28 and live IO descriptors, with sram0 and GPU-region snapshot value checked. A has no allocation-dependent relocations. Native timestamp arena base is a VA-domain parameter0xffffffa071000000, not the small RegionB timestamp object; no new allocation or map is claimed.

Wire3 is still offline/unreleased and encompasses both the IO manifest and profile receipt; old wire2 cannot negotiate. No Mu resource expansion, raw Windows MMIO admission, private-root disclosure or ownership transfer. Native profile source/helper changes are behavior-preserving serialization reuse, not a new backend. Complete C-copy output must compare byte-exact to independently invoked native constructor/helper output; perturb every expected property and optional presence to prove fail-closed input coverage. Only then consider initdata hardware and audit other existing initdata objects for equally deterministic gaps.

Tests must cover all77 real Device leaves, guard holes, private/system/root identity, concurrent200 Windows leaves, every partial allocation failure rollback, foreign UNMAP denied, stale/read-only/malformed manifest, close/restart and no MMIO free. Codec enforces exact descriptor bytes, empty slots and bounded destination; failed validation leaves destination unchanged. Candidate freeze/review/build/sign/hash follows only after later HwdataA/B fields are complete. Hardware checkpoint then initdata acceptance/progress, not repeated endpoint qualifier without new reason.
