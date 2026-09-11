# J313 AGX G0/G1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a read-only J313 AGX inventory contract and a bounded assisted firmware start/heartbeat/reset harness without changing the validated Windows boot artifact.

**Architecture:** G0 captures live ADT metadata without hardware writes, validates it through a strict pure-Python schema, and emits canonical JSON plus SHA-256. G1 uses an injected backend and explicit state machine to run ten assisted AGX firmware lifecycle cycles, records evidence for every deadline, releases ownership completely, and only then permits the unchanged stable Windows launcher to run.

**Tech Stack:** Python 3 standard library, m1n1 proxyclient AGX/AGXASC/UAT primitives, POSIX shell, `unittest`, JSON, SHA-256, existing artifact manifests and experiment ledgers.

**Spec:** `documentation/design/2026-08-13-agx-windows-acceleration.md`

## Global Constraints

- Work only on `feature/j313-gpu-acceleration` under `/Users/pavel/public_windows`.
- Preserve root `7f8492edecb96d03a6f6915b0877cb46611fcc7f`, Mu `8b4dc4b4e3ff8606d0af36163acf9de79b7b4737`, and m1n1 `9cd80ac652ac404e92ae279deeaec8c629d7d184` as the recovery source baseline.
- Never modify or overwrite `.local/recovery/STABLE-j313-8core-native-input-v1/`.
- G0 performs no clock, power, MMIO, DART, UAT, or interrupt-controller writes.
- G1 runs only before guest entry in an explicit assisted diagnostic command; production and standalone profiles remain unchanged.
- Every wait has a monotonic deadline and every failure saves evidence before one bounded reset attempt.
- Never boot Windows if G1 still owns AGX or reset state is unknown.
- Do not publish an ACPI AGX device and do not add a Windows display driver in this plan.
- No per-command m1n1/proxy design may be introduced; G2 will transfer direct runtime ownership to the Windows KMD.
- Every production behavior is written test-first and observed failing before implementation.
- Every hardware run is entered in `investigation/EXPERIMENTS.md` before launch and completed afterward.
- Every implementation commit receives an exact 40-character row in `investigation/CHANGES.csv`; the ledger-only commit does not receive its own row.
- No `Co-Authored-By`, assistant attribution, or session trailers.

---

## File map

- `tools/agx_contract.py`: immutable G0 contract types, exact-key validation, canonical serialization, digest, and cross-resource safety checks.
- `tools/agx_inventory.py`: pure conversion from raw ADT records to the strict contract; no m1n1 imports.
- `tools/agx_live_inventory.py`: guarded live ADT reader; the only module that imports `m1n1.setup`, and only after confirming no guest owns the proxy.
- `tools/agx_gate.py`: pure G1 lifecycle state machine, deadline handling, evidence manifest, and backend protocol.
- `tools/agx_m1n1_backend.py`: thin adapter over existing m1n1 AGX/AGXASC/UAT lifecycle primitives.
- `scripts/run-agx-gate.sh`: explicit assisted-only operator entry point; verifies source/artifact/recovery identities and never mutates normal launch defaults.
- `config/j313-agx.json`: reviewed canonical contract created from the first accepted live G0 capture.
- `tests/fixtures/j313-agx-adt.json`: sanitized raw G0 capture used for deterministic host tests.
- `tests/test_agx_contract.py`: schema, overlap, alignment, canonical hash, and unsupported-version tests.
- `tests/test_agx_inventory.py`: raw ADT extraction and provenance tests.
- `tests/test_agx_live_inventory.py`: guest-ownership and no-write source/API audit.
- `tests/test_agx_gate.py`: state transitions, deadline, cleanup, evidence, and ten-cycle behavior with a deterministic fake backend.
- `tests/test_run_agx_gate.py`: shell-level fail-closed and profile-isolation tests.
- `documentation/AGX_BRINGUP.md`: operator commands, evidence interpretation, recovery, and explicit non-acceleration status.
- `investigation/EXPERIMENTS.md`: pre/post hardware records.
- `investigation/CHANGES.csv`: durable commit ledger.

---

### Task 1: Strict canonical AGX contract

**Files:**
- Create: `tools/agx_contract.py`
- Create: `tests/test_agx_contract.py`

**Interfaces:**
- Produces: `ContractError`, `Region`, `AgxContract`, `load_contract(path: Path) -> AgxContract`, `validate_contract(data: dict) -> AgxContract`, `canonical_bytes(contract: AgxContract) -> bytes`, and `contract_sha256(contract: AgxContract) -> str`.
- Consumes: Python standard library only.

