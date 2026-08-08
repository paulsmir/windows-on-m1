# Launch Contract Preflight Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a structured J313 launch contract from a proven assisted boot and block standalone Windows entry whenever its normalized pre-guest state differs from that golden contract.

**Architecture:** A small freestanding C library owns the binary schema, normalization rules, comparison, and failure records. Platform-specific providers populate that schema in assisted and standalone paths, while Python tools decode captures to JSON and manage the sanitized golden fixture. Stage-0 and stage-1 are built as distinct artifacts so bootstrap-only code cannot silently alter the measured hypervisor.

**Tech Stack:** freestanding C11 in m1n1, existing m1n1 host-test runner, Python 3.10–3.12 standard library, existing UART proxy/USB monitor transport, Homebrew Clang 22.1.8 for the validated stage-1 build.

## Implementation checkpoint (2026-08-08)

- Complete: versioned contract/CRC/comparator and deterministic provider boundary.
- Complete: J313 CPU register image, explicit base-state validation, live IRQ enumeration,
  live PCI/NVMe state, and a common assisted/standalone stage-2 operation recorder.
- Complete: stage-2 mappings participate in the fail-closed contract as an order-independent
  set; overflow or invalid mapping/IRQ counts invalidate the snapshot.
- Next: publish the same boot/region/device descriptor from assisted Python and standalone C,
  add remaining display/xHCI/DART/VUART getters, then implement framed transport and capture the
  first assisted golden contract. The capture API is deliberately not called before this shared
  publication boundary exists.
- Validation at this checkpoint: complete host suite passes; changed freestanding objects compile.
  The existing Makefile still emits its pre-existing duplicate target `&` warnings.

## Global Constraints

- The assisted launch is the golden behavioral reference.
- `/Users/pavel/windows` is read-only; implementation occurs in `/Users/pavel/public_windows` or an isolated worktree created from the recorded assisted revision.
- Required mismatches at `PRE_HV_INIT`, `POST_HV_INIT`, `POST_MAPS`, or `PRE_GUEST` block `hv_start()`.
- Dynamic values are normalized only by explicit `exact`, `masked`, `relative-region`, `set`, `digest`, or `range` rules.
- Unknown fields, unsupported schema versions, missing checkpoints, duplicate sequences, and checksum failures are blocking errors.
- Raw machine captures and unique identifiers remain under ignored `.local/`; only sanitized contracts are public.
- Stage-0 and stage-1 build identities are distinct and recorded in every image.
- No ESP installation occurs until unit, golden, negative, and offline image checks pass.
- Hardware success requires all eight CPU entry records and Windows progress beyond the static logo.

---

### Task 1: Versioned Binary Contract and Normalization Core

**Files:**
- Create: `m1n1_windows/src/hv_launch_contract.h`
- Create: `m1n1_windows/src/hv_launch_contract.c`
- Create: `m1n1_windows/tests/hv_launch_contract_test.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Produces: `enum hv_contract_checkpoint`, `struct hv_contract_snapshot`, `struct hv_contract_rule`, `struct hv_contract_failure`.
- Produces: `bool hv_contract_finalize(struct hv_contract_snapshot *snapshot)`.
- Produces: `bool hv_contract_compare(const struct hv_contract_snapshot *golden, const struct hv_contract_snapshot *actual, const struct hv_contract_schema *schema, struct hv_contract_failure *failure)`.

- [ ] **Step 1: Write the failing host test**

Create a test that constructs two snapshots with different raw heap bases but identical relative-region semantics, then verifies an exact ACTLR mismatch fails:

```c
struct hv_contract_failure failure = {0};
struct hv_contract_snapshot golden = test_snapshot();
struct hv_contract_snapshot actual = golden;

actual.regions[HV_CONTRACT_REGION_HEAP].base += 0x200000;
assert(hv_contract_finalize(&golden));
assert(hv_contract_finalize(&actual));
assert(hv_contract_compare(&golden, &actual, &J313_TEST_SCHEMA, &failure));

actual.cpus[1].actlr ^= (1ULL << 12);
assert(hv_contract_finalize(&actual));
assert(!hv_contract_compare(&golden, &actual, &J313_TEST_SCHEMA, &failure));
assert(failure.field == HV_CONTRACT_FIELD_CPU_ACTLR);
assert(failure.index == 1);
```

- [ ] **Step 2: Run the test and verify RED**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh hv_launch_contract_test`

