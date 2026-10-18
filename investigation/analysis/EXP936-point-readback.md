# EXP936 copy one known bad pixel

WHY THIS HYPOTHESIS: Full same-process readback repeats zero at2432,704 and204800badpixels; cross-process opening is excluded. CPU-upload discriminator blocked at native CPUmap beforecopy. Same source clear followed by a 1x1 copy from knownbadposition distinguishes badsourcecontent from fullframecopy/readback without CPUmapping source.

WINDOWS CONTRACT: RENDER_ONLY CopySubresourceRegion with explicit boundedbox2432,704..2433,705, same BGRA format, unmapped source/destination, destination1x1; synchronous MapREAD exposes copied pixel. Width/height/box must change together to represent that one pixel. Microsoft CopySubresourceRegion documentation.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion

AGX/ASAHI CONTRACT: SourceClear/event unchanged936. Inspected ResourceCopyRegion projection uses actualbox and nativepipeblit; destination native staging path. No power/DART/firmware/layout/capabilitychange. Original measured936/m1n1/Mu contract/recovery377392 unchanged.

TRANSLATION: --local-point only narrows readback to one knownbad sourcepixel; same readbackchecker expects green. Green proves this sourcepixel survives clear and narrows fullframecorruption to downstream copy/layout; zero leaves clear/store or common addressmapping candidates. Cannot generalize onepixel to correctfullimage.

WHAT IS STILL UNKNOWN: Which operation produced the reproducible zero. Bounded25s, same exactboot/package gates; source/hash/ARM64PE beforehardware.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Hardware-only corruption boundary; no manufacturedRED test.
