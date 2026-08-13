# Canonical Public Release Consolidation Implementation Plan

> **Execution rule:** Work only in `/Users/pavel/public_windows`. Treat
> `/Users/pavel/windows` and every existing worktree as read-only migration
> sources until the final cleanup task.

**Goal:** Turn the public repository into the sole reproducible source for the
latest hardware-validated J313 Windows platform, with clean release/debug
profiles, guarded artifact provenance, repaired recovery tooling, and no stale
binary archive.

**Architecture:** The root repository pins clean m1n1 and Mu commits. Build
scripts generate replaceable `dist/j313/release` and `dist/j313/debug` trees,
each governed by a complete manifest. Launch and installation scripts validate
that manifest before hardware access. Historical binaries are replaced by
tracked engineering notes and deleted only after host and hardware gates pass.

**Technology:** Bash, Python 3 `unittest`, C host tests, m1n1 C/ARM64 build,
Mu/EDK2 build, Git submodules, SHA-256 manifests, J313 hardware validation.

## Evidence and Sources Inspected

- Live result: the latest assisted physical-display run reached the Windows 11
  desktop and exhibited substantially fewer stalls after the NVMe changes.
- Public root history and status at commit `c0840e4`.
- Public m1n1 line through `517ec3a` and the seven-file dirty stability delta.
- Public Mu history through `9dccb0133f`.
- `m1n1_windows/Makefile` release configuration and
  `src/hv_runtime_diag.h`.
- Hot-path diagnostics in `src/hv_exc.c`, `src/hv_vgic.c`, and the xHCI/NVMe
  paths.
- Existing root builders, launchers, artifact tests, and public documentation.
- Historical `.local` build records and current `dist/j313` hashes.
- Guarded reinstall implementation and the observed WinPE failure caused by
  missing `findstr.exe`.
- Project source-first and change-discipline requirements in `AGENTS.md`.

## Observed Contract

- Platform: Apple M1 MacBook Air J313, eight guest CPUs.
- Working firmware source baseline: Mu `9dccb0133f`.
- Working m1n1 behavior baseline: `517ec3a` plus the reviewed NVMe batching and
  queue-backpressure delta.
- Storage: Apple ANS backend exposed to Windows as an emulated PCIe NVMe
  controller with 4096-byte LBAs.
- Display: physical 2560x1600 framebuffer for the current production path;
  virtual/mirrored display is diagnostic-only.
- Assisted launch owns chainloading and host observability. Standalone launch
  owns autonomous stage sequencing and must consume the same guest contract.
- Mu owns the UEFI/ACPI presentation to Windows. m1n1 owns stage-2 mappings,
  interrupt virtualization, timers, PSCI, physical-device preservation, and
  emulated NVMe execution.

## Task 1: Establish a Clean Canonical Branch Without a Worktree

**Files:**
- Modify: root Git branch and submodule pointers only
- Inspect: `documentation/PLATFORM_STABILITY.md`
- Inspect: `tests/`

1. Record root/submodule status, branches, remotes, and exact hashes.
2. Create or switch the public checkout to a named `codex/` consolidation
   branch in `/Users/pavel/public_windows` itself.
3. Import the root platform-stability checkpoint by reviewing its patch rather
   than blindly merging its submodule pointer.
4. Run the root test suite before further changes.
5. Commit only the reviewed root checkpoint files.

## Task 2: Canonicalize the m1n1 Stability Source

**Files:**
- Modify: `m1n1_windows/src/hv_nvme.c`
- Modify: `m1n1_windows/src/hv_nvme_queue.c`
- Modify: `m1n1_windows/src/hv_nvme_queue.h`
- Modify: `m1n1_windows/src/nvme.c`
- Modify: `m1n1_windows/src/nvme.h`
- Test: `m1n1_windows/tests/hv_nvme_queue_test.c`
- Review separately: `m1n1_windows/src/hv.c`

1. Move the public submodule to a named branch based on `517ec3a`.
2. Add/retain failing host assertions for one-completion backpressure and
   batched multi-block transfers.
3. Run the focused queue test and confirm it fails without the implementation.
4. Apply the minimal NVMe implementation delta from the read-only source.
5. Remove or gate timing prints; do not mix bugcheck-probe code into this
   functional commit.
6. Run the focused test and the complete m1n1 host suite.
7. Commit and push the m1n1 source before updating the root pointer.

## Task 3: Separate Debug Diagnostics From Release Runtime

**Files:**
- Modify: `m1n1_windows/src/hv.c`
- Modify: `m1n1_windows/src/hv_exc.c`
- Modify: `m1n1_windows/src/hv_vgic.c`
- Modify: `m1n1_windows/src/hv_nvme.c`
- Modify as needed: xHCI/diagnostic source files
- Test: existing/new release diagnostic host tests

1. Add a regression test enumerating forbidden periodic diagnostic output in a
   release build.
2. Confirm the test exposes any unconditional hot-path prints.
3. Route optional diagnostics through the existing runtime diagnostic gate or
   compile them out under `RELEASE`.
4. Preserve fatal errors and bounded boot-stage messages needed for recovery.
5. Build debug and `RELEASE=1` m1n1 from clean source.
6. Verify the debug build retains diagnostics and the release build cannot
   execute the periodic formatting paths.
7. Commit and push the release-gating change.

## Task 4: Pin and Verify Mu

**Files:**
- Modify: root `mu` submodule pointer if required
- Inspect: Mu commits after the prior public release line
- Test: existing Mu layout/ACPI generation checks

1. Put Mu on a named branch containing `9dccb0133f`.
2. Compare alternative Mu branch changes file-by-file with the validated
   firmware contract.