Expected: compilation fails because `hv_launch_contract.h` and its types do not exist.

- [ ] **Step 3: Implement the minimal schema and comparator**

Use fixed-width, packed fields and no dynamic allocation. The header begins with:

```c
#define HV_CONTRACT_MAGIC 0x4a43314cU /* "L1CJ" on the wire */
#define HV_CONTRACT_VERSION 1
#define HV_CONTRACT_MAX_CPUS 8

enum hv_contract_checkpoint {
    HV_CONTRACT_PRE_HV_INIT,
    HV_CONTRACT_POST_HV_INIT,
    HV_CONTRACT_POST_MAPS,
    HV_CONTRACT_PRE_GUEST,
    HV_CONTRACT_CPU_ENTRY,
};

enum hv_contract_rule_kind {
    HV_CONTRACT_EXACT,
    HV_CONTRACT_MASKED,
    HV_CONTRACT_RELATIVE_REGION,
    HV_CONTRACT_SET,
    HV_CONTRACT_DIGEST,
    HV_CONTRACT_RANGE,
};
```

`hv_contract_finalize()` sets payload size and CRC32. `hv_contract_compare()` first validates magic, version, checkpoint, sequence, size, and CRC, then applies every schema rule and returns the first stable field-level failure.

- [ ] **Step 4: Run focused and complete host tests**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh hv_launch_contract_test`

Expected: `hv_launch_contract_test: ok`.

Run: `cd m1n1_windows && ./tests/run_host_tests.sh`

Expected: all existing and new host tests pass.

- [ ] **Step 5: Commit the contract core**

```bash
git add src/hv_launch_contract.c src/hv_launch_contract.h tests/hv_launch_contract_test.c tests/run_host_tests.sh Makefile
git commit -m "feat: add launch contract core"
```

### Task 2: Deterministic Snapshot Builder with Provider Boundary

**Files:**
- Create: `m1n1_windows/src/hv_launch_snapshot.h`
- Create: `m1n1_windows/src/hv_launch_snapshot.c`
- Create: `m1n1_windows/tests/hv_launch_snapshot_test.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Consumes: contract types from Task 1.
- Produces: `struct hv_launch_snapshot_provider` containing callbacks for registers, boot arguments, mappings, CPU topology, IRQ routes, and device state.
- Produces: `bool hv_launch_snapshot_collect(enum hv_contract_checkpoint checkpoint, uint32_t sequence, const struct hv_launch_snapshot_provider *provider, struct hv_contract_snapshot *out)`.

- [ ] **Step 1: Write a fake-provider test**

The test provider returns fixed J313 values and records callback order. Assert that collection is deterministic even when destination memory begins with `0xa5` bytes:

```c
struct fake_provider fake = j313_fake_provider();
struct hv_contract_snapshot first, second;
memset(&first, 0xa5, sizeof(first));
memset(&second, 0x5a, sizeof(second));
assert(hv_launch_snapshot_collect(HV_CONTRACT_PRE_GUEST, 4, &fake.ops, &first));
assert(hv_launch_snapshot_collect(HV_CONTRACT_PRE_GUEST, 4, &fake.ops, &second));
assert(memcmp(&first, &second, sizeof(first)) == 0);
assert(first.cpu_count == 8);
```

- [ ] **Step 2: Run the new test and verify RED**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh hv_launch_snapshot_test`

Expected: failure because the snapshot provider interface is absent.

- [ ] **Step 3: Implement collection without platform reads in the core**

The collector must zero the complete output, call each provider exactly once, reject more than eight CPUs or regions outside fixed capacities, and call `hv_contract_finalize()` only after all required fields are present. It must not call `mrs`, inspect globals, print, allocate, or perform USB I/O.

- [ ] **Step 4: Add missing-provider and overflow negative cases**

Assert collection fails for a null required callback, nine CPUs, duplicate CPU affinities, an invalid checkpoint, and a provider region count above the schema limit.

- [ ] **Step 5: Run all host tests and commit**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh`

Expected: all tests pass.