- [ ] **Step 1: Write the failing exact-schema and canonical-hash tests**

```python
class AgxContractTests(unittest.TestCase):
    def test_valid_contract_round_trips_canonically(self):
        contract = validate_contract(valid_contract_dict())
        encoded = canonical_bytes(contract)
        self.assertTrue(encoded.endswith(b"\n"))
        self.assertEqual(encoded, canonical_bytes(validate_contract(json.loads(encoded))))
        self.assertEqual(contract_sha256(contract), hashlib.sha256(encoded).hexdigest())

    def test_unknown_top_level_key_is_rejected(self):
        data = valid_contract_dict()
        data["surprise"] = True
        with self.assertRaisesRegex(ContractError, "keys must be exactly"):
            validate_contract(data)

    def test_overlapping_regions_are_rejected(self):
        data = valid_contract_dict()
        data["regions"]["shared"] = data["regions"]["gpu"].copy()
        with self.assertRaisesRegex(ContractError, "overlap"):
            validate_contract(data)
```

- [ ] **Step 2: Run RED and confirm the module is missing**

Run: `python3 -m unittest tests.test_agx_contract -v`

Expected: import failure for `tools.agx_contract`, not a syntax or fixture error.

- [ ] **Step 3: Implement the minimal immutable contract**

Use frozen dataclasses. Require exactly these top-level keys:

```python
TOP_LEVEL = {
    "contract_version", "platform", "source", "firmware", "nodes",
    "regions", "interrupts", "dependencies", "uat",
}
REGION_KEYS = {"base", "size"}
REGION_NAMES = {"sgx_mmio", "asc_mmio", "rtkit_private", "gpu",
                "shared", "handoff"}
```

`contract_version` must be integer `1`; platform must be `J313`; bases and
sizes must be positive, 16 KiB aligned, non-wrapping 64-bit values. Every
region pair must be non-overlapping except relationships explicitly represented
as separate MMIO and virtual-address classes. Interrupts must be a non-empty
unique list in `32..1019`. Source commits must be lowercase 40-digit hex.
Canonical output is `json.dumps(..., sort_keys=True, indent=2) + "\n"` encoded
as UTF-8.

- [ ] **Step 4: Run GREEN and the existing change-ledger contract**

Run: `python3 -m unittest tests.test_agx_contract tests.test_change_ledger -v`

Expected: all tests pass with no warnings.

- [ ] **Step 5: Commit the contract implementation**

```bash
git add tools/agx_contract.py tests/test_agx_contract.py
git commit -m "gpu: add strict J313 AGX contract schema"
```

- [ ] **Step 6: Append its exact commit to `investigation/CHANGES.csv` and commit only the ledger**

Status is `implemented`; hardware result is empty. Reproduction is an unknown
key, overlap, misalignment, unsupported version, or non-40-character source
commit accepted by the pre-change tree.

---

### Task 2: Pure ADT-to-contract extraction

**Files:**
- Create: `tools/agx_inventory.py`
- Create: `tests/test_agx_inventory.py`

**Interfaces:**
- Consumes: raw dictionary returned by `agx_live_inventory.capture_raw()` and source commits.
- Produces: `extract_contract(raw: dict, source: dict) -> AgxContract` and `required_paths() -> tuple[str, ...]`.

- [ ] **Step 1: Write failing extraction tests with an inline synthetic ADT record**

The synthetic record uses arbitrary aligned addresses and the exact raw ADT
property names, never guessed J313 production constants:

```python
def test_extracts_required_regions_and_interrupts(self):
    contract = extract_contract(raw_inventory(), source_commits())
    self.assertEqual(contract.platform, "J313")
    self.assertEqual(contract.regions["sgx_mmio"].base, 0x204000000)
    self.assertEqual(contract.regions["gpu"].size, 0x40000)
    self.assertEqual(contract.interrupts, (180, 181, 182))

def test_missing_handoff_size_is_rejected(self):
    raw = raw_inventory()
    del raw["nodes"]["/arm-io/sgx"]["properties"]["gfx-handoff-size"]
    with self.assertRaisesRegex(ContractError, "gfx-handoff-size"):
        extract_contract(raw, source_commits())
```

- [ ] **Step 2: Run RED**

Run: `python3 -m unittest tests.test_agx_inventory -v`

Expected: import failure for `tools.agx_inventory`.

- [ ] **Step 3: Implement the exact property mapping**

