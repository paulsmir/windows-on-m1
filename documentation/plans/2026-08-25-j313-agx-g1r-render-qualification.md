# J313 AGX G1R Private Render Qualification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Qualify one deterministic private 16 by 16 TA-to-3D clear across ten cold-reset-separated assisted J313 lifecycles without changing the stable Windows boot path.

**Architecture:** A pure fixture layer canonicalizes and validates one locally reproduced Mesa-to-m1n1 frame. A pure render-gate state machine validates TA, 3D, event, stamp, output, mapping, deadline, and cleanup evidence; a separate m1n1 adapter is the sole hardware boundary and reuses pinned `GPUFrame` and `GPURenderer`. Capture and replay operators enforce immutable provenance, sole proxy ownership, fresh evidence, and physical reboot boundaries.

**Tech Stack:** Python 3 standard library, `unittest`, ZIP/JSON/SHA-256, pinned m1n1 AGX renderer and UAT structures, pinned Asahi Mesa capture source, POSIX shell, existing G0/G1 provenance and recovery validation.

**Spec:** `documentation/design/2026-08-25-j313-agx-g1r-render-qualification.md`

## Global Constraints

- Work only on `feature/j313-gpu-acceleration` under `/Users/pavel/public_windows`.
- Preserve `.local/recovery/STABLE-j313-8core-native-input-v1/` byte-for-byte.
- Preserve m1n1 commit `9cd80ac652ac404e92ae279deeaec8c629d7d184` and Mu commit `8b4dc4b4e3ff8606d0af36163acf9de79b7b4737`.
- Keep assisted Windows launch, standalone launch, Mu, ACPI, firmware images, Windows, native keyboard, Precision Touchpad, and physical DCP scanout unchanged.
- G1R runs only at `Running proxy...` with sole proxy ownership and before guest entry.
- Use context ID `63`, UAT page size `0x4000`, renderer queue index `1`, one 16 by 16 RGBA8 color attachment, and a fixed `0.5` second workload deadline.
- Do not map guest RAM, the DCP scanout interval, a Windows framebuffer, NVMe buffers, or context zero into the diagnostic context.
- The workload is one complete first-submit renderer sequence: two 3D ring entries (TA barrier plus `WorkCommand3D`) and two TA ring entries (`WorkCommandInitBM` plus `WorkCommandTA`).
- Pass requires exact TA and 3D queue completion, exact matching events, exact stamp progress, and the manifest's deterministic private-output SHA-256.
- Polling is evidence only and cannot substitute for either completion event.
- Every failure blocks Windows and requires physical reboot before another AGX run.
- Every production behavior is written test-first and observed RED before implementation.
- Every implementation commit receives an exact 40-character row in `investigation/CHANGES.csv`; every ledger-only commit remains separate.
- Every hardware experiment is preregistered in `investigation/EXPERIMENTS.md` with exact commits, hashes, command, deadline, evidence directory, pass criteria, and stop rules.
- EXP-080 remains reserved for the final ten-cycle cold qualification.
- No push occurs until the hardware gate and unchanged stable Windows verification pass.
- No assistant attribution, session URL, or `Co-Authored-By` trailer is permitted.

---

## File map

- `tools/agx_frame_fixture.py`: pure ZIP canonicalization, manifest construction, and strict fixture validation.
- `tests/test_agx_frame_fixture.py`: literal synthetic capture and manifest mutation tests.
- `tools/agx_render_gate.py`: pure receipt validation, one-shot lifecycle, evidence serialization, cold aggregation, and verification CLI.
- `tests/test_agx_render_gate.py`: literal receipt mutation, lifecycle, cleanup, aggregation, and CLI tests.
- `tools/agx_m1n1_render_backend.py`: sole live AGX adapter using pinned `GPUFrame` and `GPURenderer`.
- `tests/test_agx_m1n1_render_backend.py`: complete deterministic fakes for context, queues, events, mappings, output, faults, and teardown.
- `tools/agx_capture_clear.py`: fixed capture-program provenance and two-capture normalization CLI.
- `tools/agx_clear_capture.c`: fixed 16 by 16 RGBA8 EGL/GLES clear producer;
  writes its CPU-visible readback to the operator-selected output file.
