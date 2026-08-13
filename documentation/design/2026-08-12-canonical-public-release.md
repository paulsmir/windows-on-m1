# Canonical Public Repository and Release Design

Date: 2026-08-12
Status: Approved for implementation planning

## Context

Windows 11 ARM64 now boots to the desktop on the J313 MacBook Air through the
project's m1n1 hypervisor, Mu firmware, emulated NVMe controller, USB handoff,
and physical-display path. The latest assisted run is noticeably smoother than
earlier builds after adding NVMe queue backpressure and multi-block backend I/O.

The development layout no longer reliably identifies that working system. The
validated run used Mu from `/Users/pavel/public_windows`, but m1n1 was copied
from a dirty stability worktree based on commit `517ec3a`. The standalone image
and its metadata still describe an older m1n1 commit, `cc2a46b`. Historical
images coexist under `.local` and `dist/j313`, and similarly named artifacts
have already caused incompatible framebuffer and launch contracts to be mixed.

The public repository must become the only authoritative workspace before more
platform or driver work begins.

## Goals

- Make `/Users/pavel/public_windows` the only development checkout.
- Preserve every source change and technical conclusion required by the latest
  hardware-validated Windows boot.
- Produce reproducible release and diagnostic profiles from clean commits.
- Prevent m1n1, Mu, framebuffer, memory-layout, and standalone-image mismatches.
- Keep exactly one current artifact set per supported profile.
- Remove obsolete local repositories, worktrees, logs, and binary experiments
  after migration and hardware validation.
- Repair and publish the guarded Windows reinstall workflow used during recovery.

## Non-goals

- This consolidation does not claim that platform freezes are completely fixed.
- It does not add new keyboard, trackpad, GPU, audio, or external-display drivers.
- It does not preserve every experimental binary. Git history and concise
  engineering records replace the binary archive.
- It does not publish unverified Mu changes merely because they exist on another
  branch.

## Canonical Repository Model

The root repository is the integration point and contains documentation,
launchers, builders, installers, tests, and pinned submodules:

```text
public_windows/
  m1n1_windows/       pinned hypervisor source
  mu/                 pinned firmware source
  scripts/            canonical build, launch, and installation entry points
  documentation/      public installation, operation, debugging, and history
  dist/j313/release/  latest generated production artifacts
  dist/j313/debug/    latest generated diagnostic artifacts
```

Only `/Users/pavel/public_windows` may be used to edit, build, launch, install,
commit, or push the project. The old `/Users/pavel/windows` checkout and all Git
worktrees are read-only migration sources until final deletion.

The m1n1 and Mu submodules must each point to named, pushed commits. Detached or
dirty submodule state is rejected by release tooling.

## Source Consolidation

### m1n1

Start from the public stability line ending at `517ec3a`. Migrate the seven
currently uncommitted files from the validated stability worktree as reviewed,
separable commits:

- NVMe multi-block backend transfers;
- one-completion-at-a-time queue backpressure;
- related queue and backend tests;
- only the diagnostics still required by the debug profile.

Temporary timing and bugcheck probes must not leak into production hot paths.
Each retained behavior receives a regression test before its implementation is
considered canonical.

### Mu

Use public commit `9dccb0133f` as the initial known firmware source because it
matches the firmware used by the latest assisted run. Other Mu branches are
compared by source and included only when a required behavior is independently
verified. No branch is merged wholesale based on age or name.

### Root repository

Integrate the latest platform-stability documentation and tests, together with
the guarded Windows reinstall work. Replace the reinstall script's dependency
on `findstr.exe`, which is absent from the tested WinPE environment, and add a
regression test for that exact environment before publishing it.

## Artifact Profiles

Two profiles are supported. They share the same functional platform code and
differ only in explicitly documented observability features.

### Release

- physical display by default;
- no web framebuffer stream;
- no telemetry server;
- no vUART capture;
- m1n1 built with `RELEASE=1`;
- no periodic FIQ, timer, SGI, xHCI, NVMe timing, or watchdog formatting in
  guest hot paths;
- suitable for normal standalone Windows use.

### Debug

- selectable physical, virtual, or mirrored display;
- USB serial logs and optional web display;
- KD and platform diagnostics available;
- rate-limited diagnostics explicitly enabled;
- suitable for assisted hardware investigation.

Both profiles are rebuilt from the same clean source revisions. A profile name
never selects an old binary.

## Provenance and Launch Contract

Every generated profile includes one machine-readable manifest containing:

- root, m1n1, and Mu commit IDs;
- dirty-state rejection result;
- build profile and compiler identity;
- display geometry and framebuffer contract;
- guest RAM and reserved-region layout;
- firmware, m1n1 stages, assisted Mach-O, and standalone image sizes and SHA-256
  hashes;
- standalone manifest format and flags.

Builders replace the selected profile directory atomically. Launch and ESP
installation tools validate the manifest before touching hardware. They fail
closed when an artifact is missing, has the wrong hash, uses an unsupported
manifest, or disagrees with the expected J313 memory/display contract.

This makes the invalid combination of a 1280x800 launcher, a 2560x1600 Mu build,
and an unrelated m1n1 binary mechanically impossible.

## Verification Gates

Consolidation is complete only after all of the following pass:

1. Root, m1n1, and Mu checkouts are clean and on named commits.
2. Root and m1n1 host tests pass, including NVMe queue backpressure and WinPE
   reinstall-script coverage.
3. Release and debug artifacts build from a clean checkout.
4. Manifests and hashes validate for both profiles.
5. Static or binary-level release checks demonstrate that periodic diagnostic
   strings cannot execute in the release build.
6. An assisted debug launch reaches the Windows login or desktop and retains the
   required logs/display.
7. A release launch reaches the Windows login or desktop without web display,
   telemetry, or diagnostic hot-path output.
8. The current standalone image boots Windows from the ESP.

The current successful desktop session proves the value of the latest source
behavior, but it does not satisfy the clean-release or standalone gates by
itself.

## Cleanup and Recovery

Before deletion, preserve in Git only concise provenance and conclusions from
useful experiments: source revisions, artifact hashes, observed failures, and
the final disposition. Do not retain historical boot binaries merely as an
archive.

After the verification gates pass:

- delete `/Users/pavel/public_windows/.local`;
- remove obsolete experimental files from `dist/j313`;
- retain only the current `release` and `debug` artifact directories;
- remove all worktrees belonging to the old private checkout;
- delete `/Users/pavel/windows` completely;
- document the recovery procedure and the last known-good release hash before
  replacing an installed ESP image.

Deletion is the final operation, not a prerequisite for migration. Until then,
the old tree is never used to launch or build Windows.

## Commit and Publication Policy

Changes are committed in small reviewable groups: source migration, regression
tests, release gating, artifact contract, installer repair, documentation, and
cleanup. Commit messages retain technical findings but contain no assistant
attribution, session URLs, or `Co-Authored-By` trailers.

Submodule commits are pushed before the root repository updates its pointers.
The public branch is pushed only after local tests pass. Hardware validation is
recorded explicitly and is never inferred from a successful build.
