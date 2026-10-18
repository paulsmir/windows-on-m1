# Aperture System-Page Ranges Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for the tightly coupled change below. Preserve the existing GPU checkout and hardware-control owner; no new worktree or approval gate is requested.

**Goal:** Accept actual VidMm aperture ranges in system-page units without changing local-allocation or UAT granularity.

**Architecture:** The production software aperture already stores individual 4-KiB system pages atomically. Add bounded admission wrappers for arbitrary system-page ranges, route runtime map/unmap through them, and retain the existing 64-KiB helpers as constrained adapters to the same implementation. No second memory implementation or GPU page-table changes.

**Tech Stack:** C11 portable contracts, Windows ARM64 KMD, pinned WDK26100/MSVC14.44.35207.

**Spec:** Contract below, derived from EXP491 minidump and existing production topology.

## Contract and inspected sources

EXP491 full-owner dump090626-14828-01 has bugcheck10e/b and BuildPagingBuffer
statusC000000D. Arguments atffffa70849098580 contain Operation5,
SegmentId1,OffsetInPages0xffff,NumberOfPages1,MDLffffd20fadaa51a0,MdlOffset0,
Flags0 and no DMA buffer. This is a synchronous aperture-map request, not a
transfer needing an encoded DMA packet. The map lies exactly in the last4KiB
of the256MiB aperture. Do not infer adapter state from bugcheck parameter4;
it is not established as an ADMISSION_CONTEXT pointer.

Inspected: Microsoft DXGKARG_BUILDPAGINGBUFFER members MapApertureSegment and
UnmapApertureSegment; current WDK declaration; render-admission/src/{paging_windows,
memory_runtime_windows,render_memory,memory_windows}.c; shared/src/
{apple_agx_physical_topology,apple_agx_software_aperture}.c;
Asahi mmu.rs ownership overview and current m1n1 hw/uat.py PAGE_BITS14.
Current Mu/native contracts are unchanged frozen477/406; no ACPI resource change.

Windows owns MDL-page lifetime; the existing software aperture records guest
physical pages and does not free them. Runtime owns its software-entry array
and serializes mutation with PagingLock. HVC physical owner and context63 UAT
remain responsible for the established local allocation only, unchanged.
64KiB Local.Use64KPages must not be imposed on aperture system-page operations.

## Global Constraints

- Keep existing64KiB helpers and their tests; no changes to16KiB UAT/HVC ownership.
- Reuse AppleAgxSoftwareApertureMap/Unmap; validation must finish before mutation.
- No IRQ,capabilities,firmware,ANS,scheduler or scanout behavior changes.
- Map and exact unmap share page-count/range semantics; flags unchanged.
- One coherent source commit, exact frozen-source build, one new hardware candidate.

### Task 1: System-page aperture mapping and reverse lifetime

**Files:** modify render-admission/include/render_memory.h,src/render_memory.c,
src/memory_runtime_windows.c,include/render_admission.h,src/paging_windows.c,
tests/render_memory_test.c.

**Interfaces:**

```c
APPLE_AGX_SOFTWARE_APERTURE_RESULT AdmissionMemoryMapAperturePages(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U64 ByteOffset,
    const APPLE_AGX_U64 *Pages, APPLE_AGX_U32 PageCount);
APPLE_AGX_SOFTWARE_APERTURE_RESULT AdmissionMemoryUnmapAperturePages(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U64 ByteOffset,
    APPLE_AGX_U32 PageCount, APPLE_AGX_U64 DummyPage);
```

- [ ] Add real regression: map last4KiB at0x0ffff000 with1 page; resolve exact
  page plus byte offset; unmap only that page to dummy; neighbor unchanged.
  Map a3-page range from0x1000; reject zero count,unaligned offset,out-of-range
  tail and invalid page array atomically. Existing64KiB path must remain GREEN.
- [ ] Run `python3 -m unittest discover -s tests -p test_apple_agx_render_memory.py -v`
  and observe the missing range contract fail before implementation.
- [ ] Generic wrappers validate initialized Memory,4KiB-aligned offset and
  representable index; delegate to existing software map/unmap for bounded,
  all-before-write validation. Preserve64KiB restrictions only in old wrappers.

```c
if (Memory == NULL || !Memory->Initialized || (ByteOffset & 0xfffULL) ||
    ByteOffset / 0x1000ULL > 0xffffffffULL)
  return AppleAgxSoftwareApertureInvalidArgument;
return AppleAgxSoftwareApertureMap(&Memory->Aperture,
    (APPLE_AGX_U32)(ByteOffset / 0x1000ULL), Pages, PageCount);
```

- [ ] Runtime map accepts nonzero count up to aperture capacity and preserves
  MDL offset/count overflow checks; uses generic wrapper under existing mutex.
  Runtime unmap takes an explicit UINT PageCount and passes it to generic unmap.
  BuildPagingBuffer validates unmap SIZE_T count fitsUINT and passes exact count;
  map preserves existing flags and synchronous no-DMA-buffer behavior.
- [ ] Run memory/paging plus full render and feature-contract suites; diff-check.
  Review map/unmap caller signatures and failed-operation nonmutation. Commit
  only listed files and this plan; append exact40-character CHANGES row.
- [ ] Build EXP492 from exact491 frozen source plus committed changed files;
  pinned KMD/UMD/analysis/Universal/Inf2Cat/sign/version and SHA gates.
- [ ] After clean ordinary health, preregister one full-owner natural bind.
  Require previous aperture-map bugcheck gone; collect exact new event/dump
  before further changes. Cleanup exact package, emergency385 only if ordinary
  is inaccessible, restore clean ordinary377/392. Continue first unknown.

## Self-review

Single coupled translation correction, not several independent features.
Generic wrappers reuse production software aperture;64KiB allocation planning
and16KiB UAT are preserved. The observed one-page tail request fits unchanged
topology. Tests cover reverse lifetime and no partial write. No new authority
or architectural capability needed. Execution stays in existing GPU branch;
the user explicitly requires exact current state and isolated ANS work elsewhere.
