# EXP943: locate the missing page-table link in a rejected copy QUERY

WHY THIS HYPOTHESIS:
1. EXP942 actual Explorer PID6184 QUERY of 64KiB at VA0x11001f0000 rejected with
   predicate56 before any UPLOAD. The version3 receipt reports missing_level0,
   missing_index0, reason none because the current caller never populates walk.
2. A real-handler replay with root index1/high VA passed both16/64 profiles.
   The arithmetic works when roots and parents exist. Hardware state lacks a
   link, but the current receipt cannot distinguish root, middle or leaf.
3. ETL event309/333/334/435 did not cover the failure interval; one complete
   offline ETL pass and provider metadata inspection are recorded. Another
   interpretation of the same trace cannot identify the missing link.

WINDOWS CONTRACT: FULL GRAPHICS. DxgkDdiEscape must return the same status and
reject copying from a nonresident/unmapped range. Querying graph metadata under
its existing G3 mutex and filling an immutable diagnostic receipt does not
change its WDDM interface, flags, paging queues, scheduler or memory ownership.
Microsoft DXGKDDI_ESCAPE and pinned WDK26100 were inspected.

AGX/ASAHI CONTRACT: the current graph is the native UAT mapping record. Its
existing read-only GraphInspectRangeAccess classifies missing root, middle and
native leaf; it neither maps pages nor asks m1n1 to change hardware. Asahi
render/queue and m1n1 GPUVA owner contracts remain those validated in942.

TRANSLATION: when the actual KMD CPU staging QUERY encounters a null logical
PTE, inspect only that failing 4KiB page in the existing graph. Store the
returned level/index/reason/VA in the version3 QUERY receipt. If a native leaf
exists but the logical shadow is absent, use a distinct diagnostic reason.
No copied bytes, return values, process locks or allocation references change.

WHAT IS STILL UNKNOWN: the level at which VA0x11001f0000 disappears on real
Windows after MakeResident/MapGPUVA. One exact943 package can distinguish these
causes. A result that merely repeats predicate56 without valid provenance is
inconclusive; do not guess residency or change DCP metadata.

Sources: G3 CopyEscape/CopyPte and CaptureCopyQueryFailure, graph inspect in
shared/src/apple_agx_gpuva_g3_graph.c, version3 decoder and real16/64 replay;
current942 hardware receipt/UMD log; official Microsoft Escape documentation.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP942's real QUERY failure is
currently recorded with reason none, falsely implying no map-specific cause.
The test must show no-table/index2 on an absent middle link, preserve the copy
failure and destination bytes, and leave old v1/v2 receipt parsing unchanged.

Steps: add one reason for logical-shadow absence; fill graph walk on predicate56;
add the current real handler regression; verify16/64 plus decoder; build/sign/hash;
fully roll back942, stage exactly943; take one short hardware discriminator.

Offline result: real-handler missing-middle QUERY records level1/index2/no-table/VA0x4000000 in both16/64profiles. Existing QUERY decode v1/v2/v3 tests PASS and new logical-shadow-absent reason is decoded. Old2b6 handler RED at the exact missing-middle assertion; current handler GREEN. No copied bytes or return status changed.