- `tests/test_agx_capture_clear.py`: reproducibility and unsafe-capture rejection tests.
- `scripts/capture-agx-clear-frame.sh`: assisted-only capture operator.
- `scripts/run-agx-render-gate.sh`: assisted one-shot and cold-cycle replay operator.
- `tests/test_run_agx_render_gate.py`: executable operator boundary and stable-launch isolation tests.
- `fixtures/agx/j313-g13-v13_5-clear-16x16/`: added only after two real cold captures match.
- `documentation/AGX_BRINGUP.md`: operator procedure and evidence interpretation after hardware qualification.
- `investigation/EXPERIMENTS.md`: preregistration and immutable result summaries.
- `investigation/CHANGES.csv`: exact implementation commit ledger.

---

### Task 1: Canonical frame fixture and strict validator

**Files:**
- Create: `tools/agx_frame_fixture.py`
- Create: `tests/test_agx_frame_fixture.py`

**Interfaces:**
- Consumes: a source `GPUFrame` ZIP, a JSON manifest, and literal expected board/source identity.
- Produces: `FixtureError`, `FrameObject`, `ValidatedFrame`, `canonicalize_zip(source: Path, destination: Path) -> str`, `build_manifest(...) -> dict`, `validate_fixture(frame_path: Path, manifest_path: Path, expected_identity: dict) -> ValidatedFrame`, and CLI commands `canonicalize`, `manifest`, and `verify`.

- [ ] **Step 1: Write failing safe-ZIP tests**

Name the break: a duplicate, traversing, unlisted, oversized, or hash-mismatched member can reach the replay backend.

Create a literal four-member synthetic frame with `cmdbuf.json`,
`objects.json`, `obj_1100010000.bin`, and `obj_1500000000.bin`. Use fixed ZIP
timestamps and literal SHA-256 expectations. Add independent tests rejecting:

```python
with self.assertRaisesRegex(FixtureError, "path traversal"):
    validate_fixture(self.frame("../escape.bin"), self.manifest, IDENTITY)

with self.assertRaisesRegex(FixtureError, "duplicate member"):
    validate_fixture(self.duplicate_frame(), self.manifest, IDENTITY)

with self.assertRaisesRegex(FixtureError, "member hash"):
    validate_fixture(self.changed_frame_byte(), self.manifest, IDENTITY)
```

Also reject an absolute path, backslash path, unlisted member, missing member,
a compression bomb or member over 16 MiB, aggregate uncompressed size over 64 MiB,
malformed JSON, and a boolean where an integer is required.

- [ ] **Step 2: Run RED**

Run:

```sh
./proxyenv/bin/python -m unittest tests.test_agx_frame_fixture.SafeZipTests -v
```

Expected: import failure for `tools.agx_frame_fixture`, proving no validator exists.

- [ ] **Step 3: Implement minimal canonical ZIP and member validation**

Use constants:

```python
FIXTURE_VERSION = 1
MAX_MEMBER_SIZE = 16 * 1024 * 1024
MAX_TOTAL_SIZE = 64 * 1024 * 1024
CANONICAL_ZIP_TIME = (1980, 1, 1, 0, 0, 0)
```

Read every `ZipInfo` before extraction, reject duplicate normalized names, and
read through the ZIP API without writing members to disk. Canonical output must
sort names, use `ZIP_DEFLATED`, clear comments/extra fields, apply the fixed
timestamp and permissions, and return the complete lowercase SHA-256.

- [ ] **Step 4: Run GREEN and canonical determinism mutation**

Run the SafeZip tests. Then locally change canonical member sorting to input
order, run the differently ordered ZIP test and observe failure; restore and
run GREEN again.

- [ ] **Step 5: Write failing manifest and address-isolation tests**

Name the break: a valid ZIP can still describe the wrong board, source,
attachment, mapping, or command-buffer pointers.

The literal valid manifest must bind:

```python
IDENTITY = {
    "board": "J313",
    "chip_generation": "G13",
    "firmware_version": "V13_5",
    "m1n1_commit": "9cd80ac652ac404e92ae279deeaec8c629d7d184",
    "adt_sha256": "c57d4c0db26125394409c3b5b518fdef553d8f4dfe2263ae9303e2276b0796a3",
}
```

Use private frame ranges `0x1100000000..0x1200000000` for pipelines and
`0x1500000000..0x1700000000` for GEM/userspace objects. Reject wrong identity,
non-40-character source commit, overlapping objects, object address or size not
aligned to `0x4000`, range overflow, object outside allowlists, attachment count
other than one, depth/stencil flags, size other than 16 by 16, output pointer
outside its object, missing `encoder_ptr` target, pre/post hash equality, and
fixture/member hash mismatch.

