# EXP938: correct ISP_MTILE_SIZE field offsets

WHY THIS HYPOTHESIS:
1. EXP937 still produces exactly204800 bad pixels in the large CPU-upload/full-copy test. CPU-upload/1x1-copy from the same coordinate works, while large GPU writes fail; focus on the raster geometry consumed by AGX.
2. The actual native command is2560x1600, utile32x32. Asahi derives80x50 tiles, macro dimensions20x16. Current m1n1 G13/V13_5 reflection puts the packed Y/X utile pair at0x3e8/0x3ea. Our stores target0x3e0/0x3e2, which are an unknown U64. The real template retains4x4 at the correct field.
3. The stale macro width is128 pixels instead of640, a fivefold difference. This agrees with the observed repeated horizontal fragments and corruption boundaries in multiples of128 pixels. That relationship is a hypothesis, not proof of the visual cause; the field-layout defect itself is proven and reproducible offline.

WINDOWS CONTRACT: FULL GRAPHICS driver; RENDER_ONLY checkpoint. Identical D3D11 CPU green upload, CopyResource and MapREAD must preserve all4096000 pixels. Microsoft contracts linked inEXP936 upload/local plans. No capability, DDI, scheduler or admission change.

AGX/ASAHI CONTRACT: Inspected pinned Asahi fw/fragment.rs JobParameters2 (unknownU64 before utiles_per_mtile_y/x), queue/render.rs tile_info and job_params2 constructor. Inspected current m1n1 cmdqueue.py WorkCommand3D and microsequence.py Start3DStruct1, executed Construct offsetof withV13_5/G13. Actual template bytes are4x4. Existing measured m1n1/Mu/ACPI/UAT/IRQ/power/launch ownership is unchanged; only KMD serialization changes. No external code copied.

TRANSLATION: Move the two existing16-bit stores from0x3e0/0x3e2 to0x3e8/0x3ea. Values already derive from native geometry. Preserve the unknownU64 and all neighboring fields. UMD generates the render; KMD builds firmware jobs; existing broker owns hardware execution/recovery.

ATOMIC CONTRACT: ISP_MTILE_SIZE is the packed Y/X utile-size pair. Asahi packs these together for this single register/parameter. Both offsets move as one layout correction; no independent feature is bundled.

WHAT IS STILL UNKNOWN: Whether this exact serialization defect explains the spatial corruption. Same858bac8c --local-private-upload must yield0bad/4096000; any mismatch rejects sufficiency immediately. Only after success test shared clear/readback, real Present and physical image. No long stability wait before correct output.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The actual production constructor over the actual template must write8x12 for1280x720 and16x20 for2560x1600 at the primary-source offsets, preserving unknown0x3e0. RED observed on previous code, GREEN underASan/UBSan after moving the two stores. Run existing builder/broker/parser/template suites, then pinned native/WDK/sign/hash/PDB gates.

The independent AuxFBInfo16x16 omission is deferred. Commit2aaf9e38 was never built or installed and was reverted by90db80be before this experiment; do not include its four stores. Its evidence remains inEXP937 plan and Git history. EXP938 changes only the ISP_MTILE_SIZE pair relative to937.

Recovery: freeze original937 evidence, normal immutable377/392 GPU-visible recovery, exact937 removal and durableCode28 before staging only938. Firmware artifacts remainbdcf/MuR143. Preserve manifests and rollback artifacts. No third equivalent historical comparison; this is one new, source-proven field discrepancy tied to measured geometry.
