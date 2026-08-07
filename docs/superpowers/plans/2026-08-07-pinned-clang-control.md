# Pinned-Clang Standalone Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Determine whether the current standalone CPU1 reset follows m1n1 compiler code generation or the first-stage iBoot execution context.

**Architecture:** Rebuild only the current public m1n1 sources with the exact Homebrew Clang 22.1.8 toolchain that produced the successful assisted `hv.o`, then pack the unchanged current Mu firmware and physical/monitor manifest into a separate immutable artifact. Install it reversibly and capture a complete cold boot with the existing passive recorder; no guest-startup source or image format changes are allowed in this control.

**Tech Stack:** m1n1 freestanding AArch64 C/Rust build, Homebrew LLVM 22.1.8, existing Python standalone packer/parser, SHA-256, passive USB ACM monitor, Asahi ESP installer.

## Global Constraints

- Use `/Users/pavel/public_windows` only; `/Users/pavel/windows` is reference evidence and must remain unchanged.
- The control must use Homebrew Clang 22.1.8 from `/opt/homebrew/opt/llvm/bin` for every rebuilt m1n1 C object.
- Reuse the already built `dist/j313/J313_EFI.fd`; do not rebuild or alter Mu for this experiment.
- Keep launch flags at `display=physical debug=monitor`, which encode as `0x11`.
- Write the artifact and logs under ignored `.local/`; do not replace `dist/j313/boot.bin`.
- Install through `scripts/install-esp.sh` so the existing scoped backup and restore path remain valid.
- A connected monitor may record output but must never take over the automatic boot.
- Do not claim the compiler is causal unless the Clang image passes the exact CPU1 boundary that the GCC image fails.
- Do not add co-author or session trailers to commits.

---

### Task 1: Build an immutable Clang control artifact

**Files:**
- Read: `m1n1_windows/Makefile`
- Read: `tools/pack_boot.py`
- Read: `config/j313-guest-layout.json`
- Create (ignored artifact): `.local/clang22-control/m1n1.bin`
- Create (ignored artifact): `.local/clang22-control/m1n1.elf`
- Create (ignored artifact): `.local/clang22-control/boot.bin`
- Create (ignored evidence): `.local/clang22-control/BUILD.txt`
- Test: `m1n1_windows/tests/run_host_tests.sh`
- Test: `standalone_image.parse_image`

**Interfaces:**
- Consumes: current submodule commit `c6dc965a4d3312e4aa437835236e6b0e98c32c16`, `/opt/homebrew/opt/llvm/bin/clang`, and `dist/j313/J313_EFI.fd`.
- Produces: a physical/monitor `boot.bin`, its SHA-256, compiler identity, source commit, manifest fields, and the exact ELF used for later symbolization.

- [ ] **Step 1: Prove the intended compiler is present**

Run:

```bash
/opt/homebrew/opt/llvm/bin/clang --version
git -C m1n1_windows rev-parse HEAD
shasum -a 256 dist/j313/J313_EFI.fd
```

Expected: the first line reports `Homebrew clang version 22.1.8`, the submodule is `c6dc965a4d3312e4aa437835236e6b0e98c32c16`, and the firmware hash is recorded before the build.

- [ ] **Step 2: Remove mixed-compiler m1n1 objects and rebuild with Clang**

Run from `/Users/pavel/public_windows/m1n1_windows`:

```bash
make clean
make -j8 USE_CLANG=1 TOOLCHAIN=/opt/homebrew/opt/llvm/bin/ LLDDIR=/opt/homebrew/opt/llvm/bin/
```

Expected: the build succeeds and each visible C compile command, when rerun with `V=1` for audit if necessary, begins with `/opt/homebrew/opt/llvm/bin/clang`; no GCC-built stale object survives `make clean`.

- [ ] **Step 3: Run the native m1n1 host suite**

Run:

```bash
./tests/run_host_tests.sh
```

Expected: all 19 named host binaries print their `: ok` result and the script exits zero.

- [ ] **Step 4: Preserve the exact m1n1 binary and symbols**

Run from `/Users/pavel/public_windows`:

```bash
mkdir -p .local/clang22-control
cp m1n1_windows/build/m1n1.bin .local/clang22-control/m1n1.bin
cp m1n1_windows/build/m1n1.elf .local/clang22-control/m1n1.elf
```

Expected: both files exist, and `m1n1.bin` has a size divisible by `0x4000` so its end is `_payload_start`.

- [ ] **Step 5: Pack the unchanged Mu firmware with the monitor profile**

Run:

```bash
python3 tools/pack_boot.py --m1n1 .local/clang22-control/m1n1.bin --firmware dist/j313/J313_EFI.fd --layout config/j313-guest-layout.json --output .local/clang22-control/boot.bin --display physical --debug monitor
```

Expected: packing succeeds and reports `profile: display=physical debug=monitor`.

- [ ] **Step 6: Validate the complete image and record immutable provenance**

Run:

```bash
python3 -c 'from pathlib import Path; from standalone_image import parse_image; m, fw = parse_image(Path(".local/clang22-control/boot.bin").read_bytes()); print(f"flags={m.flags:#x} firmware={len(fw)} crc32={m.crc32:08x}")'
shasum -a 256 .local/clang22-control/boot.bin .local/clang22-control/m1n1.bin .local/clang22-control/m1n1.elf dist/j313/J313_EFI.fd
```

Expected: parser output starts with `flags=0x11`; all four SHA-256 values are retained in `.local/clang22-control/BUILD.txt` together with `clang --version`, root commit, submodule commit, and the packer output.

