# Production and Debug Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish the validated standalone exception/launch fixes together with exact production, USB-monitor, assisted, KD, install, rollback, and repository-synchronization instructions.

**Architecture:** The public root repository is the canonical operator-facing project. Its m1n1 and Mu submodules contain the implementation and generated guest-layout contract; the private root keeps historical experiments but advances to the same reachable submodule commits. Production and monitor images are separately packed immutable artifacts, never an in-place flag edit.

**Tech Stack:** freestanding AArch64 C in m1n1, Project Mu DSC/ACPI firmware configuration, Python 3 host tools and unit tests, POSIX shell launch/install wrappers, Git submodules, macOS USB ACM and ESP tooling.

## Global Constraints

- The supported target is J313/T8103 MacBook Air M1.
- Normal autonomous boot uses `display=physical`, `debug=off`, and no host dependency.
- Standalone diagnosis uses `display=physical`, `debug=monitor`; host attachment never gates guest entry.
- Never write the Windows ESP. Only the Asahi-created m1n1 ESP confirmed by `inspect` may receive `boot.bin`.
- Preserve the original Asahi `boot.bin` and both validated project artifacts before replacement.
- Never stage runtime logs, PID files, framebuffer captures, images, credentials, or temporary baselines.
- Commit messages contain no `Co-Authored-By`, Codex/Claude attribution, session URL, or assistant trailer.

---

### Task 1: Finalize the m1n1 standalone launch implementation

**Files:**
- Modify: `m1n1_windows/Makefile`
- Modify: `m1n1_windows/src/hv_autonomous_boot_runtime.c`
- Modify: `m1n1_windows/src/hv_autonomous_runtime.c`
- Modify: `m1n1_windows/src/hv_exc.c`
- Modify: `m1n1_windows/src/hv_launch_j313.c`
- Modify: `m1n1_windows/src/hv_launch_j313.h`
- Modify: `m1n1_windows/src/hv_launch_preflight.c`
- Modify: `m1n1_windows/src/hv_launch_preflight.h`
- Create: `m1n1_windows/src/hv_assisted_layout.c`
- Create: `m1n1_windows/src/hv_assisted_layout.h`
- Create: `m1n1_windows/src/hv_autonomous_transport.h`
- Create: `m1n1_windows/src/hv_exception_lower.h`
- Create: `m1n1_windows/src/hv_launch_golden_j313.c`
- Create: `m1n1_windows/src/hv_launch_golden_j313.h`
- Modify/Create: matching files under `m1n1_windows/tests/`

**Interfaces:**
- Consumes: the assisted Python launch contract, guest SPSR/VBAR state, generated J313 layout, and packed standalone manifest.
- Produces: fail-closed J313 preflight checkpoints, exact assisted/standalone launch equivalence checks, reconnect-safe monitor transport, and native lowering of guest BRKs other than the `0x4242` proxy ABI.

- [ ] **Step 1: Verify the complete host-test registration**

Run:

```sh
rg -n 'hv_assisted_layout_test|hv_autonomous_transport_test|hv_exception_lower_test|hv_launch_golden_j313_test' \
  m1n1_windows/tests/run_host_tests.sh
```

Expected: each test is present in both the test list and its source/definition dispatch.

- [ ] **Step 2: Run all m1n1 host tests**

Run:

```sh
m1n1_windows/tests/run_host_tests.sh
```

Expected: every named host test exits successfully, including `hv_exception_lower_test`.

- [ ] **Step 3: Check source hygiene**

Run:

```sh
find m1n1_windows -name .DS_Store -prune -o -type f -print0 | \
  xargs -0 rg -n 'Co-Authored-By|Claude-Session|Codex' || true
git -C m1n1_windows diff --check
```

Expected: no attribution trailers in changed source and no whitespace errors. `.DS_Store` remains untracked.

- [ ] **Step 4: Commit and publish the m1n1 implementation**

Stage only the files listed by `git -C m1n1_windows status --short`, excluding `.DS_Store`, then run:

