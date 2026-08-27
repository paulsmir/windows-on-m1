# J313 AGX memory ownership core

## Goal

Add the allocation-lifetime and ownership state machine needed by firmware,
UAT tables, queues and later render resources without yet allocating memory in
Windows or making a GPU-visible mapping.

## Address contract

Windows GPU-accessible driver-private memory must be allocated through the
Dxgkrnl IOMMU callbacks.  WDDM 2.4 isolation normally maps logical pages 1:1,
but WDDM 3.0 DMA remapping can deliberately return non-physical logical page
addresses through an ADL.  The shared core therefore calls the value a
*device address* and never derives it from a CPU pointer.  The future Windows
adapter will therefore:

1. call `DxgkCbAllocateContiguousMemory` at `PASSIVE_LEVEL`;
2. obtain the device-visible address through the active Dxgkrnl address model;
3. select a contained 16 KiB-aligned device-address subrange;
4. retain the Dxgkrnl handle and original CPU pointer until the matching free;
5. publish only the aligned subrange into Apple UAT;
6. call `DxgkCbFreeContiguousMemory` only after the object is no longer mapped
   or in flight.

Raw `MmAllocateContiguousMemory*` is forbidden for GPU-visible allocations.
An arbitrary CPU virtual address, physical address, MDL handle or Dxgkrnl
tracking handle must never be interpreted as an Apple UAT output address.
`MmGetPhysicalAddress` is not accepted as a general WDDM 3.x DMA-remapping
solution.  The adapter must either prove the qualified J313 path is 1:1 or
consume the logical pages provided by the WDDM 3.0 ADL contract.

## State model

`empty -> CPU-owned -> prepared -> GPU-mapped -> in-flight -> completed`

An object can be unmapped only before submission or after the exact submitted
fence completes.  It can be released only while CPU-owned or prepared.  Every
allocation retains both the original allocation and the aligned view so cleanup
always uses the exact paired handle.

## Gates

- Host sanitizer tests prove alignment, overflow rejection, rollback, exact
  fence matching and refusal to free an active object.
- The shared core is compiled by every ARM64 WDK profile.
- No DDI calls the core in this change; no allocation, cache operation, UAT
  publication, firmware action or hardware experiment is authorized.