```bash
git add src/hv_launch_snapshot.c src/hv_launch_snapshot.h tests/hv_launch_snapshot_test.c tests/run_host_tests.sh Makefile
git commit -m "feat: collect deterministic launch snapshots"
```

### Task 3: J313 Schema and Local m1n1 Provider

**Files:**
- Create: `m1n1_windows/src/hv_launch_j313.h`
- Create: `m1n1_windows/src/hv_launch_j313.c`
- Create: `m1n1_windows/tests/hv_launch_j313_test.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Consumes: snapshot provider and contract types from Tasks 1–2.
- Produces: `const struct hv_contract_schema HV_J313_CONTRACT_SCHEMA`.
- Produces: `void hv_launch_j313_provider_init(struct hv_launch_snapshot_provider *provider)`.
- Produces: `bool hv_launch_j313_capture(enum hv_contract_checkpoint checkpoint, uint32_t sequence, struct hv_contract_snapshot *out)`.

- [ ] **Step 1: Write the J313 schema test**

Assert exact rules for guest IPA, CPU affinity, firmware entry, vINTIDs, AP keys, APSTS, and required ACTLR/HACR bits. Assert relative-region rules for heap, framebuffer physical backing, and DART tables. Assert masks are exactly the documented architectural bits.

- [ ] **Step 2: Run the test and verify RED**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh hv_launch_j313_test`

Expected: undefined J313 schema/provider symbols.

- [ ] **Step 3: Implement the platform provider**

Read existing state rather than recreating it. Provider callbacks source data from `cur_boot_args`, PSCI topology, vGIC state, stage-2 mapping records, PCI/NVMe state, xHCI/DART handoff state, and display configuration. The register callback reads:

```c
out->hacr = mrs(HACR_EL2);
out->mdcr = mrs(MDCR_EL2);
out->mdscr = mrs(MDSCR_EL1);
out->amx_config = mrs(SYS_IMP_APL_AMX_CTL_EL1);
out->apvmkeylo = mrs(SYS_IMP_APL_APVMKEYLO_EL2);
out->apvmkeyhi = mrs(SYS_IMP_APL_APVMKEYHI_EL2);
out->apsts = mrs(SYS_IMP_APL_APSTS_EL12);
out->actlr = cpu_features->actlr_el2 ? mrs(SYS_ACTLR_EL12)
                                      : mrs(SYS_IMP_APL_ACTLR_EL12);
```

Under `HV_LAUNCH_J313_HOST_TEST`, replace architectural reads with injected values so the schema test runs on macOS.

- [ ] **Step 4: Run focused and full tests**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh hv_launch_j313_test`

Expected: `hv_launch_j313_test: ok`.

Run: `cd m1n1_windows && ./tests/run_host_tests.sh`

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/hv_launch_j313.c src/hv_launch_j313.h tests/hv_launch_j313_test.c tests/run_host_tests.sh Makefile
git commit -m "feat: describe J313 launch contract"
```

### Task 4: Snapshot Transport and Host Decoder

**Files:**
- Create: `m1n1_windows/src/hv_launch_transport.h`
- Create: `m1n1_windows/src/hv_launch_transport.c`
- Create: `m1n1_windows/tests/hv_launch_transport_test.c`
- Create: `tools/launch_contract.py`
- Create: `tests/test_launch_contract.py`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Consumes: finalized snapshots from Task 2.
- Produces: framed records with magic `J313CONTRACT`, payload length, checkpoint, sequence, payload, and CRC32.
- Produces Python: `decode_records(data: bytes) -> list[Snapshot]`, `normalize(snapshot: Snapshot, schema: Schema) -> dict`, and `compare(golden: dict, actual: dict) -> list[Difference]`.

- [ ] **Step 1: Write failing C framing tests and Python decoder tests**

The C test verifies partial writes resume without duplication. The Python test feeds two concatenated records in uneven chunks and expects two decoded snapshots; corrupting one payload byte must raise `ContractDecodeError("CRC mismatch")`.

