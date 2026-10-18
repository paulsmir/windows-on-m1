# Windows Mesa/AGX Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish a reproducible Mesa/WGL control build and a portable,
fail-closed Windows AGX command envelope before any new driver capability or
hardware experiment.

**Architecture:** Pin upstream Mesa as an external MIT source input instead of
copying it into the driver tree. Add one shared C ABI that represents a
versioned command plus WDDM-allocation-relative GPU address references. The
portable validator proves bounds and ownership independently of WDK types; the
future UMD and KMD use the same code.

**Tech Stack:** C11 portable shared code, Python `unittest`, clang sanitizers,
PowerShell, Meson/Ninja, MSVC/Windows SDK+WDK 10.0.26100, Mesa WGL/softpipe
control build.

**Spec:** `docs/superpowers/specs/2026-09-08-windows-mesa-agx-design.md`

## Global Constraints

- Do not change KMD capabilities, INF graphics names, AGX firmware, queues,
  DCP, Mu, m1n1, retained-root, or context-63 in this bootstrap.
- Software Mesa is an uninstalled toolchain/WGL control only and is never an
  AGX or acceleration PASS.
- Mesa commit is exactly `9aa1215f878b504f66159dd2ead4c7973142126e`.
- Pinned WDK/SDK version is `10.0.26100.0`; the copied `d3d10umddi.h` SHA-256
  is `61899403d94840fab282dbb7da6faf234e2954bbdb47e3455f0f0572eb4e723a`.
- Every wire integer has a fixed-width project type. Reserved fields are zero.
- The wire format contains no CPU or physical pointer.
- One command references only allocations owned by the supplied device view.
- All source changes use RED -> GREEN and are committed independently.

---

### Task 1: Pin and verify external source inputs

**Files:**
- Create: `drivers/apple-agx/mesa/mesa-source.lock.json`
- Create: `tools/verify_apple_agx_mesa_source.py`
- Create: `tests/test_apple_agx_mesa_source.py`

**Interfaces:**
- Consumes: a Mesa checkout path passed with `--source`.
- Produces: exit zero and one JSON object containing `commit`, `license_sha256`,
  and `required_paths`; nonzero for any mismatch.

- [ ] **Step 1: Write the failing manifest/checkout test**

```python
def test_pinned_mesa_checkout_and_mit_sources(self):
    manifest = ROOT / "drivers/apple-agx/mesa/mesa-source.lock.json"
    tool = ROOT / "tools/verify_apple_agx_mesa_source.py"
    checkout = ROOT / ".local/reference/mesa"
    run = subprocess.run(
        [sys.executable, str(tool), "--lock", str(manifest),
         "--source", str(checkout)],
        cwd=ROOT, text=True, capture_output=True,
    )
    self.assertEqual(run.returncode, 0, run.stderr)
    result = json.loads(run.stdout)
    self.assertEqual(result["commit"],
                     "9aa1215f878b504f66159dd2ead4c7973142126e")
    self.assertTrue(result["license_sha256"])
```

The lock must require `licenses/MIT`, `meson.build`,
`src/gallium/frontends/wgl`, `src/gallium/targets/wgl`,
`src/gallium/targets/libgl-gdi`, `src/gallium/drivers/asahi`, and
`src/asahi/compiler`.

- [ ] **Step 2: Run the test and verify RED**

Run: `python3 -m unittest tests.test_apple_agx_mesa_source -v`

Expected: FAIL because the lock and verifier do not exist.

- [ ] **Step 3: Implement the exact lock verifier**

The verifier must use `git -C <source> rev-parse HEAD`, hash `licenses/MIT`, reject
dirty tracked files, check every required path, and emit only:

```json
{"commit":"...","license_sha256":"...","required_paths":["..."]}
```

The JSON lock contains the repository URL, exact commit, expected MIT license
SHA-256 calculated from the pinned checkout, and the sorted required paths.

- [ ] **Step 4: Run focused and ledger regressions**

Run:

```bash
python3 -m unittest tests.test_apple_agx_mesa_source -v
python3 -m unittest tests.test_change_ledger -v
```

Expected: GREEN.

- [ ] **Step 5: Commit**