```sh
git -C m1n1_windows commit -m 'fix: match assisted Windows exception and launch semantics'
git -C m1n1_windows push origin HEAD:main
```

Expected: the resulting commit is reachable from `paulsmir/m1n1_windows` `main`.

### Task 2: Publish the matching Mu guest-layout contract

**Files:**
- Modify: `mu/Platform/MacBookAirMid2020Pkg/J313GuestLayout.dsc.inc`

**Interfaces:**
- Consumes: `config/j313-guest-layout.json` and its generated m1n1 header.
- Produces: the same `PcdBootArgsPointer` in Mu, assisted launch, and autonomous Stage1.

- [ ] **Step 1: Verify the generated layout values agree**

Run:

```sh
python3 tools/generate_guest_layout.py --check
rg -n '8533e8000' config/j313-guest-layout.json \
  m1n1_windows/src/hv_autonomous_layout.generated.h \
  mu/Platform/MacBookAirMid2020Pkg/J313GuestLayout.dsc.inc
```

Expected: all three consumers use `0x8533e8000` for boot arguments.

- [ ] **Step 2: Exclude nested dependency dirt**

Run:

```sh
git -C mu status --short
git -C mu/Common/TIANO status --short
```

Expected: only `J313GuestLayout.dsc.inc` is staged in Mu; nested third-party changes remain untouched.

- [ ] **Step 3: Commit and publish Mu**

Run:

```sh
git -C mu add Platform/MacBookAirMid2020Pkg/J313GuestLayout.dsc.inc
git -C mu commit -m 'fix: align J313 boot arguments with standalone guest RAM'
git -C mu push origin HEAD:main
```

Expected: the commit is reachable from `paulsmir/apple_silicon_platforms_mu` `main`.

### Task 3: Document the complete production and driver-debug workflows

**Files:**
- Modify: `documentation/BUILD.md`
- Modify: `documentation/RUN.md`
- Modify: `documentation/DEBUGGING.md`
- Modify: `documentation/CONFIGURATION.md`
- Modify: `README.md`
- Test: `tests/test_public_documentation.py`

**Interfaces:**
- Consumes: `scripts/build-standalone.sh`, `scripts/install-esp.sh`, `scripts/log-standalone.sh`, `scripts/run-windows.sh`, and `tools/kd/`.
- Produces: copy-paste commands for building, naming, hashing, installing, logging, switching profiles, assisted experiments, KD work, and recovery.

- [ ] **Step 1: Add failing documentation assertions**

Extend `PublicDocumentationTests.test_standalone_monitor_workflow_is_explicit_and_abi_safe`
with these exact assertions:

```python
for token in (
    "boot-physical-monitor.bin",
    "boot-physical-production.bin",
    "attach after Windows has started",
    "verbose synchronous USB logging",
    "USB backpressure",
    "production profile",
    "tools/kd/kd_liveness.py",
    "sudo scripts/install-esp.sh restore --disk",
):
    self.assertIn(token, text)
self.assertIn("does not prove that Windows crashed", text)
```

- [ ] **Step 2: Run the documentation tests and confirm the new assertions fail**

Run:

```sh
proxyenv/bin/python -m unittest tests.test_public_documentation -v
```

Expected: failures identify the missing new workflow text.

- [ ] **Step 3: Write exact English operator instructions**

Document these commands verbatim, with machine roles and expected output explained:

```sh
scripts/build-standalone.sh --display physical --debug monitor
cp dist/j313/boot.bin .local/validated-artifacts/boot-physical-monitor.bin
shasum -a 256 .local/validated-artifacts/boot-physical-monitor.bin
scripts/log-standalone.sh --output standalone-monitor-logs

scripts/build-standalone.sh --display physical --debug off
cp dist/j313/boot.bin .local/validated-artifacts/boot-physical-production.bin
shasum -a 256 .local/validated-artifacts/boot-physical-production.bin

sudo scripts/install-esp.sh inspect --disk disk0s4
sudo scripts/install-esp.sh install --disk disk0s4 \
  --image .local/validated-artifacts/boot-physical-production.bin
sudo scripts/install-esp.sh restore --disk disk0s4
```