- [ ] **Step 2: Run both tests and verify RED**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh hv_launch_transport_test`

Run: `python3 -m unittest tests.test_launch_contract -v`

Expected: missing transport and decoder modules.

- [ ] **Step 3: Implement bounded framing and strict decoding**

The C writer takes an injected sink callback and never blocks indefinitely. The Python decoder uses `struct.Struct` with explicit little-endian formats, rejects trailing bytes, unknown versions, invalid lengths, duplicate checkpoint/sequence pairs, and more than eight CPU records.

- [ ] **Step 4: Implement normalized JSON output**

Expose:

```bash
python3 tools/launch_contract.py decode capture.bin --output capture.json
python3 tools/launch_contract.py compare golden.json actual.json
```

The compare command exits `0` on match and `1` on differences, printing one stable field path per line.

- [ ] **Step 5: Run tests and commit**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh && cd .. && python3 -m unittest tests.test_launch_contract -v`

Expected: all C and Python tests pass.

```bash
git add m1n1_windows/src/hv_launch_transport.c m1n1_windows/src/hv_launch_transport.h m1n1_windows/tests/hv_launch_transport_test.c m1n1_windows/tests/run_host_tests.sh m1n1_windows/Makefile tools/launch_contract.py tests/test_launch_contract.py
git commit -m "feat: transport and decode launch contracts"
```

### Task 5: Calibrate the Collector against the Unmodified Assisted Reference

**Files:**
- Create: `scripts/capture-assisted-contract.sh`
- Create: `tools/capture_assisted_contract.py`
- Create: `tests/test_capture_assisted_contract.py`
- Modify: `scripts/run-assisted.sh`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: `/Users/pavel/windows` only as a read-only source of the known-good assisted scripts, binaries, ELF symbols, and logs.
- Produces an unmodified-reference capture under `.local/contracts/assisted-reference/<run-id>/`.
- Produces a collector-candidate capture under `.local/contracts/assisted-collector/<run-id>/`.
- Produces Python: `capture_reference_checkpoint(context, checkpoint: str) -> dict`, `decode_collector_checkpoint(data: bytes) -> dict`, `compare_calibration(reference, collector, schema) -> list[Difference]`, and `validate_capture_set(records) -> None`.

- [ ] **Step 1: Write failing command-construction and validation tests**

Use a fake proxy, fake assisted-runner context, and fake subprocess runner. Assert the script refuses a dirty or missing reference identity, never writes below `/Users/pavel/windows`, requires all four blocking checkpoints, records the m1n1/Mu SHA-256 values, and does not accept a collector capture unless it matches the host-side reference capture.

- [ ] **Step 2: Run the tests and verify RED**

Run: `python3 -m unittest tests.test_capture_assisted_contract -v`

Expected: capture module is missing.

- [ ] **Step 3: Implement the read-only reference probe**

The unchanged binary cannot emit a record format that did not exist when it was built. Therefore the first capture is intentionally host-side: add checkpoint callbacks around the established Python-assisted orchestration and read only already-exposed proxy state, symbols, boot arguments, stage-2 descriptors, and device configuration. Encode those observations into the canonical schema on the host. Do not patch, rebuild, or write into `/Users/pavel/windows`.

The reference probe must record provenance for every field: direct proxy read, assisted-runner value, ELF symbol plus offset, or digest of a bounded memory region. A field that cannot be observed is marked `unobserved`; it cannot be silently assigned an expected value and cannot become blocking until collector calibration supplies it.

- [ ] **Step 4: Implement the shared-collector candidate wrapper**

`capture-assisted-contract.sh` accepts:

```text
--reference-root /Users/pavel/windows
--public-root /Users/pavel/public_windows
--reference-output .local/contracts/assisted-reference/<run-id>
--collector-output .local/contracts/assisted-collector/<run-id>
--device /dev/cu.usbmodem...
```

The first mode invokes the established unmodified chainload and Mu flow through explicit paths and takes the host-side reference observations from Step 3. The second mode launches an isolated public-tree assisted candidate containing the shared C collector and retrieves its records through the proxy transport. It installs nothing and never modifies the reference root.

Before the collector candidate may become golden, require all of the following:

1. its normalized collector records match every observable reference field;
2. it reaches all eight `CPU_ENTRY` records;
3. Windows reaches the desktop and passes the five-minute liveness check;
4. its exact source, compiler, m1n1, and Mu hashes are recorded.

This calibration prevents the act of adding the collector from silently redefining the working launch state.

- [ ] **Step 5: Add both capture directories to `.gitignore` and dry-run the commands**

Run:

```bash
scripts/capture-assisted-contract.sh \
  --reference-root /Users/pavel/windows \
  --public-root /Users/pavel/public_windows \
  --reference-output .local/contracts/assisted-reference/dry-run \
  --collector-output .local/contracts/assisted-collector/dry-run \
  --device /dev/cu.usbmodemC02HDNCCQ6L41 \
  --dry-run
```

Expected: both exact command lines, runner paths, hashes, output files, and checkpoint order are printed; no hardware is touched.

- [ ] **Step 6: Run Python tests and commit**

```bash
python3 -m unittest tests.test_capture_assisted_contract -v
git add scripts/capture-assisted-contract.sh scripts/run-assisted.sh tools/capture_assisted_contract.py tests/test_capture_assisted_contract.py .gitignore
git commit -m "feat: capture assisted launch contracts"
```

### Task 6: Capture, Calibrate, and Sanitize the Golden J313 Contract

**Files:**
- Create locally: `.local/contracts/assisted-reference/<timestamp>/*`
- Create locally: `.local/contracts/assisted-collector/<timestamp>/*`
- Create: `config/contracts/j313-golden-v1.json`
- Create: `config/contracts/j313-schema-v1.json`
- Create: `tests/fixtures/contracts/j313-dynamic-address-variant.json`
- Modify: `tests/test_launch_contract.py`

**Interfaces:**
- Consumes: both calibrated capture modes from Task 5.
- Produces: reviewed public golden values from the shared C collector and explicit normalization schema version 1.

- [ ] **Step 1: Add a failing golden-fixture test**

The test loads the public schema and golden file, asserts all four blocking checkpoints exist, verifies no serial-number-shaped strings or `/Users/` paths are present, and compares the dynamic-address fixture successfully.

- [ ] **Step 2: Run the test and verify RED**

Run: `python3 -m unittest tests.test_launch_contract.GoldenContractTests -v`

Expected: golden and schema files are absent.

- [ ] **Step 3: Capture the unmodified assisted hardware reference**

Run the tested reference mode from Task 5 against the old assisted flow. Do not change or rebuild `/Users/pavel/windows`. Keep the machine at the Windows desktop for five minutes and record guest liveness separately from the pre-guest host observations. Preserve the complete command output and hashes in `.local/contracts/assisted-reference/<timestamp>/metadata.json`.

- [ ] **Step 4: Capture and calibrate the shared C collector in assisted mode**

Launch the isolated collector-enabled assisted candidate. Compare it with the unmodified reference using `compare_calibration`. Reject the run on any observable mismatch, missing checkpoint, CPU-entry omission, Windows failure to reach the desktop, or five-minute liveness failure. Store raw records and the calibration report under `.local/contracts/assisted-collector/<timestamp>/`.

- [ ] **Step 5: Generate and manually inspect the sanitized fixture**

Run:

```bash
python3 tools/launch_contract.py sanitize \
  .local/contracts/assisted-collector/<timestamp>/normalized.json \
  --schema config/contracts/j313-schema-v1.json \
  --output config/contracts/j313-golden-v1.json
```

Verify the calibration report has zero mismatches for all observable reference fields, the sanitization report says `removed_identifiers=0`, every dynamic field has an explicit rule, and no raw host path or USB serial remains. Fields that were `unobserved` in the unmodified reference become blocking only when they are architectural constants or are independently justified by a test and documentation.

- [ ] **Step 6: Run golden tests twice and commit**

Run: `python3 -m unittest tests.test_launch_contract -v`

Expected: all tests pass, including the permitted dynamic-address variant.

```bash
git add config/contracts/j313-golden-v1.json config/contracts/j313-schema-v1.json tests/fixtures/contracts/j313-dynamic-address-variant.json tests/test_launch_contract.py
git commit -m "test: record golden J313 launch contract"
```

### Task 7: Blocking Standalone Preflight and CPU Entry Audit