- [ ] **Step 6: Run RED, implement typed validation, then run GREEN**

Run:

```sh
./proxyenv/bin/python -m unittest tests.test_agx_frame_fixture -v
```

Implement frozen dataclasses with defensive immutable bytes:

```python
@dataclass(frozen=True)
class FrameObject:
    name: str
    gpu_va: int
    size: int
    map_flags: tuple[tuple[str, int], ...]
    sha256: str
    data: bytes

@dataclass(frozen=True)
class ValidatedFrame:
    fixture_sha256: str
    command_buffer: dict
    objects: tuple[FrameObject, ...]
    output_gpu_va: int
    output_size: int
    poison_sha256: str
    expected_output_sha256: str
```

Expected: all fixture tests pass with no m1n1 import.

- [ ] **Step 7: Commit implementation and ledger separately**

```sh
git add tools/agx_frame_fixture.py tests/test_agx_frame_fixture.py
git commit -m "gpu: add strict AGX frame fixture validation"
```

Append the exact implementation commit to `investigation/CHANGES.csv` with the
literal mutation set as reproduction, then commit only the ledger with
`docs: record AGX frame fixture validation`.

---

### Task 2: Reproducible two-capture normalizer

**Files:**
- Create: `tools/agx_capture_clear.py`
- Create: `tools/agx_clear_capture.c`
- Create: `tests/test_agx_capture_clear.py`

**Interfaces:**
- Consumes: two independently captured frame ZIPs, two final attachment files, capture-program bytes, exact source identity, and an empty output directory.
- Produces: `CaptureError`, `CaptureInput`, `compare_captures(first: CaptureInput, second: CaptureInput) -> dict`, `package_capture(...) -> tuple[Path, Path]`, and CLI command `package-two`.

- [ ] **Step 1: Write failing reproducibility tests**

Name the break: two captures with different command/object bytes or final output
can be normalized into one apparently trusted fixture.

Build two independently ordered ZIPs with different ZIP timestamps but
identical literal member bytes. Assert packaging produces byte-identical
canonical ZIPs and one manifest. Independently mutate command-buffer JSON,
object JSON, one object byte, one object address, one map flag, final attachment
byte, source identity, capture-program byte, and proxy identity; every mutation
must raise `CaptureError` naming that boundary.

Add a subprocess contract test for the capture program: it must reject zero or
two arguments, create exactly one 1024-byte RGBA8 output for one valid path,
and every pixel must equal literal bytes `11 22 33 ff`. This test runs only in
the pinned Mesa-shim build environment; the normal host suite exercises the
same CLI boundary through a deterministic fake executable and never silently
skips a requested capture-environment test.

- [ ] **Step 2: Run RED**

```sh
./proxyenv/bin/python -m unittest tests.test_agx_capture_clear -v
```

Expected: import failure for `tools.agx_capture_clear`.

- [ ] **Step 3: Implement minimal comparison and atomic packaging**

Require two distinct cold-boot proxy identities and two distinct m1n1 bases,
but identical J313/G13/V13_5/ADT/source identities. Canonicalize both captures
through Task 1, compare the complete canonical bytes and final attachment hash,
then write `frame.agx`, `manifest.json`, and `provenance.json` through temporary
files renamed atomically into an initially empty directory.

Implement `tools/agx_clear_capture.c` as a surfaceless 16 by 16 RGBA8 GLES2
context. It sets viewport 16 by 16, disables scissor, depth, stencil, blending,
and dithering, calls `glClearColor(17.0f/255.0f, 34.0f/255.0f,
51.0f/255.0f, 1.0f)`, clears only `GL_COLOR_BUFFER_BIT`, calls `glFinish`,
reads one RGBA8 image, verifies all 256 pixels in-process, and atomically writes
the 1024-byte readback. Any EGL/GL error or pixel mismatch exits nonzero without
an accepted output.

- [ ] **Step 4: Run GREEN and partial-output cleanup tests**

Inject a write failure after the first temporary file and assert the destination
contains no accepted fixture. Run all Task 1 and Task 2 tests together.

- [ ] **Step 5: Commit implementation and ledger separately**

