# AD04 compiler milestone — 2026-09-12

Source checkpoint427f0062f86da31bef0c38c5cebd29c8387a2741, isolated integration
worktree only. No hardware, push or architect-branch integration.

## Proven scope

Full NIR (not nir_stub), compiler/util/c11/BLAKE3 dependencies built on x64 and
ARM64. Forty Asahi compiler C units plus two generated compiler units,
disassembler, generated libagx.cpp and existing compute fixture compile and link
on both architectures (45 units). x64 executes both variants successfully;
each160-byte binary matches the pinned native fixture byte-for-byte. ARM64
executable was cross-linked, NOT executed. This is not full shader conformance,
a real agx_screen/context, standard D3D device or accelerated desktop proof.

Executable evidence verifier: drivers/apple-agx/mesa/scripts/verify-fullcompiler-evidence.py.
Evidence: investigation/evidence/AD04-fullcompiler-001/verified.json, x64/ and
arm64/ manifests, independent native and Windows binaries, debugger and FPCR
instruction records. Raw builder evidence remains under
C:\Users\pauls\AD04-fullcompiler-001. Final attempts:
asahi-x64-4aac999030764fef813f32b1e0dcc7fd;
asahi-arm64-37c472b7142e4e52a35fccf1b41da45a.

## Causal corrections

- 32-bit p_atomic_add_return: ExchangeAdd old value -> unsigned modulo sum ->
  updated signed representation. x64 execution/ARM64 compile pass, including wrap.
- Assembly guard: forced C header must not enter upstream BLAKE3 .S sources.
  Actual assembler RED then full dependency GREEN; no SIMD/selftest disabled.
- Windows debug null device: actual debugger observed fopen("/dev/null") return
  NULL, then vfprintf FAST_FAIL_INVALID_ARG. Use NUL only on Windows; retain
  disassembler selftest/assertions. Shader executable RED -> GREEN.
- ARM64_FPCR: Clang's arm64intr.h shadows Microsoft's header but omits this
  constant. SDK26100 ksarm64.h1214 and MSVC14.44 ARM64_SYSREG both encode0x5a20.
  Narrow missing define; executable compile test verifies mrs/msr FPCR output.
- agx_spill.c: include existing Mesa c99_alloca.h, not a new allocator or heap
  substitute. ARM64 full compiler RED -> build/link GREEN; x64 regression GREEN.

All derived Mesa source retains upstream MIT notices. Pinned source is unchanged.
Build uses the upstream graphics compiler graph via softpipe configuration but
builds/links ONLY compiler dependencies, never softpipe or a software renderer.
Toolchain: clang-cl20.1.8/MSVC14.44.35207/SDK26100; Meson1.8.3,
ninja1.11.1.4, Mako1.3.10, PyYAML6.0.2, packaging25.0, ply3.11.
Upstream warnings remain; no warning-free/general conformance claim.

## Workflow accounting

One local GPU model only; no cloud reviewer/subagent during this milestone.
Assembly guard needed one format repair; FPCR needed one causal repair after
actual Clang header resolution; null-device proposal's copied hash had a typo,
so source identity came from deterministic hashing, not model text. Earlier
atomic32 proposal needed Tier-A alias correction. No claim of measured frontier
token savings. Local proposal/preflight/usage records: .local/phase3 on main root.
Latest user policy supersedes routine delegation: direct Tier-A fixes and compact
context; Devstral only for demonstrably useful bulk work. Thirty existing runner/
ledger tests pass. Raw logs are preserved, not default model input.

## Return to existing frontend — architecture question

Windows device owner already creates one KernelContext and holds the runtime.
Current pipe wrapper is project-owned, not upstream real agx_screen/agx_context.
Native agx_bo requires a mapped GPU VA and keeps BO dependencies alive; existing
Windows interface exposes an allocation token and CPU mapping only. Do not use
CPU pointers or private tokens as GPU addresses, infer stable residency from an
open object, or silently switch WDDM memory models.

Primary comparison: agx_bo.h45–100; agx_pipe.c1759–1860 and2423–2478;
agx_device.c502 and740; agx_win32_transport.h; umd_win32_screen.c168;
umd_runtime_device.c124. Microsoft separates physical allocation/patch-list
addressing from stable GPUVA and its residency responsibilities:
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-memory-in-wddm-2-0
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/residency-overview

Decision needed before changing UMD/KMD ownership: expose a versioned,
generation-checked Windows-owned BO binding with explicit validity/lifetime to
the real Asahi platform seam, or keep addresses opaque and provide a complete
relocation translation for native references. Prefer specifying the former,
without physical addresses/private tables/new caps or implied permanent pinning;
compatibility with current WDDM residency must be proven before implementation.
No frontend/ABI change was made while this choice is open.