Required paths are `/arm-io/sgx` and `/arm-io/gfx-asc`. Map SGX properties
`rtkit-private-vm-region-base/size`, `gpu-region-base/size`,
`gfx-shared-region-base/size`, and `gfx-handoff-base/size`; obtain SGX/ASC MMIO
from the first exact `reg` tuple; preserve all interrupt values and dependency
records without renumbering. Reject multiple matching nodes or a missing
property.

- [ ] **Step 4: Run GREEN**

Run: `python3 -m unittest tests.test_agx_contract tests.test_agx_inventory -v`

- [ ] **Step 5: Commit and add the subsequent ledger-only commit**

Implementation message: `gpu: extract AGX contract from ADT inventory`.

---

### Task 3: Guarded read-only live inventory

**Files:**
- Create: `tools/agx_live_inventory.py`
- Create: `tests/test_agx_live_inventory.py`

**Interfaces:**
- Consumes: m1n1 `u.adt` only after `ensure_guest_inactive(ROOT)`.
- Produces: `node_record(node) -> dict`, `capture_raw() -> dict`, and CLI `--output PATH`.

- [ ] **Step 1: Write the failing ownership and no-write audit tests**

```python
def test_live_inventory_refuses_active_guest(self):
    with tempfile.TemporaryDirectory() as tmp:
        Path(tmp, "guest.pid").write_text(str(os.getpid()))
        with self.assertRaisesRegex(RuntimeError, "guest runner"):
            ensure_guest_inactive(Path(tmp))

def test_live_inventory_source_has_no_write_capable_api(self):
    source = LIVE_INVENTORY.read_text()
    for forbidden in ("u.proxy", " p.", "write32", "write64", "writemem",
                      "pmgr_adt_clocks_enable", "iomap", "dart"):
        self.assertNotIn(forbidden, source)
    self.assertIn("from m1n1.setup import u", source)
    self.assertLess(source.index("ensure_guest_inactive(ROOT)"),
                    source.index("capture_raw()"))
```

- [ ] **Step 2: Run RED**

Run: `python3 -m unittest tests.test_agx_live_inventory -v`

- [ ] **Step 3: Implement the smallest ADT reader**

Delay `from m1n1.setup import u` until after the guest ownership guard. Read
only node paths, `compatible`, raw properties, `get_reg()`, and `interrupts`.
Emit canonical raw JSON with format version, target type, and records for the
two required paths plus dependency paths referenced by their properties.

- [ ] **Step 4: Run GREEN and repository hygiene tests**

Run: `python3 -m unittest tests.test_agx_live_inventory tests.test_repository_hygiene -v`

- [ ] **Step 5: Commit and add the subsequent ledger-only commit**

Implementation message: `gpu: add read-only live AGX inventory`.

---

### Task 4: Capture and review the real J313 G0 contract

**Files:**
- Create from accepted capture: `tests/fixtures/j313-agx-adt.json`
- Create from accepted extraction: `config/j313-agx.json`
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `tests/test_agx_contract.py`
- Modify: `tests/test_agx_inventory.py`

**Interfaces:**
- Consumes: live Air in `Running proxy...`, exact stable m1n1 artifact, Tasks 1-3.
- Produces: reviewed source-of-truth contract and fixture.

- [ ] **Step 1: Enter the G0 experiment before connecting**

Record commits, dirty diff hashes, proxy device, command, destination under
`investigation/artifacts/EXP-20260825-073-agx-g0/`, stable recovery hashes, expected
required paths, and the failure criterion of any write-capable call or missing
resource.

- [ ] **Step 2: Run the exact read-only capture**

```bash
M1N1DEVICE=/dev/cu.usbmodemC02HDNCCQ6L41 \
  proxyenv/bin/python tools/agx_live_inventory.py \
  --output investigation/artifacts/EXP-20260825-073-agx-g0/raw-adt.json
```

Do not launch Windows or enable AGX clocks during this capture.

- [ ] **Step 3: Extract, validate, and hash without editing values**

Run a CLI entry in `tools/agx_inventory.py` that accepts `--input`, `--output`,
and exact root/m1n1/Mu commits. Verify the output twice and require identical
SHA-256 values.

- [ ] **Step 4: Review every resource against current m1n1 accessors and Asahi notes**

Reject the capture if any address, size, interrupt, dependency, firmware
generation, or UAT property is absent or ambiguous. Copy only the accepted raw
record to the fixture and its canonical extracted result to `config/j313-agx.json`.

- [ ] **Step 5: Add exact-value regression assertions and run full host tests**

Run: `python3 -m unittest discover -s tests -v`

- [ ] **Step 6: Complete the experiment record, commit the reviewed contract, and add a ledger-only commit**