```bash
git add drivers/apple-agx/mesa/mesa-source.lock.json \
  tools/verify_apple_agx_mesa_source.py tests/test_apple_agx_mesa_source.py
git commit -m "Pin Mesa source for Windows AGX"
```

Append the resulting 40-character implementation hash to `CHANGES.csv` in a
separate bookkeeping commit.

### Task 2: Add the portable command-envelope ABI

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_win32_abi.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_win32_abi.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_win32_abi_test.c`
- Create: `tests/test_apple_agx_win32_abi.py`

**Interfaces:**
- Consumes: immutable command storage, storage byte count, allocation views,
  and allocation count.
- Produces: `APPLE_AGX_WIN32_COMMAND_VIEW` only when the complete record is
  structurally and ownership valid.

The public API is:

```c
typedef struct _APPLE_AGX_WIN32_ALLOCATION_VIEW {
  unsigned long long Owner;
  unsigned long long GpuVa;
  unsigned long long Bytes;
  unsigned int Active;
  unsigned int Reserved;
} APPLE_AGX_WIN32_ALLOCATION_VIEW;

typedef struct _APPLE_AGX_WIN32_COMMAND_VIEW {
  const struct _APPLE_AGX_WIN32_COMMAND_HEADER *Header;
  const unsigned char *Payload;
  const struct _APPLE_AGX_WIN32_ADDRESS_REFERENCE *References;
  const struct _APPLE_AGX_WIN32_ATTACHMENT *Attachments;
} APPLE_AGX_WIN32_COMMAND_VIEW;

int AppleAgxWin32CommandOpen(
    const void *Storage, unsigned int StorageBytes,
    const APPLE_AGX_WIN32_ALLOCATION_VIEW *Allocations,
    unsigned int AllocationCount, unsigned long long ExpectedOwner,
    APPLE_AGX_WIN32_COMMAND_VIEW *View);
```

The v1 layout is header, opaque payload, address references, attachments. The
header carries magic `0x57475841`, version 1, total bytes, opcode, generation,
sequence, payload bytes, reference count, attachment count, and reserved zero.
Counts are bounded at 64 references and 16 attachments. Each address reference
contains payload field offset, allocation-list index, allocation byte offset,
required byte length, access (`READ=1`, `WRITE=2`), and reserved zero. Each
attachment names a reference and has reserved-zero flags.

- [ ] **Step 1: Write the failing executable C tests**

Tests construct one exact record and require successful open. Independent
mutations must reject bad magic/version/opcode/total size, unaligned sections,
integer overflow, count overflow, nonzero reserved fields, inactive allocation,
wrong owner, allocation-index overflow, zero range, allocation-range overflow,
payload field overflow, a payload GPU address unequal to
`Allocation.GpuVa + AllocationOffset`, unknown access bits, duplicate write
ranges, invalid attachment reference, and a write attachment backed only by a
read reference.

The Python wrapper compiles the production C and C test with:

```python
command = [
    os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
    "-Werror", "-fsanitize=address,undefined",
    "-I", str(SHARED / "include"),
    str(SHARED / "tests/apple_agx_win32_abi_test.c"),
    str(SHARED / "src/apple_agx_win32_abi.c"),
    "-o", str(binary),
]
```

- [ ] **Step 2: Run the test and verify RED**

Run: `python3 -m unittest tests.test_apple_agx_win32_abi -v`

Expected: FAIL because the ABI does not exist.

- [ ] **Step 3: Implement minimal validation**

Use explicit checked addition/multiplication helpers. Do not cast the input to a
section pointer until the complete section range has been proven inside
`Header.TotalBytes == StorageBytes`. Read the payload GPU address with a byte
copy helper so untrusted alignment is irrelevant. Require all allocations to
match `ExpectedOwner`, be active, and have zero reserved fields. Reject any two
write references that overlap in the same allocation.

- [ ] **Step 4: Run focused and shared regressions**

Run:

```bash
python3 -m unittest tests.test_apple_agx_win32_abi -v
python3 -m unittest tests.test_apple_agx_gpuvm_codec \
  tests.test_apple_agx_render_job tests.test_apple_agx_render_template -v