```sh
git add tools/agx_capture_clear.py tools/agx_clear_capture.c \
  tests/test_agx_capture_clear.py
git commit -m "gpu: require reproducible AGX clear captures"
```

Record the exact commit in `investigation/CHANGES.csv`, then commit the ledger
alone with `docs: record reproducible AGX capture packaging`.

---

### Task 3: Pure G1R receipt and one-shot lifecycle

**Files:**
- Create: `tools/agx_render_gate.py`
- Create: `tests/test_agx_render_gate.py`

**Interfaces:**
- Consumes: `AgxContract`, `ValidatedFrame`, a `RenderGateBackend`, exactly one live cycle, fixed deadline, and a fresh evidence directory.
- Produces: `RenderGateError`, `RenderGateBackend`, `RenderGateResult`, `validate_render_completion(receipt: dict, fixture: ValidatedFrame) -> dict`, `run_render_gate(...) -> RenderGateResult`, `aggregate_cold_render_results(...) -> dict`, `verify_render_gate_result(path: Path) -> dict`, and CLI commands `run-one`, `proxy-receipt`, `aggregate-cold`, and `verify-result`.

- [ ] **Step 1: Write failing literal receipt mutation tests**

Name the break: pointer progress, events, stamps, output, mappings, faults, or
deadline can be wrong while a render receipt is accepted.

Use one hand-checked valid receipt whose first-submit deltas are TA `0 -> 2`
and 3D `0 -> 2`, the TA event and 3D event deltas are exactly one each, stamps move from their
literal initial values to their literal submitted values, and output changes
from the fixture poison hash to expected hash. Independently mutate every field:

```python
self._assert_rejected("context_id", 0, "context_id")
self._assert_rejected("ta_done_after", 1, "TA done")
self._assert_rejected("event_3d_matches", 2, "3D event")
self._assert_rejected("output_sha256_after", "2" * 64, "output")
self._assert_rejected("firmware_faults", {"fault": 1}, "firmware fault")
```

Also reject wrong page size, queue index, command counts, producer/read/done
deltas, wrap ambiguity, missing/spurious events, unchanged stamps, elapsed over
0.5 seconds, immutable-object mutation, guard mapping, unexpected mapping,
nonzero readable physical fault, cleanup false, unknown field, and booleans as
integers.

- [ ] **Step 2: Run RED, implement strict validation, then run GREEN**

Run the receipt test class and expect import failure. Implement exact-key
validation and return a deep copy only after every boundary passes. Keep the
fixture expected hash outside the receipt so self-asserted output cannot pass.

- [ ] **Step 3: Write failing lifecycle tests**

Name the break: the gate can grant launch or lose evidence after prepare,
submit, snapshot, stop, reset, or release failure.

Define the protocol:

```python
class RenderGateBackend(Protocol):
    def prepare(self, contract, fixture): ...
    def start(self): ...
    def heartbeat(self) -> dict: ...
    def configure_context(self, context_id: int): ...
    def submit_frame(self, queue_index: int, timeout_s: float) -> dict: ...
    def snapshot(self, reason: str) -> dict: ...
    def stop(self): ...
    def reset(self): ...
    def released(self) -> bool: ...
```

The valid fake must record the exact sequence and the result must remain
`verdict: incomplete`, `windows_launch_permitted: false` after one live cycle.
Each failure path must retain the original error, every obtainable snapshot,
cleanup errors, and false launch permission in atomic JSON.

- [ ] **Step 4: Run RED, implement lifecycle, then run GREEN**

Use the existing queue-gate atomic evidence pattern without importing its
barrier receipt schema. The host monotonic submit interval must independently
enforce 0.5 seconds.

- [ ] **Step 5: Write failing ten-cycle aggregation tests**

Name the break: edited, reordered, warm, reused-identity, incomplete, or fewer
than ten results can qualify the gate.

Build ten literal one-shot results plus ten reset receipts. A valid aggregate
has version 1, ten completed cycles, ten distinct proxy identities and bases,
`cold_reset_between_cycles: true`, deterministic aggregate SHA-256, and launch
permission. Mutate each boundary separately and require rejection.

- [ ] **Step 6: Run RED, implement aggregation and CLI, then run GREEN**

Run all render-gate tests. Exercise `verify-result` in a subprocess against one
valid and one byte-edited aggregate.

- [ ] **Step 7: Commit implementation and ledger separately**