**Files:**
- Create: `m1n1_windows/src/hv_launch_preflight.h`
- Create: `m1n1_windows/src/hv_launch_preflight.c`
- Create: `m1n1_windows/tests/hv_launch_preflight_test.c`
- Modify: `m1n1_windows/src/hv_autonomous_runtime.c`
- Modify: `m1n1_windows/src/hv.c`
- Modify: `m1n1_windows/tests/hv_autonomous_stage_test.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Consumes: J313 schema, embedded golden contract, collector, comparator, and transport.
- Produces: `bool hv_launch_preflight_checkpoint(enum hv_contract_checkpoint checkpoint)`.
- Produces: `void hv_launch_audit_cpu_entry(unsigned int cpu, uint64_t mpidr)`.
- Produces: `const struct hv_contract_failure *hv_launch_preflight_failure(void)`.

- [ ] **Step 1: Write the blocking behavior test**

Inject snapshots where `PRE_HV_INIT`, `POST_HV_INIT`, and `POST_MAPS` match but `PRE_GUEST` has `cpu[1].actlr.EnMDSB=0`. Assert the guest-entry callback is not invoked and the stable failure identifies the field and checkpoint. Add a passing case where guest entry occurs exactly once.

- [ ] **Step 2: Run tests and verify RED**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh hv_launch_preflight_test hv_autonomous_stage_test`

Expected: preflight symbols and gate are missing.

- [ ] **Step 3: Insert checkpoints at existing stage boundaries**

Capture `PRE_HV_INIT` before `hv_init()`, `POST_HV_INIT` immediately after it, `POST_MAPS` after VUART/xHCI/PCI/NVMe setup, and `PRE_GUEST` immediately before the guest-entry call. Any false return prints the structured failure and returns `HV_AUTONOMOUS_RESULT_STAGE_FAILED` without calling `hv_start()`.

- [ ] **Step 4: Add post-launch CPU entry records**

At the first guest entry on each CPU, atomically set a one-shot bit and emit `CPU_ENTRY` with CPU index and MPIDR. Do not print repeatedly from timer/exception loops. Add host-test hooks that simulate CPU0–CPU7 and assert exactly eight unique records.

- [ ] **Step 5: Run all tests and commit**

Run: `cd m1n1_windows && ./tests/run_host_tests.sh`

Expected: all tests pass, including mismatch-blocking and eight-entry audit.

```bash
git add src/hv_launch_preflight.c src/hv_launch_preflight.h src/hv_autonomous_runtime.c src/hv.c tests/hv_launch_preflight_test.c tests/hv_autonomous_stage_test.c tests/run_host_tests.sh Makefile
git commit -m "feat: gate standalone launch with preflight"
```

### Task 8: Explicit Stage-0 and Stage-1 Build Products

**Files:**
- Modify: `m1n1_windows/Makefile`
- Modify: `scripts/build-standalone.sh`
- Modify: `tools/pack_boot.py`
- Create: `tests/test_pack_boot_stages.py`
- Modify: `documentation/BUILD.md`

**Interfaces:**
- Produces: `dist/j313/m1n1-stage0.bin`, `dist/j313/m1n1-stage1.bin`, and `dist/j313/boot.bin`.
- Records: compiler identity, source commit, binary SHA-256, and role for each stage in `dist/j313/BUILD-METADATA.json`.

- [ ] **Step 1: Write a failing packer test**

Pass identical byte strings as stage-0 and stage-1 and assert packing fails with `stage-0 and stage-1 identities must differ`. Pass distinct fixtures and assert both hashes and roles appear in metadata.

- [ ] **Step 2: Run the test and verify RED**

Run: `python3 -m unittest tests.test_pack_boot_stages -v`

Expected: current packer accepts the same m1n1 binary for both roles.

- [ ] **Step 3: Add role-specific m1n1 targets**

`make stage0` includes bootstrap detection/chainload and excludes hypervisor preflight. `make stage1` includes autonomous hypervisor/preflight and excludes bootstrap dispatch. Both use separate object directories so an incremental build cannot mix role flags.

- [ ] **Step 4: Pin the validated stage-1 compiler and metadata checks**

On macOS, stage-1 must report `Homebrew clang version 22.1.8` until a later compiler is hardware-qualified. The build aborts if the compiler differs unless an explicit development-only override is supplied. The release path never accepts the override.

- [ ] **Step 5: Run packer, host, and parser tests**

Run:

```bash
python3 -m unittest tests.test_pack_boot_stages tests.test_launch_contract -v
cd m1n1_windows && ./tests/run_host_tests.sh
```

Expected: all tests pass and metadata reports different stage hashes.

