# OV6: provable vertex-stage independence -- finite access ranges (2026-11-08)

Class: C (the UMD's independence verdict must not refuse a render for the tracker's own
limits) + I (consumer: the OV5 TA waiver, parked on the EXP2214 hang). Offline only.

## Evidence
- EXP2274 (fork 6): R2/R4 reason 11 (unplaceable), R6 reason 10 (vertex conflict, an upper
  bound: root-bound views and ExecuteIndirect arguments entered the sets as [offset, ~0)),
  R8 0. Apple (EXP2201) starts R16 (our R6 by position) during c15 and R20 (R8) during c19.
- Source: `agx_d3d12_ddi.cpp JobHeapRange/JobDescriptorRange` emit `~0ull` for every access
  without a size (root CBV/SRV/UAV, texture views, indirect args/count); the range set holds
  48 entries and reports one `Overflow` for both "full" and "no heap".

## WINDOWS CONTRACT (D3D12)
- ExecuteIndirect reads `(MaxCommandCount - 1) * ByteStride + record` bytes from
  `ArgumentBufferOffset` and 4 bytes of the count buffer; the count is at most MaxCommandCount.
- A root descriptor (CBV/SRV/UAV) is a GPU VA inside one buffer resource; accesses beyond the
  resource are undefined, so the resource's extent bounds the access (a root CBV is also at
  most 64 KiB, D3D12 constant-buffer limit).
- ClearUnorderedAccessView writes exactly the view's resource range.
- A placed resource occupies `[HeapOffset, HeapOffset + Width)` of its heap.

## TRANSLATION (UMD only, no KMD/firmware change)
- `AgxD3d12Heap` keeps the placed buffers' spans (`HeapAddSpan` at CreatePlacedResource,
  freed with the heap, never on an alias heap); `HeapSpanEnd(heap, offset)` = the largest end
  of the spans containing the offset, else the heap's extent.
- `JobHeapRange/JobDescriptorRange`: an unsized access ends at `HeapSpanEnd`, never at `~0`.
- `JobIndirectRanges`: the argument range from `record_bytes` (new field of
  `AgxD3d12Indirect`, copied from the command signature) and 4 bytes of the count buffer.
- UAV clears are placeable (their view's range, write).
- `AGX_D3D12_JOB_RANGES` 48 -> 128; `Overflow` is a bit set: 1 full, 2 no heap.
- Receipt `measure-d3d12-ov2-refusal` value 27: per render-after-compute nibble
  `render.Overflow | compute.Overflow << 2` (28 values; cap 32).

## WHAT IS STILL UNKNOWN
- Whether GravityMark's R2/R4 (fork 6 numbering; our R1/R3) become placeable with 128
  entries, or still carry a no-heap access (GS/HS/DS use, an exotic clear) -- the new nibble
  says which.
- Whether R6's vertex ranges really overlap C5's writes once finite -- the verdict then is
  exact, not an upper bound.
- The EXP2214 hang stays the gate for using any verdict on hardware (separate reproducer).

## Checkpoint
Receipt-only GravityMark run (no waiver): the ov2-refusal rows show value 26 (no-prelude
verdict) and value 27 (overflow nibbles) per R2/R4/R6/R8; expected: 11 disappears or
is explained by a 2 (no heap) nibble; R6 either stays 10 (real conflict) or drops to 0.
