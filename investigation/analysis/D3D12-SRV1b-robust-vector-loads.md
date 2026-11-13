# SRV-1b: D3D-robust buffer reads as one vector load

Status: design (2026-11-13), offline; implementation after EXP2389. Follows SRV-1 (D3D12-SRV1-buffer-srv-loads.md).

## Problem

The D3D-H6/RC74 robust lowering (`winsys/agx_win32_d3d_robustness.h` `agx_win32_d3d_lower_load_ssbo`) splits every n-dword shader-buffer read into n one-dword loads, each with its own clamp and select. The vectorizer does not merge them back in a gather loop. OPTWIN G1 with SRV-1, production shape:

| Form | Instructions | Memory ops |
|---|---|---|
| table SRV, robust | 170 | 23 |
| root SRV, unchecked | 80 | 7 |
| Apple | 57 | 4 x 16-byte device_load |

The same lowering covers table UAV reads, so the cost is not specific to SRVs.

## WINDOWS CONTRACT

D3D11.3 functional spec 7.5 and SM5 ld_raw / ld_structured: each 32-bit component is read if in bounds, 0 if not. A read that straddles the end of the view returns its in-bounds components. (RC74's form exists for this rule; Honeykrisp's check-once-per-load is Vulkan-legal only.)

## AGX/ASAHI CONTRACT

- `load_ssbo` -> device_load of up to 16 bytes, with alignment = component size (`agx_mem_vectorize_cb`).
- The backend waits for outstanding loads at the end of a block (`agx_insert_waits.c`), so a branch around a load serialises it (RC74). The form must therefore be straight-line.

## TRANSLATION (architect 2026-11-13: one load with a per-component select)

For an n-dword read at byte offset `o` of a view of `size` bytes (dword multiple):

1. One n-dword load at the unclamped offset: `v = load_ssbo(slot, o)`.
2. Component i: `valid_i = o + 4(i+1) <= size` (RC74's per-component test), `c_i = valid_i ? v[i] : 0`.
3. Root views keep ACCESS_IN_BOUNDS, so none of this applies (unchanged).

Cost per n-dword read: 1 load + n compares + n selects, against RC74's n loads + n compares + n selects. No base clamp and no index select chain. The earlier draft clamped the base to `size - E` and re-indexed with about n(n-1) selects; the soft-fault contract below makes that unnecessary.

Why the over-read is safe (this resolves the earlier "view smaller than E / view at the end of its allocation" question):

- The load can read up to 4(n-1) bytes past the view, or anywhere when `o` itself is out of bounds. Those bytes are always discarded by the selects. The only question is whether the access can fault.
- Soft faults are the production contract (RC82). RegionC fault_control is always 0xb (`apple_agx_regionc.c`, APPLE_AGX_REGIONC_J313_FAULT_CONTROL). The KMD reports AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS unless the device value Wom1SoftFaults = 0 (`backend_platform_windows.c`, rc82). Asahi's definition: "Shader loads of unmapped memory will return zero".
- So an unmapped page returns 0 without a fault, and a mapped one belongs to the same process VM (another allocation, read and discarded).
- Soft faults do not cover the texture unit, but this form issues only `load_ssbo` (a device load).

Gating: the form is chosen at compile time only when `agx_win32_soft_faults(dev)` holds, the same key RC82 uses for speculation. Without the feature (Wom1SoftFaults = 0, a diagnostic setting) the pass keeps RC74's per-dword split, which never reads outside the view.

Expected G1 table-SRV form: 7 memory ops instead of 23 (one request per float4 instead of four), about 8 ALU ops per float4.

## WHAT IS STILL UNKNOWN

- Whether one 16-byte load at a 4-byte-aligned offset costs the same as an aligned one on AGX. `agx_mem_vectorize_cb` already permits alignment = component size, and the compiler emits such loads today for in-bounds vectors, so this is a measurement, not a correctness question.
- Production-shape cost of G1 (table) and of every capture with table SRV/UAV reads (offline, `agx_shader_cost.py`).
- GravityMark's UAVs: answered below (not read through the RC74 split, so not a GM lever).

## Checkpoint

1. Offline: RC74's exactness tests (`test_d3d_buffer_robustness`: every size, every straddle) GREEN with the new form, plus a new straddle case where the vector base moves.
2. Offline: production-shape cost of G1 (table) and of every GM compute capture that has table UAV reads.
3. Hardware only after that: the OPTWIN G1 table rows (an SRV-1 table variant, to be added to the probe) and the GM gate.

## GravityMark check (offline, 2026-11-13)

The question "are GravityMark's UAVs root or table" from WHAT IS STILL UNKNOWN is answered from the EXP2387 gm r2 measure-d3d12-root-views receipts:

- 179 of 180 compute shaders bind UAVs; 32 of them have a table (robust-checked) UAV. Those 32 include the top of the ranking: bdb1944a (85.8 % of compute instruction work, UAVs 0xf, root 0xe, table u0), c5af950a (4.0 %) and 17de760d (2.2 %).
- Production-shape cost with the receipts' root views, compared with the table UAV marked unchecked:
  - bdb1944a 3514 = 3514 instructions, nir-mem 234 = 234;
  - c5af950a 196 = 196;
  - 17de760d 108 = 108.

  The table UAV is not read through the RC74 load split in these kernels, so SRV-1b changes nothing in them. SRV-1b stays a general-defect item, not a GravityMark lever.