Explain that verbose monitor output can add latency if the host is not draining USB; only the production profile measures normal guest behavior.

- [ ] **Step 4: Run documentation and public-script tests**

Run:

```sh
proxyenv/bin/python -m unittest tests.test_public_documentation tests.test_public_scripts -v
```

Expected: all tests pass.

### Task 4: Finish the reconnect-safe standalone monitor

**Files:**
- Modify: `tools/standalone_monitor.py`
- Modify: `tests/test_standalone_monitor.py`

**Interfaces:**
- Consumes: macOS USB VID/PID, serial number, USB location, and two ACM endpoints.
- Produces: one capture directory per USB generation even when pyserial returns empty reads instead of raising after disconnect.

- [ ] **Step 1: Run the focused regression test**

Run:

```sh
proxyenv/bin/python -m unittest tests.test_standalone_monitor -v
```

Expected: pair-presence and reconnect tests pass.

- [ ] **Step 2: Verify a passive late attach**

With a monitor image already running, execute:

```sh
scripts/log-standalone.sh \
  --console /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --output /tmp/windows-on-m1-monitor
```

Expected: current console records are captured without proxy commands or a guest reset; Ctrl-C stops only the host recorder.

### Task 5: Build and validate immutable monitor and production artifacts

**Files:**
- Generated, not committed: `dist/j313/*`
- Preserved locally, not committed: `.local/validated-artifacts/*.bin`

**Interfaces:**
- Consumes: committed root, m1n1, Mu, layout, and profile sources.
- Produces: separate `debug=monitor` and `debug=off` images with recorded hashes and parseable nested manifests.

- [ ] **Step 1: Preserve the already hardware-observed monitor artifact**

Run:

```sh
mkdir -p .local/validated-artifacts
cp .local/validated-artifacts/boot-physical-monitor-18187a708220.bin \
  .local/validated-artifacts/boot-physical-monitor.bin
shasum -a 256 .local/validated-artifacts/boot-physical-monitor.bin
```

Expected hash for the current observed monitor build: `18187a7082204fa46704f2d6769567edb611e7a0a6efcf282759e9322d7752f4`.

- [ ] **Step 2: Build the production artifact**

Run:

```sh
scripts/build-standalone.sh --display physical --debug off
cp dist/j313/boot.bin .local/validated-artifacts/boot-physical-production.bin
shasum -a 256 .local/validated-artifacts/boot-physical-production.bin
python3 -c 'from standalone_image import parse_image; print(parse_image(open("dist/j313/boot.bin", "rb").read()))'
```

Expected current production hash: `1843a39f94a70ade119dea5571696ced7af9df5b5e5121f22fb4921c37f30568`; outer and inner flags are `0x1`.

- [ ] **Step 3: Run the complete root test suite**

Run:

```sh
proxyenv/bin/python -m unittest discover -s tests -v
git diff --check
```

Expected: all applicable tests pass and Mu-artifact tests either pass or explicitly skip only when their documented input is absent.

### Task 6: Perform the final no-host production smoke test

**Files:**
- Target-only: `<Asahi ESP>/m1n1/boot.bin`

**Interfaces:**
- Consumes: the preserved production artifact and Air-local installer.
- Produces: a cold autonomous Windows boot with the physical panel and no diagnostic USB dependency.

- [ ] **Step 1: Transfer while the Air runs macOS**

Run on the development host:

```sh
scp -i "$HOME/.ssh/air" \
  .local/validated-artifacts/boot-physical-production.bin \
  pavel@192.168.1.35:~/boot-physical-production.bin
```

- [ ] **Step 2: Validate and install locally on the Air**

Run on the Air:

```sh
cd ~/windows-on-m1
sudo ./scripts/install-esp.sh inspect --disk disk0s4
sudo ./scripts/install-esp.sh install \
  --disk disk0s4 \
  --image ~/boot-physical-production.bin
```