- [ ] **Step 6: Build without installing and compare offline**

Run:

```bash
scripts/build-standalone.sh --display physical --debug monitor
python3 tools/launch_contract.py inspect-image dist/j313/boot.bin \
  --golden config/contracts/j313-golden-v1.json
```

Expected: manifest validation passes, stage identities differ, embedded schema/golden versions equal `1`, and offline preflight data is complete.

- [ ] **Step 7: Commit**

```bash
git add m1n1_windows/Makefile scripts/build-standalone.sh tools/pack_boot.py tests/test_pack_boot_stages.py documentation/BUILD.md
git commit -m "fix: build distinct standalone stages"
```

### Task 9: Hardware Validation and Operational Documentation

**Files:**
- Modify: `scripts/log-standalone.sh`
- Modify: `tools/standalone_monitor.py`
- Create: `documentation/PREFLIGHT.md`
- Modify: `documentation/DEBUGGING.md`
- Modify: `documentation/RUN.md`
- Create locally: `.local/contracts/standalone/<timestamp>/*`

**Interfaces:**
- Consumes: final standalone image and golden contract.
- Produces: a monitor summary containing checkpoint results, eight CPU entries, and the last reliable event before any failure.

- [ ] **Step 1: Write failing monitor parser tests**

Feed interleaved console, VUART, reconnect, and retained-framebuffer events. Assert the monitor reports transport loss separately from guest failure and never interprets a static Windows logo as progress. Assert missing CPU entries list exact CPU indices.

- [ ] **Step 2: Run parser tests and verify RED**

Run: `python3 -m unittest tests.test_standalone_monitor -v`

Expected: contract checkpoint and CPU-entry summaries are not implemented.

- [ ] **Step 3: Implement monitor summaries and documentation**

The final summary contains:

```text
preflight: PASS
checkpoints: PRE_HV_INIT POST_HV_INIT POST_MAPS PRE_GUEST
cpu_entry: 0 1 2 3 4 5 6 7
transport: connected|lost-after-<checkpoint>
windows_progress: telemetry-only; framebuffer is informational
```

Document golden regeneration, offline comparison, the no-install gate, installation, rollback, log locations, and interpretation of blocking failures.

- [ ] **Step 4: Run the complete local verification suite**

Run:

```bash
cd m1n1_windows && ./tests/run_host_tests.sh
cd .. && python3 -m unittest discover -s tests -v
scripts/build-standalone.sh --display physical --debug monitor
python3 tools/launch_contract.py inspect-image dist/j313/boot.bin \
  --golden config/contracts/j313-golden-v1.json
```

Expected: all tests and offline checks pass before installation is offered.

- [ ] **Step 5: Perform one controlled hardware installation and boot**

Start `scripts/log-standalone.sh` before powering on the Air. Install only the image whose SHA-256 was validated offline. Success requires `PREFLIGHT PASS`, CPU entries `0 1 2 3 4 5 6 7`, no EL2 exception, and visible Windows progress beyond the static logo. Preserve the full capture under `.local/contracts/standalone/<timestamp>/`.

- [ ] **Step 6: Run a five-minute Windows stability check**

After desktop entry, verify clock progress, keyboard input, mouse input, NVMe I/O, and an RDP connect/disconnect cycle. A moving cursor alone is not sufficient. Record pass/fail in the local capture metadata.

- [ ] **Step 7: Commit documentation and monitor support**

```bash
git add scripts/log-standalone.sh tools/standalone_monitor.py documentation/PREFLIGHT.md documentation/DEBUGGING.md documentation/RUN.md tests/test_standalone_monitor.py
git commit -m "docs: add standalone preflight workflow"
```

## Final Verification Gate

- [ ] Confirm `git diff --check` is clean in the root repository and both submodules.
- [ ] Confirm no `.local` capture, USB serial, user path, password, or machine-specific identifier is tracked.
- [ ] Confirm every commit message contains no `Co-Authored-By` or session trailer.
- [ ] Confirm the public golden contract passes the dynamic-address variant and every negative fixture fails at the intended field.
- [ ] Confirm stage-0 and stage-1 hashes differ and their compiler/source identities are recorded.
- [ ] Confirm the hardware evidence contains all four blocking checkpoints, all eight CPU entries, and Windows desktop progress.