### Task 2: Capture the CPU1 hardware boundary

**Files:**
- Read: `scripts/install-esp.sh`
- Read: `scripts/log-standalone.sh`
- Create (ignored evidence): `.local/clang22-control-monitor/generation-*/console.tlog`
- Create (ignored evidence): `.local/clang22-control-monitor/generation-*/vuart.tlog`
- Modify after evidence exists: `documentation/DEVELOPMENT_HISTORY.md`

**Interfaces:**
- Consumes: `.local/clang22-control/boot.bin` and the passive monitor profile from Task 1.
- Produces: one complete cold-boot trace that classifies the failure as compiler-sensitive or startup-context-sensitive.

- [ ] **Step 1: Transfer and verify the control artifact on the Air**

Run from the development Mac:

```bash
scp -i /Users/pavel/.ssh/air .local/clang22-control/boot.bin pavel@192.168.1.35:~/boot-clang22-control.bin
ssh -i /Users/pavel/.ssh/air pavel@192.168.1.35 'shasum -a 256 ~/boot-clang22-control.bin'
```

Expected: the remote SHA-256 exactly matches Task 1.

- [ ] **Step 2: Install reversibly on the Air**

Run locally on the Air from its clone of `windows-on-m1`:

```bash
sudo ./scripts/install-esp.sh install --disk disk0s4 --image ~/boot-clang22-control.bin
```

Expected: the installer validates the manifest, preserves the existing scoped backup, atomically replaces only `<ESP>/m1n1/boot.bin`, and prints the installed hash.

- [ ] **Step 3: Start the recorder before cold boot**

Run from `/Users/pavel/public_windows` on the development Mac:

```bash
./scripts/log-standalone.sh --output .local/clang22-control-monitor
```

Expected: the recorder waits for USB ACM generation 1 and remains active across disconnect and re-enumeration.

- [ ] **Step 4: Cold-boot the Air and classify the trace**

Boot the Asahi Windows entry with no host interaction. In the captured console, search:

```bash
rg -n 'PSCI DEBUG: turning on CPU1|HV: Initializing secondary 1|HV: Entering guest secondary 1|HV: Secondary 1 consumed entry|Exception: SYNC|Unhandled exception' .local/clang22-control-monitor
```

Expected classification:

- **Compiler-sensitive:** Clang records both `HV: Entering guest secondary 1` and `HV: Secondary 1 consumed entry`, while the otherwise equivalent GCC monitor image resets before `Entering`.
- **Startup-context-sensitive:** Clang records the same EL2 exception before `Entering`; compiler code generation is eliminated as the trigger and the fresh self-chainload boundary becomes the next controlled experiment.
- **Different failure:** preserve the full generation and symbolize its PC with `.local/clang22-control/m1n1.elf`; do not map it onto either hypothesis without evidence.

- [ ] **Step 5: Record the result without changing the conclusion retroactively**

Add an English entry to `documentation/DEVELOPMENT_HISTORY.md` containing the artifact SHA, exact compiler, Mu firmware SHA, relevant trace lines, and one of the three classifications above. State explicitly that compiler sensitivity does not prove compiler correctness: undefined behavior must still be investigated before standardizing the toolchain.

- [ ] **Step 6: Run documentation tests and commit only the evidence**

Run:

```bash
python3 -m unittest tests.test_public_documentation tests.test_repository_hygiene -v
git diff --check
git add documentation/DEVELOPMENT_HISTORY.md
git commit -m "diag: classify standalone CPU1 reset"
```

Expected: tests pass, `.local/` artifacts remain untracked/ignored, and the commit contains only the development-history evidence with no co-author or session trailer.

### Task 3: Gate the self-chainload implementation

**Files:**
- Read: `docs/superpowers/specs/2026-08-07-self-chainload-shared-guest-engine-design.md`
- Create after Task 2: `docs/superpowers/plans/2026-08-07-stage0-self-chainload.md`

**Interfaces:**
- Consumes: the hardware classification and exact immutable artifact from Task 2.
- Produces: a separate TDD implementation plan for the outer bootstrap format and Stage 0 chainload boundary; the plan must not yet move assisted hardware ordering into C.

- [ ] **Step 1: Carry the classification into the Stage 0 acceptance criteria**

If compiler-sensitive, require Stage 0 and Stage 1 to use the corrected/pinned toolchain and add a generated build-identity record. If startup-context-sensitive, require a byte-identical Stage 1 m1n1 control to prove that only the fresh-image boundary changes. In both cases retain the same Mu firmware SHA and `flags=0x11` for the first Stage 0 hardware checkpoint.

- [ ] **Step 2: Write the Stage 0 plan with exact interfaces**

The next plan must define the outer Python `BootstrapManifest`, matching C `struct hv_bootstrap_manifest`, parser error enum, `chainload_image()` handoff, host-test fake chainload operation, packer CLI changes, old/new format rejection, and the monitor trace expected on both sides of the reload. It must use failing tests before implementation and end at the Stage 0 boundary, not at a claimed stable Windows desktop.

- [ ] **Step 3: Self-review and commit the next plan**

Run:

```bash
rg -n 'T(BD)|T(ODO)|implement[ ]later|appropriate[ ]error|similar[ ]to' docs/superpowers/plans/2026-08-07-stage0-self-chainload.md
git diff --check
git add docs/superpowers/plans/2026-08-07-stage0-self-chainload.md
git commit -m "docs: plan standalone self-chainload stage"
```

Expected: the placeholder scan is empty, every interface matches the approved design, and the plan commit contains no generated artifact or unrelated submodule dirt.
