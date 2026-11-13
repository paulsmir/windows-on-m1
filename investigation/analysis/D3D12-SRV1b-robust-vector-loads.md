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

## TRANSLATION (proposed)

For an n-dword read at byte offset `o` of a view of `size` bytes (dword multiple), with E = 4n:

1. `whole = o + E <= size` (RC74's limit form: one compare against a preamble constant).
2. Base: `B = whole ? o : (size >= E ? size - E : 0)`. B is dword-aligned because o and size are.
3. One n-dword load at B: `v = load_ssbo(slot, B)`.
4. Component i:
   - `valid_i = o + 4(i+1) <= size` (RC74's per-component test);
   - `j = ((o - B) >> 2) + i`, which is 0..n-1 whenever valid_i;
   - `c_i = valid_i ? v[j] : 0`.
   - `v[j]` is a select chain over the n loaded components (n-1 bcsel). When `whole` holds, j = i, and NIR keeps the chain because `whole` is not constant. Cost: about n(n-1) selects for a vec4 (12) plus n compares, and 1 load instead of n.
5. Root views keep ACCESS_IN_BOUNDS, so none of this applies (unchanged).

Expected G1 table-SRV cost: about 80 + 4 x (12 selects + 4 compares), roughly 140 issued instructions but 7 memory ops instead of 23. The point is the memory requests: one per float4 instead of four.

Alternative to measure first: a `j` select that only distinguishes "whole" from "straddle" (`c_i = whole ? v[i] : per-component RC74 load`). It is not branch-free in memory ops, because the straddle path needs loads, so it is rejected unless the select chain costs more than it saves.

## WHAT IS STILL UNKNOWN

- **Views smaller than E bytes (size < 16 for a vec4):** B = 0 and the load reads past the view.
  - If the view ends inside its resource, the extra bytes are discarded by the selects.
  - If the view sits at the very end of the allocation, the read could leave it. That needs either the owner's guarantee of 16 bytes of slack after every shader-buffer binding (BO allocations are page-granular; to be verified in `agx_d3d12_windows.cpp` buffer creation) or a scalar fallback when `size < E` (one compare and select, no branch, by clamping n).
- Whether the select chain re-schedules well on AGX (offline: production-shape cost of G1 and the GM kernels with table UAV reads).
- GM's UAVs are register-bound. Whether they are root (unchecked) or table (RC74) in GM's root signature decides whether SRV-1b also changes GM's code. The receipts `measure-d3d12-root-views` of a GM run answer it offline.

## Checkpoint

1. Offline: RC74's exactness tests (`test_d3d_buffer_robustness`: every size, every straddle) GREEN with the new form, plus a new straddle case where the vector base moves.
2. Offline: production-shape cost of G1 (table) and of every GM compute capture that has table UAV reads.
3. Hardware only after that: the OPTWIN G1 table rows (an SRV-1 table variant, to be added to the probe) and the GM gate.