Implementation message: `gpu: record reviewed J313 AGX resources`. Status is
`validated` only if the live capture was deterministic and the source audit
proved no hardware write API was reachable.

---

### Task 5: Pure bounded G1 lifecycle and evidence model

**Files:**
- Create: `tools/agx_gate.py`
- Create: `tests/test_agx_gate.py`

**Interfaces:**
- Defines backend protocol methods `prepare(contract)`, `start()`,
  `heartbeat() -> dict`, `snapshot(reason) -> dict`, `stop()`, `reset()`, and
  `released() -> bool`.
- Produces: `run_gate(backend, contract, cycles, timeout_s, evidence_dir, clock=time.monotonic) -> GateResult`.

- [ ] **Step 1: Write failing state-machine tests**

```python
def test_ten_cycles_release_ownership(self):
    backend = FakeBackend()
    result = run_gate(backend, contract(), cycles=10, timeout_s=1,
                      evidence_dir=self.path, clock=backend.clock)
    self.assertEqual(result.completed_cycles, 10)
    self.assertTrue(backend.released())
    self.assertEqual(backend.calls.count("start"), 10)
    self.assertEqual(backend.calls.count("reset"), 10)

def test_heartbeat_timeout_saves_snapshot_and_fails_closed(self):
    backend = FakeBackend(stall_heartbeat=True)
    with self.assertRaisesRegex(GateError, "heartbeat deadline"):
        run_gate(backend, contract(), cycles=10, timeout_s=1,
                 evidence_dir=self.path, clock=backend.clock)
    manifest = json.loads((self.path / "gate-result.json").read_text())
    self.assertEqual(manifest["verdict"], "failed")
    self.assertIn("snapshot", manifest["cycles"][0])
    self.assertFalse(manifest["windows_launch_permitted"])
```

- [ ] **Step 2: Run RED**

Run: `python3 -m unittest tests.test_agx_gate -v`

- [ ] **Step 3: Implement minimal state transitions and atomic evidence writes**

Allowed sequence per cycle is `prepare -> start -> heartbeat -> snapshot -> stop
-> reset -> released`. Use `time.monotonic()` for deadlines and write JSON to a
temporary sibling followed by `Path.replace()`. Set
`windows_launch_permitted=true` only after exactly ten successful cycles and a
final `released()` check.

- [ ] **Step 4: Run GREEN**

Run: `python3 -m unittest tests.test_agx_gate -v`

- [ ] **Step 5: Commit and add the subsequent ledger-only commit**

Implementation message: `gpu: add bounded AGX firmware gate state machine`.

---

### Task 6: m1n1 AGX backend adapter

**Files:**
- Create: `tools/agx_m1n1_backend.py`
- Modify only if a missing lifecycle primitive is proven: focused files under `m1n1_windows/proxyclient/m1n1/agx/`
- Create: `tests/test_agx_m1n1_backend.py`

**Interfaces:**
- Implements the Task 5 backend protocol using existing `AGX`, `AGXASC`, `UAT`,
  channel, event, fault, and recovery primitives.
- Consumes: validated `AgxContract`; refuses a live ADT mismatch before enabling clocks.

- [ ] **Step 1: Write failing adapter boundary tests with injected fake m1n1 objects**

Assert that `prepare()` compares every reviewed live resource before calling
either `pmgr_adt_clocks_enable`; that `heartbeat()` requires management/event
progress; that `snapshot()` contains firmware state, SGX IRQ state, fault state,
and UAT mappings; and that `reset()` invalidates private mappings before
reporting released ownership.

- [ ] **Step 2: Run RED**

Run: `python3 -m unittest tests.test_agx_m1n1_backend -v`

- [ ] **Step 3: Implement the thin adapter without render submission**

Use existing m1n1 calls for `/arm-io/gfx-asc` and `/arm-io/sgx` clock enable,
`AGX.start()`, channel polling, fault inspection, `AGX.stop()`, and reviewed
reset primitives. Do not import `m1n1.agx.render`, instantiate `GPUContext`,
create `GPUWorkQueue`, or submit a command buffer.

- [ ] **Step 4: Run GREEN and the complete m1n1 host suite**

Run the focused unittest, then the repository's existing full m1n1 host-test
command used by `scripts/build-standalone.sh`.

- [ ] **Step 5: Commit component changes first if any, update the root gitlink, then add exact ledger rows**

Component message: `gpu: expose bounded firmware lifecycle diagnostics`.
Root adapter message: `gpu: connect G1 gate to m1n1 lifecycle`.

---

### Task 7: Explicit assisted-only operator command

