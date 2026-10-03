# EXP938: project current framebuffer dimensions into AuxFBInfo

WHY THIS HYPOTHESIS:
1. EXP937 corrected the independently proven RunFragment header offsets, but the identical CPU-upload/full-copy test still returns 204800 zero pixels in the same positions. That correction is not sufficient to explain the corruption; stop that hypothesis.
2. Actual EXP936 command input is 2560x1600 with 32x32 utiles. Both AuxFBInfo copies in the materialized template contain width=16, height=16, and the current constructor never updates them. These are framebuffer dimensions, not an unknown capability or guessed register.
3. Asahi builds one AuxFBInfo from cmdbuf.width_px/height_px and copies it into both primary and reload parameters. Current m1n1 G13/V13_5 reflection confirms width/height offsets 0xb8/0xbc and 0x6e0/0x6e4. The actual production constructor regression fails before this fix.

WINDOWS CONTRACT: FULL GRAPHICS driver; RENDER_ONLY pixel checkpoint. The same D3D11 DEFAULT BGRA CPU upload, CopyResource to matching staging, and MapREAD must preserve every pixel. Official Microsoft contracts are linked in EXP936-upload-readback.md and EXP936-local-readback.md. No DDI or capability changes.

AGX/ASAHI CONTRACT: Inspected pinned Asahi fw/fragment.rs AuxFBInfo and queue/render.rs (construction at 614-623 and copies into fragment parameter blocks), plus m1n1 fw/agx/microsequence.py AuxFBInfo/Start3DStruct2/Start3DStruct3 and cmdqueue.py WorkCommand3D. Current m1n1 Construct reflection was executed with V13_5/G13. Existing measured m1n1/Mu/ACPI/UAT/interrupt/power/launch contracts remain unchanged. No external implementation is copied.

TRANSLATION: Add only four scalar stores in AppleAgxG4PatchRenderScalars. KMD projects existing native render width and height into the two AuxFBInfo copies. UMD still owns native render generation; KMD owns firmware serialization and submission; the existing broker owns runtime hardware access and recovery. Preserve all neighboring control fields and the EXP937 offset correction.

ATOMIC CONTRACT: Width and height in the primary and reload copies represent the same framebuffer. Asahi initializes one AuxFBInfo and uses that value in both paths. A mixture of current and template dimensions violates that invariant, so these four stores constitute one geometry projection change.

WHAT IS STILL UNKNOWN: Whether stale AuxFBInfo dimensions cause the observed pixel pattern. One hardware discriminator uses the identical 858bac8c executable with --local-private-upload. Expected: zero mismatches among 4096000 pixels. On any mismatch, collect immediately and reject this as a sufficient cause; do not repeat the same run or add unrelated guesses. Only after pixel success test Present and physical output.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The actual constructor over the materialized 16x16 template must replace both width/height pairs with the current scene dimensions. RED on previous code, GREEN after the four stores under ASan/UBSan. Existing builder, broker, parser and template suites apply. The unrelated virtual-submit replay harness remains blocked by pre-existing missing declarations; it is not claimed passing.

Recovery: freeze original EXP937 evidence first, then ordinary immutable GPU-visible 377/392 recovery, exact 937 package removal, and durable Code28 baseline. Build, sign, hash and PDB gates precede installation of only package938. No GPU-hidden boot or repeated stable-display wait.
