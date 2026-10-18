# J313 AGX G2 Initdata Memory Graph Implementation Plan

**Goal:** Build the complete offline ownership graph for the top-level G13/V13_5 initdata envelope, its four referenced regions, context-zero UAT tables and the m1n1-compatible TTBR pair.

**Architecture:** Generated constants pin the executable m1n1 structure sizes and derive the kernel virtual base from the hash-bound G1 `rtkit_private` region. A freestanding builder owns five DMA-visible objects, places them in one deterministic high-canonical window with guard pages, creates context-zero mappings, encodes the envelope and returns an unpublished TTBR pair. Reverse teardown is retryable and releases UAT pages before data objects.

**Tech Stack:** Python contract generator, freestanding C11, ASan/UBSan host tests.

## Constraints

- Derive platform addresses from the accepted G1 contract; do not handwrite them in driver code.
- Pin G13/V13_5 object sizes to the executable m1n1 layouts, not stale comments.
- Allocate and zero all objects before publishing any encoded pointer.
- Validate and build entirely offline; do not call the builder from `adapter.c`.
- Do not power AGX, write fixed GPU memory, start ASC, send RTKit messages or expose rendering.
- On every allocation or mapping failure, release only owned resources in reverse order.

## Task 1: Generated object-layout contract

- [x] Add RED tests for kernel VA base and exact G13/V13_5 structure sizes.
- [x] Emit the constants only in the Windows generated header.
- [x] Prove ACPI and m1n1 generated outputs remain byte-identical.

## Task 2: Offline memory graph

- [x] Add RED sanitizer tests for exact allocation, mapping, encoding and teardown.
- [x] Add allocation-failure tests for every data and UAT allocation edge.
- [x] Implement the freestanding graph with validate-then-publish semantics.
- [x] Compile the graph into the WDK project while proving no adapter call site exists.

## Task 3: Verify and record

- [x] Run focused tests and the complete public suite.
- [x] Commit code separately, record its exact hash in `investigation/CHANGES.csv`, close this plan and push.