```

Expected: GREEN under ASan/UBSan.

- [ ] **Step 5: Commit**

```bash
git add drivers/apple-agx/shared/include/apple_agx_win32_abi.h \
  drivers/apple-agx/shared/src/apple_agx_win32_abi.c \
  drivers/apple-agx/shared/tests/apple_agx_win32_abi_test.c \
  tests/test_apple_agx_win32_abi.py
git commit -m "Add validated Windows AGX command envelope"
```

Append the implementation hash to `CHANGES.csv` separately.

### Task 3: Build uninstalled Mesa WGL software controls on FRYZZING

**Files:**
- Create: `drivers/apple-agx/mesa/scripts/build-wgl-control.ps1`
- Create: `drivers/apple-agx/mesa/scripts/verify-wgl-control.ps1`
- Create: `tests/test_apple_agx_mesa_wgl_scripts.py`
- Artifact only: `.local/experiments/EXP648-mesa-bootstrap/`

**Interfaces:**
- Consumes: the verified pinned Mesa checkout and the pinned VS/SDK environment.
- Produces: ARM64 and Win32 `opengl32.dll`/`libgallium_wgl.dll` software-control
  bundles, manifests, dependency lists, export lists, and SHA-256 values.

- [ ] **Step 1: Write failing script-contract tests**

Require the scripts to reject an unverified commit, use Meson/Ninja only from
an explicit tools directory, configure `-Dgallium-drivers=softpipe`, disable
unneeded Vulkan/video/LLVM paths, build both `arm64` and `x86`, inspect PE
machine type and required WGL exports, and label every manifest
`renderer_control=software` and `installable=false`. The scripts must contain no
INF, `pnputil`, registry, SSH-to-Air, or package-install command.

- [ ] **Step 2: Run the tests and verify RED**

Run: `python3 -m unittest tests.test_apple_agx_mesa_wgl_scripts -v`

Expected: FAIL because the scripts do not exist.

- [ ] **Step 3: Implement scripts and prepare builder tools**

Install Meson/Ninja into an experiment-local Python virtual environment on
FRYZZING. Transfer an archive produced from the verified checkout, not a dirty
working tree. Import the VS 2022 ARM64/x86 environments explicitly. The build
script records `cl`, `link`, SDK, Meson, Ninja, Python and source identities.

- [ ] **Step 4: Build and verify both controls**

Run the pinned script once for ARM64 and once for x86. Verify PE machine,
Authenticode state (unsigned control is allowed but must be recorded), exports,
dependencies, and exact hashes. Do not transfer or install these controls on
the Air yet.

- [ ] **Step 5: Commit only reproducible workflow files**

```bash
git add drivers/apple-agx/mesa/scripts/build-wgl-control.ps1 \
  drivers/apple-agx/mesa/scripts/verify-wgl-control.ps1 \
  tests/test_apple_agx_mesa_wgl_scripts.py
git commit -m "Add reproducible Mesa WGL control build"
```

Record artifacts and the implementation commit in `CHANGES.csv`; status remains
`implemented`, with no hardware result.

### Task 4: Freeze the next implementation boundary

**Files:**
- Modify: `investigation/GPU_CURRENT_STATE.md`
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/CHANGES.csv`
- Create: `docs/superpowers/plans/2026-09-08-windows-agx-fake-transport.md`

**Interfaces:**
- Consumes: verified source lock, ABI validator, and both WGL controls.
- Produces: the next plan for a fake Mesa winsys/KMD transport pair; no hardware
  package.

- [ ] **Step 1: Record exact software evidence**

Record source/tool hashes, test counts, build logs, PE architecture, exports,
and software-renderer labels. Do not set OpenGL, D3D, submission, or Present
hardware readiness.

- [ ] **Step 2: Write the fake-transport plan**

The next plan must cover query, BO create/destroy, VA assignment, bind/unbind,
single-queue submit, exact fence completion, generation reset, and present
ownership using the v1 envelope. It must use a fake allocation inventory and
real Mesa winsys calls but no KMD/INF capability change.

- [ ] **Step 3: Verify ledgers and commit**

Run:

```bash
python3 -m unittest tests.test_change_ledger -v
git diff --check
```

Commit compact state and the next plan. Then execute that plan inline under the
standing main-process-only instruction.