```sh
git add tools/agx_render_gate.py tests/test_agx_render_gate.py
git commit -m "gpu: add strict private render qualification gate"
```

Record the exact implementation commit, then commit the ledger only with
`docs: record private AGX render gate`.

---

### Task 4: Pinned m1n1 complete-render backend

**Files:**
- Create: `tools/agx_m1n1_render_backend.py`
- Create: `tests/test_agx_m1n1_render_backend.py`

**Interfaces:**
- Consumes: live m1n1 `u`, the proven `M1n1AgxBackend`, validated immutable frame bytes, pinned `GPUContext`, `GPUFrame`, `GPURenderer`, queue/event/stamp objects, and fixed context/queue/deadline values.
- Produces: `M1n1AgxRenderBackend` implementing `RenderGateBackend` and returning Task 3's exact completion receipt.

- [ ] **Step 1: Write failing context and mapping-accounting tests**

Name the break: replay can bind context zero, map a guest/display range, hide
the fixed context bootstrap mapping, or leave a mapping unclassified.

Complete fakes must mirror real `GPUContext.bind`, including the `thing` mapping
at `0x6fffff8000`, its TTBR allocation, `uobj`, `gobj`, and `pobj` allocators.
Assert context 63 only; 0x4000 UAT geometry; frame objects loaded at exact
validated VAs; and every map classified as bootstrap, frame, renderer, or
firmware-shared. Reject any other context, overlap, guest/display denylist hit,
or unclassified mapping.

- [ ] **Step 2: Run RED, implement prepare/context setup, then run GREEN**

Delay m1n1 imports until live construction. Compose `M1n1AgxBackend` rather
than copying its firmware startup, heartbeat, fault, and reset code. Pass
validated bytes through a private temporary canonical ZIP only at the
`GPUFrame` loading boundary and delete it during cleanup.

- [ ] **Step 3: Write failing complete-submit tests**

Name the break: only one queue runs, the old barrier-only path returns, output
is self-asserted, or completion is accepted from polling.

The complete fake renderer must expose real-shape `wq_ta`, `wq_3d`,
`stamp_ta1/2`, `stamp_3d1/2`, `stamp_value_ta/3d`, `ev_ta/3d`, and `work`.
Assert:

- output is poisoned and hashed before `submit`;
- `submit` is called exactly once with the fixture command buffer;
- first submit adds two entries to each queue;
- `run()` dispatches 3D then TA through queue index 1;
- the loop services ASC/events until both exact events and done pointers agree;
- final output is pulled from the validated attachment object and compared to
  the fixture's expected hash;
- event without done pointers, done pointers without event, TA-only completion,
  3D-only completion, duplicate/spurious event, wrong stamp, wrong output, and
  timeout all fail.

- [ ] **Step 4: Run RED, implement submission and evidence, then run GREEN**

Do not call upstream `GPURenderer.wait()` because its two-second wait exceeds
the gate deadline and treats polling as sufficient. Use the renderer only to
construct/submit/run the known-valid work, then observe its real queue, stamp,
event, and output objects under the fixed monotonic loop.

- [ ] **Step 5: Write failing teardown and failure-snapshot tests**

Name the break: renderer objects, fixture mappings, context roots, or a useful
fault snapshot can survive cleanup or disappear from evidence.

Assert snapshot includes both queue/channel states, both events, all four
stamps, mapping classification, immutable-object hashes, output hashes,
firmware faults, readable physical faults, SGX IRQ samples, deadline, and
temporary-file state. Teardown frees renderer work/frame/context objects,
clears both context-63 roots under the UAT handoff lock, flushes/invalidates,
invokes G1 reset for context zero, removes the temporary ZIP, and reports
released only when every boundary is clear.

- [ ] **Step 6: Run RED, implement cleanup, then run adjacent GREEN**

```sh
./proxyenv/bin/python -m unittest \
  tests.test_agx_m1n1_render_backend \
  tests.test_agx_m1n1_backend \
  tests.test_agx_render_gate -v
```

- [ ] **Step 7: Add a real-source boundary test**

Import the pinned `GPUFrame` and `GPURenderer` classes in an integration test
without hardware and verify the adapter's attribute expectations exist. This
test catches a submodule ABI mismatch; behavioral completion remains covered by
the complete fake rather than assertions on mock calls.