**Files:**
- Create: `scripts/run-agx-gate.sh`
- Create: `tests/test_run_agx_gate.py`
- Modify: `tests/test_public_scripts.py`

**Interfaces:**
- CLI requires `--proxy`, `--contract`, `--artifact-dir`, `--evidence-dir`, and
  the literal confirmation `--cycles 10`.
- On success only, optional `--launch-stable-windows` calls the existing
  `scripts/run-assisted.sh` with the same exact stable artifact directory.

- [ ] **Step 1: Write failing dry-run and rejection tests**

Test that missing manifest, mismatched hashes, cycles other than ten, an active
guest PID, a dirty recovery directory, or a G1 result without
`windows_launch_permitted=true` exits nonzero. Assert that
`scripts/run-assisted.sh`, `scripts/build-standalone.sh`, launch profiles, and
standalone manifests are not modified.

- [ ] **Step 2: Run RED**

Run: `python3 -m unittest tests.test_run_agx_gate -v`

- [ ] **Step 3: Implement the fail-closed wrapper**

Verify the canonical contract digest, artifact manifest, m1n1 SHA-256, source
commits, and stable recovery hashes before invoking Python. Print each gate and
evidence path. Never infer a profile or choose the newest build directory.

- [ ] **Step 4: Run GREEN and the complete root suite**

Run: `python3 -m unittest discover -s tests -v`

- [ ] **Step 5: Commit and add the subsequent ledger-only commit**

Implementation message: `gpu: add assisted J313 firmware gate runner`.

---

### Task 8: Real G1 ten-cycle qualification and documentation

**Files:**
- Modify: `investigation/EXPERIMENTS.md`
- Create: `documentation/AGX_BRINGUP.md`
- Modify: `documentation/ROADMAP.md`
- Modify: `documentation/LIMITATIONS.md`
- Modify: `investigation/CHANGES.csv`

**Interfaces:**
- Consumes: exact G0 contract, exact stable assisted artifact, Task 7 runner,
  Air in `Running proxy...`.
- Produces: ten-cycle evidence and a post-gate stable Windows health result.

- [ ] **Step 1: Record the hardware experiment before launch**

Include exact commits, dirty diff hashes, contract/artifact/recovery hashes,
command, evidence directory, expected ten cycles, deadline values, Windows
boot gate, and rollback command.

- [ ] **Step 2: Run G1 with display `both` and monitor diagnostics**

Use the exact explicit Task 7 command. Failure of any cycle ends the experiment;
do not retry with changed timing in the same experiment.

- [ ] **Step 3: On ten successful cycles, launch the unchanged stable Windows artifact**

Require lock screen within 30 seconds, eight CPUs, live physical and virtual
frames, responsive USB and native input, healthy NVMe, SSH response, and zero
new BugCheck, WHEA, stornvme, storage reset, watchdog, or AGX ownership errors.

- [ ] **Step 4: Complete the experiment with exact timings and verdict**

Use `validated` only if all ten cycles and the post-gate Windows health checks
pass. Otherwise use `rejected` or `inconclusive` and preserve all evidence.

- [ ] **Step 5: Document only demonstrated behavior**

`documentation/AGX_BRINGUP.md` must state that G1 proves firmware lifecycle,
not rendering or acceleration; include commands, hashes, evidence fields,
recovery, and the next G2 ownership transfer. Update the roadmap and limitations
without claiming a Windows GPU adapter.

- [ ] **Step 6: Run complete verification**

Run root full tests, m1n1 full host tests, `git diff --check`, contract hash
verification, and evidence-manifest validation.

- [ ] **Step 7: Commit documentation and final ledger row, then push the three GPU branches**

Documentation message: `docs: record J313 AGX firmware gate result`.
Push only after verifying every remote branch resolves to the recorded commit
and root gitlinks point to those exact component commits.

---

## Self-review

- Spec coverage: G0 read-only inventory, strict versioned contract, G1 bounded
  lifecycle, ten resets, fail-closed recovery, unchanged Windows artifact,
  ownership separation, evidence, ledgers, and final no-proxy performance rule
  each map to an explicit task.
- Placeholder scan: runtime hardware values are deliberately obtained from the
  live capture and are never represented by guessed constants; all interfaces,
  files, commands, failure behavior, and gates are explicit.
- Type consistency: Tasks 2-8 consume the `AgxContract` produced by Task 1;
  Task 6 implements the backend protocol defined by Task 5; Task 7 consumes the
  Task 5 result field `windows_launch_permitted`; Task 8 uses only Task 7's
  explicit runner.