Expected: image parsing reports `display=physical`, `debug=off`; installation preserves the original backup and writes only `disk0s4`.

- [ ] **Step 3: Cold-boot without the debug cable**

Expected: Windows progresses beyond the static logo to the sign-in or desktop screen. No host endpoint is required. If it fails, restore from macOS with `sudo ./scripts/install-esp.sh restore --disk disk0s4` and do not publish a production-success claim.

### Task 7: Commit and push the canonical public root

**Files:**
- Modify: submodule pointers `m1n1_windows`, `mu`
- Modify: files from Tasks 3 and 4
- Modify: `config/j313-guest-layout.json`

**Interfaces:**
- Consumes: reachable fork commits and completed verification evidence.
- Produces: a fast-forwardable public main containing code pointers, scripts, tests, and documentation.

- [ ] **Step 1: Audit the exact staged scope**

Run:

```sh
git status --short
git diff --check
git diff --cached --name-status
git diff --cached | rg -n 'Co-Authored-By|Claude-Session|Codex' && exit 1 || true
```

Expected: only intended source, tests, documentation, generated layout, and submodule pointers are staged.

- [ ] **Step 2: Commit the public release**

Run:

```sh
git commit -m 'fix: ship validated standalone Windows launch'
```

- [ ] **Step 3: Push the feature history and fast-forward public main**

Run:

```sh
git push -u origin codex/restore-assisted-baseline
git merge-base --is-ancestor origin/main HEAD
git push origin HEAD:main
```

Expected: both the diagnostic branch and public `main` resolve to the release commit without force-push.

### Task 8: Synchronize the private laboratory repository

**Files:**
- Modify: private root submodule pointers `m1n1_windows`, `mu`
- Modify: private `.gitignore`
- Modify: private `README.md`

**Interfaces:**
- Consumes: the two reachable fork SHAs and canonical public documentation URL.
- Produces: a private-history checkpoint that references the published implementation without importing generated logs or unrelated public history.

- [ ] **Step 1: Ignore current laboratory artifacts without deleting them**

Add ignore entries for `.tmp-m1n1-*`, `*.pid`, `logview.out`, `fb-info.json`, and `*-fb-info.json`. Verify the files remain on disk and disappear from `git status` only when they are untracked; tracked runtime files are left unstaged.

- [ ] **Step 2: Record the canonical release location**

Replace the obsolete status at the start of the private README with a short historical notice and link to:

```text
https://github.com/paulsmir/windows-on-m1
```

State that current English installation, build, run, standalone-monitor, assisted-debug, and KD instructions live under the public repository's `documentation/` directory.

- [ ] **Step 3: Advance only the two private root gitlinks**

Fetch the published fork commits, check them out in the private submodule directories without deleting untracked lab files, and stage only `.gitignore`, `README.md`, `m1n1_windows`, and `mu`.

- [ ] **Step 4: Audit, commit, and push private main**

Run:

```sh
git diff --cached --name-status
git diff --cached | rg -n 'Co-Authored-By|Claude-Session|Codex' && exit 1 || true
git commit -m 'chore: reference the validated standalone release'
git push origin main
```

Expected: private runtime artifacts remain local and unstaged; `origin/main` contains the historical commits already ahead locally plus this synchronization commit.

### Task 9: Verify published reachability and handoff

**Files:** None.

**Interfaces:**
- Consumes: four pushed repositories/branches.
- Produces: recorded SHAs and operator commands for production, monitor, assisted, and rollback use.

- [ ] **Step 1: Verify every published ref**

Run `git ls-remote` for public root, private root, m1n1 fork, and Mu fork. Confirm both root gitlinks appear in the corresponding fork output.

- [ ] **Step 2: Report exact handoff commands**

Provide the final production boot command (power on with no cable), monitor build/log/install commands, assisted launch command, KD environment command, and ESP restore command together with the four commit SHAs and two artifact hashes.