- [ ] **Step 8: Commit implementation and ledger separately**

```sh
git add tools/agx_m1n1_render_backend.py tests/test_agx_m1n1_render_backend.py
git commit -m "gpu: replay one complete private AGX render job"
```

Record the exact commit, then commit only the ledger with
`docs: record complete AGX render backend`.

---

### Task 5: Capture and replay operators

**Files:**
- Create: `scripts/capture-agx-clear-frame.sh`
- Create: `scripts/run-agx-render-gate.sh`
- Create: `scripts/build-agx-capture-env.sh`
- Create: `scripts/run-agx-capture-container.sh`
- Create: `tools/agx-capture-container/Dockerfile`
- Create: `tools/agx-capture-container/run-capture.sh`
- Create: `tools/verify-agx-capture-env.py`
- Create: `tests/test_run_agx_render_gate.py`
- Create: `tests/test_build_agx_capture_env.py`
- Create: `tests/test_run_agx_capture_container.py`
- Modify: `tools/agx_capture_clear.py`
- Modify: `tools/agx_render_gate.py`

**Interfaces:**
- Consumes: exact proxy, contract, stable recovery manifest, pinned Mesa and m1n1 sources, empty capture/evidence directories, and literal capture or replay mode.
- Produces: two cold capture receipts and one fixture candidate, or one replay receipt per cold boot and an eventual verified aggregate.

- [ ] **Step 1: Write failing operator-boundary tests**

Name the break: an operator can use the wrong source, active guest, dirty
fixture, reused evidence, tunable timeout, warm retry, or changed stable launch.

Execute scripts against controlled temporary repositories and fake proxy tools.
Assert capture dry-run reports the exact fixed clear and two cold identities.
Assert replay dry-run reports context 63, queue 1, TA+3D, 0.5 seconds, and ten
cold cycles. Independently reject missing/dirty Mesa source, m1n1 mismatch,
contract mismatch, active guest, nonempty destination, changed fixture,
recovery-manifest mismatch, cycle count other than ten, absent reboot receipt,
reused proxy identity/base, and unknown option. Assert no `--timeout` option.
Hash every stable launch file before and after each test.

- [ ] **Step 2: Run RED**

```sh
./proxyenv/bin/python -m unittest tests.test_run_agx_render_gate -v
```

Expected: script-not-found failures.

- [ ] **Step 3: Implement capture preflight and fixed-clear invocation**

The capture script must refuse hardware unless the pinned historical Mesa
`LD_PRELOAD` shim library and capture program are present and hash-matched. The
shim is an untracked ELF build artifact under the exact clean Mesa checkout,
not a tracked executable launcher. It sets frame dump and
attachment pull, executes one fixed clear, saves one raw frame and final output,
records identity, and reboots on every exit path. It never installs packages,
downloads a capture, launches Windows, or mutates pinned m1n1.

- [ ] **Step 4: Implement replay runner and CLI wiring**

Reuse the existing G1 preflight and reboot receipt mechanisms. Invoke only
Task 3 CLI commands and Task 4 backend. A one-shot success remains incomplete;
only a verified ten-cycle EXP-080 aggregate may permit optional launch of the
unchanged stable Windows artifacts.

- [ ] **Step 5: Run GREEN and all launch-isolation tests**

```sh
./proxyenv/bin/python -m unittest \
  tests.test_run_agx_render_gate \
  tests.test_run_agx_queue_gate \
  tests.test_run_agx_gate \
  tests.test_launch_profiles \
  tests.test_standalone_builder -v
```

- [ ] **Step 6: Commit implementation and ledger separately**

```sh
git add scripts/capture-agx-clear-frame.sh scripts/run-agx-render-gate.sh \
  tests/test_run_agx_render_gate.py tools/agx_capture_clear.py \
  tools/agx_render_gate.py
git commit -m "gpu: add bounded AGX capture and replay operators"
```

Record the exact commit, then commit only the ledger with
`docs: record AGX capture and replay operators`.

- [x] **Step 7: Correct and reproduce the historical capture environment**

Pin `asahilina/mesa` commit
`7a4f24061fa56ef7eff12132dd7b1461d5a890d8`, its actual
`libasahi_m1n1_drm_shim.so` `LD_PRELOAD` contract, and an ARM64 Ubuntu image by
digest. Build the shim, Asahi DRI driver, EGL/GLES libraries, and fixed-clear
producer inside that image. Export them atomically with an exact hash manifest.
Require an ELF shim under the pinned checkout and its literal SHA-256 at capture
preflight. Mount the repository read-only, evidence separately read-write, and
bridge the host USB serial proxy through a reconnecting PTY.