3. Retain only changes required for J313 eight-core boot, 2560x1600 GOP,
   generated guest layout, and Windows autoboot.
4. Build Mu and run layout/ACPI tests.
5. Record the firmware hash; commit and push any necessary Mu delta.

## Task 5: Replace the Artifact Layout and Manifest

**Files:**
- Modify: `scripts/build-standalone.sh`
- Modify: `scripts/build-development.sh`
- Modify/create: manifest helper under `tools/`
- Modify: `.gitignore`
- Test: `tests/test_build_standalone.py`
- Test: manifest/profile tests under `tests/`

1. Add tests for exactly two output directories:
   `dist/j313/release` and `dist/j313/debug`.
2. Add tests requiring root/m1n1/Mu commits, clean state, compiler, profile,
   display/memory contract, sizes, and hashes in each manifest.
3. Make builds fail when a submodule is dirty or detached from the expected
   named revision.
4. Build into a temporary sibling and atomically replace the selected profile.
5. Ensure rebuilding a profile cannot leave stale files.
6. Keep generated binaries out of Git; keep manifest schema and verification
   logic in Git.

## Task 6: Enforce the Contract at Launch and Installation

**Files:**
- Modify: `scripts/run-windows.sh`
- Modify: `scripts/run-assisted.sh`
- Modify: `scripts/install-esp.sh`
- Modify/create: contract validation helper under `tools/`
- Test: `tests/test_public_scripts.py`
- Test: `tests/test_install_esp.py`

1. Add failing tests for missing, stale, hash-mismatched, wrong-profile, and
   wrong-framebuffer artifacts.
2. Make release the explicit normal-use default and debug an explicit opt-in.
3. Validate all artifacts before chainload, Mu launch, or ESP write.
4. Print the selected profile and component commit IDs once before launch.
5. Refuse legacy top-level `dist/j313/*.bin` inputs.
6. Run focused and full root tests.

## Task 7: Repair and Integrate the Windows Reinstaller

**Files:**
- Add/modify: `scripts/reinstall-windows.cmd`
- Modify: `tests/test_reinstall_windows.py`
- Modify: `documentation/INSTALL.md`
- Modify: `documentation/reference/windows-install-commands.txt`
- Modify: `.gitattributes`

1. Import the guarded reinstall tests and script from the read-only source.
2. Add a failing test proving the script does not require `findstr.exe`.
3. Replace `findstr` parsing with commands available in the tested minimal
   WinPE, while preserving disk/volume identity checks before deletion.
4. Retain the verified manual path: partition, DISM image application, BCDBoot,
   and `BOOTAA64.EFI` fallback creation.
5. Test CRLF encoding and all destructive-action guards.
6. Commit the installer independently.

## Task 8: Rewrite Public Operational Documentation

**Files:**
- Modify: `README.md`
- Modify: `documentation/BUILD.md`
- Modify: `documentation/RUN.md`
- Modify: `documentation/DEBUGGING.md`
- Modify: `documentation/INSTALL.md`
- Modify: `documentation/LIMITATIONS.md`
- Modify: `documentation/PLATFORM_STABILITY.md`
- Add: concise artifact/provenance history under `documentation/history/`

1. Remove commands referring to arbitrary copied binaries or old worktrees.
2. Document release, debug, assisted, and standalone workflows using only the
   canonical profile paths.
3. Explain ownership of display, NVMe, interrupts, timers, USB, and recovery.
4. Record useful historical findings and hashes without retaining binaries.
5. Document the exact ESP backup, install, restore, and recovery procedure.
6. Run documentation tests and scan for private paths, stale profile names,
   Russian comments, and assistant attribution.

## Task 9: Clean Build and Host Verification

1. Confirm root and both submodules are clean and on named commits.
2. Run all root tests.
3. Run all m1n1 host tests and format checks applicable to changed files.
4. Build debug and release profiles from scratch.
5. Validate manifests and SHA-256 files.
6. Confirm release output excludes hot-path diagnostic behavior.
7. Save a concise verification report in Git.

## Task 10: Hardware Verification Gates

1. Preserve the currently installed ESP image as a recoverable backup.
2. Assisted debug boot: require Windows login/desktop, physical display, and
   expected USB logs.
3. Assisted release boot: require Windows login/desktop with no web display,
   telemetry, vUART capture, or periodic diagnostics.
4. Apply a short bounded storage/CPU workload and confirm the NVMe
   backpressure path does not introduce the former long stalls.
5. Install the release standalone image on the ESP.
6. Cold boot without the helper Mac and require Windows login/desktop.
7. Record exact artifact hashes and observed result.

Hardware failure stops cleanup. Diagnose from the last passing gate and retain
the recovery image.

## Task 11: Delete Legacy State

**Destructive; execute only after Task 10 passes.**

1. Verify all useful source commits exist on public remotes.
2. Verify the root repository points to those pushed submodule commits.
3. Delete `/Users/pavel/public_windows/.local`.
4. Delete legacy top-level experimental binaries from `dist/j313`, leaving only
   current `release` and `debug` directories.
5. Remove old private Git worktrees using Git's worktree management.
6. Delete `/Users/pavel/windows` completely.
7. Re-clone or run a clean-checkout smoke test from the public repository to
   prove no hidden dependency on deleted files remains.

## Task 12: Publish

1. Review every commit for accidental binaries, credentials, private hostnames,
   assistant attribution, session URLs, or `Co-Authored-By` trailers.
2. Push m1n1 and Mu branches first.
3. Push the root consolidation branch.
4. Open or update the public integration PR, or merge only with explicit user
   authorization.
5. Tag the release only after the standalone hardware gate passes, then push the
   tag.

