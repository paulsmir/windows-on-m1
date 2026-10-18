# Native construction address -> Windows owner

AgxWin32AsahiFindAddress enumerates the existing namespace-scoped Windows
NativeBo registry, validates membership before dereferencing a BO, and validates
current owner/generation plus the existing construction allocator's identity.
It checks native coordinate consistency, exact extent, integer overflow and
unique matching ownership. Retired zero-reference BOs are excluded even when
deferred Windows deallocation keeps their storage mapped.

This is caller-serialized construction lookup, not a WDDM GPUVA/PA API and not a
residency proof. A returned BO is borrowed. CaptureAddress immediately invokes
existing CaptureReference/RetainExact under that same serialization discipline;
it assigns only a per-request reference/index. No second persistent registry.

Tests execute original native pool.c with actual UMD owner functions and
controlled external Windows runtime callbacks. They verify exact subrange,
wrong owner/generation, zero/overflow/cross-end/unknown range, a deliberately
inconsistent native coordinate, retired BO rejection and unchanged holds/counts
on failed capture. The separate existing composer tests remain green.

RED source017 failed to link the two missing API implementations (not a hardware
failure). GREEN source018 x64 build/link/execution PASS, ARM64 build/link PASS,
ARM64 execution NOT_RUN. Three host regression suites PASS. Runner records exact
commands and exits and never installs/runs a driver.

Source018 SHA256 da941276ba715420b09a94fa9a6790ed096def7e1e358a564186a9cf8ac22318.
x64 test SHA256 ae426dbfc026cf49226d48099181157957127a0ce500d7bd6d3348e0d8d3cbcb.
Raw logs: investigation/evidence/AD04-native-construction-owner/.
PowerShell redirected text logs are UTF-16LE; retain originals when decoding.

Next remains actual native pipeline emitter capture, not a hardware-ready draw.
Source-class reconciliation, nested descriptor/resource edges, complete native
overlay/DMA and runtime dispatch/lifetime remain required before enablement.