Verify two independent no-cache builds have byte-identical manifests. Run a
negative control without the shim and require `EGL_NOT_INITIALIZED` with no
output. Loopback-test host serial to TCP to container PTY in both directions.
Do not touch the Air during these host checks.

- [x] **Step 8: Fail closed on deferred bridge readiness**

After EXP-082 proved that `wait-slave` races m1n1's fixed 150 ms bootstrap NOP,
remove the deferred-open contract, keep the PTY alive across capture and receipt
processes, export both repository Python roots globally, and wait at a bounded
post-connect readiness boundary. Add a transport-only operator that records
J313/V13_5 before and after exactly one reboot, requires a changed randomized
m1n1 base, retries only the post-reboot read-only handshake within a fixed
twenty-attempt bound, and never imports or starts AGX. A new capture experiment
is forbidden until that transport-only gate passes under its own preregistration.

---

### Task 6: Host gate, hardware acquisition, one-shot replay, and final qualification

**Files:**
- Modify: `investigation/EXPERIMENTS.md`
- Create after matching cold captures: `fixtures/agx/j313-g13-v13_5-clear-16x16/frame.agx`
- Create after matching cold captures: `fixtures/agx/j313-g13-v13_5-clear-16x16/manifest.json`
- Create after matching cold captures: `fixtures/agx/j313-g13-v13_5-clear-16x16/provenance.json`
- Modify after qualification: `documentation/AGX_BRINGUP.md`

**Interfaces:**
- Consumes: all host-green components and a physical J313 at a fresh sole-owner proxy.
- Produces: a reviewed fixture, one accepted one-shot replay, ten cold EXP-080 receipts, verified aggregate, and unchanged stable Windows verification.

- [ ] **Step 1: Run the complete host gate**

```sh
./proxyenv/bin/python -m unittest discover -s tests -v
git diff --check
git status --short --branch
```

Expected: all root tests pass; only known nested submodule dirt remains; stable
recovery hashes match their recorded manifest.

- [ ] **Step 2: Preregister the two-cold-capture acquisition**

Append exact source commits, capture-program hash, contract/ADT hashes, commands,
two fresh evidence directories, fixed workload, reboot command, comparison
criteria, and stop rules to `investigation/EXPERIMENTS.md`. Commit only that
registration before touching hardware.

- [ ] **Step 3: Acquire capture one, reboot, acquire capture two**

Run only the preregistered commands. Reboot after each run regardless of result.
If identities, canonical bytes, member hashes, final output, fault state, or
cleanup differ, close the experiment as rejected and do not create a fixture.

- [ ] **Step 4: Package and review the fixture**

Run `package-two`, then `verify` against the committed contract and manually
inspect manifest ranges against the denylist. Commit fixture plus acquisition
result only when both cold captures are byte-reproducible and fault-free.

- [ ] **Step 5: Preregister and run one replay**

Register a fresh experiment with the exact fixture hash and current source
commits. Run one replay once, force reboot, record the atomic result hash and
post-reboot identity. A failure closes that experiment and returns to evidence
analysis; no in-place retry or Windows launch is allowed.

- [ ] **Step 6: Preregister and execute reserved EXP-080**

Only after the one-shot replay passes, bind EXP-080 to the exact fixture and
implementation commits. Execute ten one-shot cycles separated by ten physical
reboots and fresh identities. Aggregate only after all ten immutable receipts
pass independent verification.

- [ ] **Step 7: Verify unchanged stable Windows**

After the EXP-080 aggregate permits launch, boot the byte-identical stable
Windows artifacts and verify eight CPUs, NVMe, physical xHCI, native keyboard,
Precision Touchpad, and physical DCP scanout. G1R/web-display diagnostics remain
off during this verification.

- [ ] **Step 8: Document, commit, and push only the qualified result**

Update `documentation/AGX_BRINGUP.md` with exact fixture provenance, commands,
receipt interpretation, rollback, and explicit G1R non-claims. Commit docs and
hardware results without attribution trailers. Run verification-before-completion,
then push the feature branch only after the complete hardware and Windows gate
passes.
