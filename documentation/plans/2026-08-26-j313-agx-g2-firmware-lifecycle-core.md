# J313 AGX G2 Firmware Lifecycle Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a generated J313/V13_5 firmware-lifecycle contract, exact RTKit message codec and host-testable fail-closed lifecycle coordinator, then compile them into the ARM64 WDK package without making them reachable from `StartDevice`.

**Architecture:** The shared C layer contains no WDK, allocation or hardware code. It consumes explicit callbacks, computes finite absolute deadlines from a monotonic clock, records completed phases and rolls back only those phases in reverse order. The Windows project compiles the shared sources but `adapter.c` keeps the EXP-127 stage-7 `STATUS_NOT_SUPPORTED` boundary, so this plan performs no GPU MMIO, firmware boot, UAT publication, interrupt connection or Windows hardware experiment.

**Tech Stack:** C11 freestanding shared core, Python 3.11 contract generator and `unittest`, Clang ASan/UBSan host tests, ARM64 Windows WDK/MSBuild CI.

**Spec:** `documentation/design/2026-08-26-j313-agx-g2-direct-firmware-ownership.md`

## Global Constraints

- Target only J313, AGX generation `G13` and firmware version `V13_5`.
- Use 16 KiB GPU pages and a 40-bit GPU address limit.
- Windows KMD owns firmware, RTKit, UAT, queues, interrupts, faults and reset while started.
- m1n1 remains the stage-2/vGIC boundary and lifecycle-only bounded power broker.
- No USB, Python or synchronous EL2 dependency may be reachable from startup or steady-state progress.
- DCP scanout and the GOP framebuffer remain unchanged.
- Every wait uses a generated finite deadline and a monotonic clock; regression is failure.
- Rollback is reverse-ordered, phase-aware and idempotent.
- This plan authorizes offline code and tests only; hardware remains prohibited until a separate preregistered experiment passes a zero-Event-129 storage gate.
- Existing `m1n1_windows` and `mu` submodule dirt are not part of these commits.

## File Structure

- `config/j313-agx-g2.json`: human-reviewed J313 lifecycle values bound to the accepted G1R contract.
- `tools/generate_j313_agx_g2_contract.py`: exact schema validation and deterministic lifecycle macro generation.
- `drivers/apple-agx/shared/include/j313_agx_g2.generated.h`: generated lifecycle constants used by host and WDK code.
- `drivers/apple-agx/shared/include/apple_agx_rtkit.h`: freestanding RTKit management/firmware message interface.
- `drivers/apple-agx/shared/src/apple_agx_rtkit.c`: exact 64-bit message encoding and bounded decoding.
- `drivers/apple-agx/shared/tests/apple_agx_rtkit_test.c`: literal message-vector and rejection tests.
- `drivers/apple-agx/shared/include/apple_agx_firmware.h`: lifecycle phases, callback contract, receipts and public functions.
- `drivers/apple-agx/shared/src/apple_agx_firmware.c`: deadline validation, ordered startup and reverse idempotent rollback.
- `drivers/apple-agx/shared/tests/apple_agx_firmware_test.c`: fake transport, failure injection and exact trace assertions.
- `tests/test_apple_agx_firmware.py`: sanitizer-backed host compilation and execution.
- `drivers/apple-agx/windows/AppleAgx.vcxproj`: compile shared RTKit/lifecycle sources into both ARM64 configurations.
- `tests/test_apple_agx_windows_package.py`: prove WDK inclusion and continued hardware unreachability.
- `investigation/CHANGES.csv`: one row per implementation commit after its hash is known.

---

### Task 1: Generate the Immutable Firmware-Lifecycle Contract

**Files:**
- Modify: `config/j313-agx-g2.json`
- Modify: `tools/generate_j313_agx_g2_contract.py`
- Modify: `tests/test_j313_agx_g2_contract.py`
- Regenerate: `drivers/apple-agx/shared/include/j313_agx_g2.generated.h`

**Interfaces:**
- Consumes: accepted G1R firmware identity and `asc_mmio` range from `config/j313-agx.json`.
- Produces: `G2Contract.firmware_lifecycle` and `J313_AGX_G2_ASC_*`, endpoint, state and deadline macros.

- [x] **Step 1: Write failing schema and generation tests**

Add assertions to `test_reviewed_contract_is_bound_to_accepted_g1r_resources`:

```python
self.assertEqual(contract.firmware_lifecycle.management_endpoint, 0)
self.assertEqual(contract.firmware_lifecycle.firmware_endpoint, 0x20)
self.assertEqual(contract.firmware_lifecycle.doorbell_endpoint, 0x21)
self.assertEqual(contract.firmware_lifecycle.iop_boot_request_state, 0x220)
self.assertEqual(contract.firmware_lifecycle.running_state, 0x20)
self.assertEqual(contract.firmware_lifecycle.stopped_state, 0x10)
self.assertEqual(contract.firmware_lifecycle.asc_boot_timeout_ms, 3000)
self.assertEqual(contract.firmware_lifecycle.endpoint_timeout_ms, 500)
self.assertEqual(contract.firmware_lifecycle.initdata_timeout_ms, 500)
self.assertEqual(contract.firmware_lifecycle.heartbeat_timeout_ms, 500)
self.assertEqual(contract.firmware_lifecycle.stop_timeout_ms, 1000)
```

Add deterministic header assertions:

```python
for line in (
    "#define J313_AGX_G2_ASC_CPU_CONTROL_OFFSET 0x44u",
    "#define J313_AGX_G2_ASC_CPU_STATUS_OFFSET 0x48u",
    "#define J313_AGX_G2_ASC_INBOX_CTRL_OFFSET 0x8110u",
    "#define J313_AGX_G2_ASC_OUTBOX_CTRL_OFFSET 0x8114u",
    "#define J313_AGX_G2_ASC_INBOX0_OFFSET 0x8800u",
    "#define J313_AGX_G2_ASC_INBOX1_OFFSET 0x8808u",
    "#define J313_AGX_G2_ASC_OUTBOX0_OFFSET 0x8830u",
    "#define J313_AGX_G2_ASC_OUTBOX1_OFFSET 0x8838u",
    "#define J313_AGX_G2_FIRMWARE_ENDPOINT 0x20u",
    "#define J313_AGX_G2_DOORBELL_ENDPOINT 0x21u",
    "#define J313_AGX_G2_ASC_BOOT_TIMEOUT_MS 3000u",
):
    self.assertIn(line, rendered)
```

Add mutation cases that change each endpoint, state, offset or deadline and require `G2ContractError` containing the exact field name. Also require an unknown `firmware_lifecycle` key to fail the existing exact-key check.

- [x] **Step 2: Run the focused test and verify RED**

Run:

```bash
./proxyenv/bin/python -m unittest tests.test_j313_agx_g2_contract -v
```

Expected: FAIL because `G2Contract` has no `firmware_lifecycle` field and the generated macros do not exist.

- [x] **Step 3: Add the reviewed JSON object**

Add this exact top-level object to `config/j313-agx-g2.json`:

```json
"firmware_lifecycle": {
  "management_endpoint": 0,
  "firmware_endpoint": 32,
  "doorbell_endpoint": 33,
  "iop_boot_request_state": 544,
  "running_state": 32,
  "stopped_state": 16,
  "asc_cpu_control_offset": 68,
  "asc_cpu_status_offset": 72,
  "asc_inbox_control_offset": 33040,
  "asc_outbox_control_offset": 33044,
  "asc_inbox0_offset": 34816,
  "asc_inbox1_offset": 34824,
  "asc_outbox0_offset": 34864,
  "asc_outbox1_offset": 34872,
  "asc_boot_timeout_ms": 3000,
  "endpoint_timeout_ms": 500,
  "initdata_timeout_ms": 500,
  "heartbeat_timeout_ms": 500,
  "stop_timeout_ms": 1000
}
```

The register and message values mirror the pinned `m1n1/hw/asc.py`, `fw/asc/mgmt.py` and `fw/agx/__init__.py`. The 3000 ms boot deadline matches `StandardASC.start`; all new deadlines are finite and independently validated.

- [x] **Step 4: Implement exact parsing and rendering**

Add immutable `FirmwareLifecycle` and a field on `G2Contract`:

```python
@dataclass(frozen=True)
class FirmwareLifecycle:
    management_endpoint: int
    firmware_endpoint: int
    doorbell_endpoint: int
    iop_boot_request_state: int
    running_state: int
    stopped_state: int
    asc_cpu_control_offset: int
    asc_cpu_status_offset: int
    asc_inbox_control_offset: int
    asc_outbox_control_offset: int
    asc_inbox0_offset: int
    asc_inbox1_offset: int
    asc_outbox0_offset: int
    asc_outbox1_offset: int
    asc_boot_timeout_ms: int
    endpoint_timeout_ms: int
    initdata_timeout_ms: int
    heartbeat_timeout_ms: int
    stop_timeout_ms: int
```

Extend `TOP_KEYS`, define one exact-key set, validate every literal against the JSON above, and render unsigned C macros. Reject offsets outside `asc_mmio`, duplicate mailbox offsets, endpoint values outside `0..255`, equal firmware/doorbell endpoints and deadlines outside `1..5000`.

- [x] **Step 5: Regenerate and verify GREEN**

Run:

```bash
./proxyenv/bin/python tools/generate_j313_agx_g2_contract.py
./proxyenv/bin/python tools/generate_j313_agx_g2_contract.py --check
./proxyenv/bin/python -m unittest tests.test_j313_agx_g2_contract -v
git diff --check
```

Expected: deterministic check succeeds, all contract tests pass and only JSON, generator, test and generated header differ.

- [x] **Step 6: Commit**

```bash
git add config/j313-agx-g2.json tools/generate_j313_agx_g2_contract.py tests/test_j313_agx_g2_contract.py drivers/apple-agx/shared/include/j313_agx_g2.generated.h
git commit -m "gpu: generate AGX firmware lifecycle contract"
```

Record the resulting full hash as the commit field of the matching `investigation/CHANGES.csv` row.

---

### Task 2: Add the Exact RTKit Message Codec

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_rtkit.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_rtkit.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_rtkit_test.c`
- Create: `tests/test_apple_agx_firmware.py`

**Interfaces:**
- Consumes: generated endpoint/state macros from Task 1.
- Produces: `AppleAgxRtkitSetIopPower`, `AppleAgxRtkitSetApPower`, `AppleAgxRtkitStartEndpoint`, `AppleAgxRtkitInitdata`, `AppleAgxRtkitDecodeManagement` and `AppleAgxRtkitDecodeEndpoint`.

- [x] **Step 1: Write literal codec tests**

Create a C test with these exact vectors:

```c
assert(AppleAgxRtkitSetIopPower(0x220u) == 0x0060000000000220ULL);
assert(AppleAgxRtkitSetApPower(0x20u) == 0x00b0000000000020ULL);
assert(AppleAgxRtkitStartEndpoint(0x20u, 2u) == 0x0050002000000002ULL);
assert(AppleAgxRtkitStartEndpoint(0x21u, 2u) == 0x0050002100000002ULL);
assert(AppleAgxRtkitInitdata(0x00000abcde000ULL) ==
       0x00810000abcde000ULL);
assert(!AppleAgxRtkitDecodeManagement(0x00f0000000000000ULL, &decoded));
assert(!AppleAgxRtkitDecodeEndpoint(0x100ULL, &endpoint));
```

Also assert that a 44-bit overflow initdata address and endpoint/flag values above their bit widths return `APPLE_AGX_RTKIT_INVALID_MESSAGE` rather than truncating.

- [x] **Step 2: Add the sanitizer runner and verify RED**

In `tests/test_apple_agx_firmware.py`, compile with:

```python
command = [
    os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
    "-Werror", "-fsanitize=address,undefined",
    "-I", str(SHARED / "include"),
    str(SHARED / "tests" / "apple_agx_rtkit_test.c"),
    str(SHARED / "src" / "apple_agx_rtkit.c"),
    "-o", str(binary),
]
```

Run:

```bash
./proxyenv/bin/python -m unittest tests.test_apple_agx_firmware -v
```

Expected: FAIL because the header and codec do not exist.

- [x] **Step 3: Implement the freestanding codec**

Use only project-defined unsigned types; do not include hosted C headers. Encode management type in bits `59:52`, endpoint in `39:32`, start flag in `1:0`, firmware message type in `63:48`, initdata address in `43:0`, and endpoint selector in `7:0`.

Public signatures:

```c
APPLE_AGX_RTKIT_U64 AppleAgxRtkitSetIopPower(APPLE_AGX_RTKIT_U32 State);
APPLE_AGX_RTKIT_U64 AppleAgxRtkitSetApPower(APPLE_AGX_RTKIT_U32 State);
APPLE_AGX_RTKIT_U64 AppleAgxRtkitStartEndpoint(APPLE_AGX_RTKIT_U32 Endpoint,
                                               APPLE_AGX_RTKIT_U32 Flag);
APPLE_AGX_RTKIT_U64 AppleAgxRtkitInitdata(APPLE_AGX_RTKIT_U64 Address);
APPLE_AGX_RTKIT_BOOL AppleAgxRtkitDecodeManagement(
    APPLE_AGX_RTKIT_U64 Message, APPLE_AGX_RTKIT_MANAGEMENT *Decoded);
APPLE_AGX_RTKIT_BOOL AppleAgxRtkitDecodeEndpoint(
    APPLE_AGX_RTKIT_U64 Selector, APPLE_AGX_RTKIT_U32 *Endpoint);
```

The invalid-message sentinel is `~0ULL`; every encoder checks its input before shifting.

- [x] **Step 4: Run focused tests and verify GREEN**

Run:

```bash
./proxyenv/bin/python -m unittest tests.test_apple_agx_firmware -v
git diff --check
```

Expected: literal vectors and every rejection case pass under ASan/UBSan.

- [x] **Step 5: Commit**

```bash
git add drivers/apple-agx/shared/include/apple_agx_rtkit.h drivers/apple-agx/shared/src/apple_agx_rtkit.c drivers/apple-agx/shared/tests/apple_agx_rtkit_test.c tests/test_apple_agx_firmware.py
git commit -m "gpu: add bounded AGX RTKit message codec"
```

Record the full hash in `investigation/CHANGES.csv`.

---

### Task 3: Implement the Pure Firmware Lifecycle Coordinator

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_firmware.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_firmware.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_firmware_test.c`
- Modify: `tests/test_apple_agx_firmware.py`

**Interfaces:**
- Consumes: endpoint and deadline macros from Task 1; RTKit codec from Task 2 through transport callbacks.
- Produces: `APPLE_AGX_FIRMWARE`, `APPLE_AGX_FIRMWARE_IO`, `AppleAgxFirmwareInitialize`, `AppleAgxFirmwareStart`, `AppleAgxFirmwareRollback` and `AppleAgxFirmwareFail`.

- [x] **Step 1: Define the failing fake-transport suite**

The fake transport appends one byte per callback to `Trace[64]`:

```c
enum {
  TracePowerOn = 1, TraceCreateUat, TraceBootAsc,
  TraceStartFirmwareEndpoint, TraceStartDoorbellEndpoint,
  TracePublishInitdata, TraceSendInitdata, TraceDeviceControlInit,
  TraceUpdateIdleTimestamp, TraceHeartbeat,
  TraceUnpublishInitdata, TraceStopDoorbellEndpoint,
  TraceStopFirmwareEndpoint, TraceStopAsc, TraceDestroyUat, TracePowerOff,
};
```

Require successful startup to produce bytes `1..10`, set all ten completion bits and enter `AppleAgxFirmwareHeartbeatObserved`. Require rollback to append `11..16`, clear the mask and enter `AppleAgxFirmwareStopped`.

For each startup callback index `1..10`, inject one false return and assert rollback contains exactly the reverse cleanup operations corresponding to already completed bits. Call rollback twice and assert the second call adds no trace byte.

Deadline tests use a scripted `NowMs` callback:

```c
fake.Times[0] = 1000; /* phase start */
fake.Times[1] = 1000 + J313_AGX_G2_ASC_BOOT_TIMEOUT_MS; /* accepted */
fake.Times[2] = 2000;
fake.Times[3] = 2000 + J313_AGX_G2_ENDPOINT_TIMEOUT_MS + 1; /* rejected */
```

Also test addition overflow from `~0ULL - 2`, post-callback clock regression, unknown completion bits, null callbacks and cleanup failure. Cleanup failure must end in `AppleAgxFirmwareFailed` and preserve the bit for the resource that was not released.

- [x] **Step 2: Compile the new suite and verify RED**

Extend `tests/test_apple_agx_firmware.py` with a second sanitizer compilation containing `apple_agx_firmware_test.c`, `apple_agx_firmware.c` and `apple_agx_rtkit.c`.

Run:

```bash
./proxyenv/bin/python -m unittest tests.test_apple_agx_firmware -v
```

Expected: FAIL because the firmware coordinator files and symbols do not exist.

- [x] **Step 3: Add the exact callback contract**

Define ten startup operations through nine callback members and six cleanup operations through five callback members. Every operation receives an absolute deadline; endpoint callbacks also receive the generated endpoint value. The clock is called before and after every startup and cleanup operation.

Use these exact public state and result contracts:

```c
typedef enum _APPLE_AGX_FIRMWARE_PHASE {
  AppleAgxFirmwareOff = 0,
  AppleAgxFirmwarePowered,
  AppleAgxFirmwareUatReady,
  AppleAgxFirmwareAscRunning,
  AppleAgxFirmwareEndpointStarted,
  AppleAgxDoorbellEndpointStarted,
  AppleAgxFirmwareInitdataPublished,
  AppleAgxFirmwareInitdataSent,
  AppleAgxFirmwareDeviceControlInitialized,
  AppleAgxFirmwareIdleTimestampUpdated,
  AppleAgxFirmwareHeartbeatObserved,
  AppleAgxFirmwareRollingBack,
  AppleAgxFirmwareStopped,
  AppleAgxFirmwareFailed,
} APPLE_AGX_FIRMWARE_PHASE;

typedef enum _APPLE_AGX_FIRMWARE_RESULT {
  AppleAgxFirmwareResultOk = 0,
  AppleAgxFirmwareResultInvalid,
  AppleAgxFirmwareResultDeadlineOverflow,
  AppleAgxFirmwareResultTimeout,
  AppleAgxFirmwareResultClockRegression,
  AppleAgxFirmwareResultTransportFailed,
  AppleAgxFirmwareResultCleanupFailed,
} APPLE_AGX_FIRMWARE_RESULT;

typedef struct _APPLE_AGX_FIRMWARE {
  APPLE_AGX_FIRMWARE_PHASE Phase;
  APPLE_AGX_FW_U32 CompletedMask;
  APPLE_AGX_FW_U64 InitdataAddress;
  APPLE_AGX_FIRMWARE_RESULT LastResult;
} APPLE_AGX_FIRMWARE;
```

Define ten completion bits, one for every successful startup operation, from `APPLE_AGX_FIRMWARE_POWERED` at bit 0 through `APPLE_AGX_FIRMWARE_HEARTBEAT` at bit 9. The endpoint bits are distinct even though both use the same callback member.

```c
typedef struct _APPLE_AGX_FIRMWARE_IO {
  void *Context;
  APPLE_AGX_FW_U64 (*NowMs)(void *Context);
  APPLE_AGX_FW_BOOL (*PowerOn)(void *Context, APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*CreateFirmwareUat)(void *Context,
                                         APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*BootAsc)(void *Context, APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*StartEndpoint)(void *Context, APPLE_AGX_FW_U32 Endpoint,
                                     APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*PublishInitdata)(void *Context,
                                      APPLE_AGX_FW_U64 DeadlineMs,
                                      APPLE_AGX_FW_U64 *Address);
  APPLE_AGX_FW_BOOL (*SendInitdata)(void *Context, APPLE_AGX_FW_U64 Address,
                                    APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*SendDeviceControlInit)(void *Context,
                                             APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*UpdateIdleTimestamp)(void *Context,
                                           APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*ObserveHeartbeat)(void *Context,
                                        APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*UnpublishInitdata)(void *Context,
                                        APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*StopEndpoint)(void *Context, APPLE_AGX_FW_U32 Endpoint,
                                    APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*StopAsc)(void *Context,
                               APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*DestroyFirmwareUat)(void *Context,
                                          APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*PowerOff)(void *Context,
                                APPLE_AGX_FW_U64 DeadlineMs);
  void (*RecordPhase)(void *Context, APPLE_AGX_FIRMWARE_PHASE Phase,
                      APPLE_AGX_FIRMWARE_RESULT Result,
                      APPLE_AGX_FW_U32 CompletedMask);
} APPLE_AGX_FIRMWARE_IO;
```

Every operation callback and `NowMs` is required. `RecordPhase` is optional;
the coordinator skips it when null. Keep the generated power broker behind
`PowerOn`/`PowerOff`; the pure core does not know MMIO or EL2.

Public functions are exact and return the stored result rather than a lossy boolean:

```c
void AppleAgxFirmwareInitialize(APPLE_AGX_FIRMWARE *Firmware);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStart(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareRollback(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
void AppleAgxFirmwareFail(APPLE_AGX_FIRMWARE *Firmware,
                          APPLE_AGX_FIRMWARE_RESULT Result);
```

- [x] **Step 4: Implement ordered startup and rollback**

`AppleAgxFirmwareStart` must execute exactly:

```text
PowerOn -> CreateFirmwareUat -> BootAsc -> StartEndpoint(0x20) ->
StartEndpoint(0x21) -> PublishInitdata -> SendInitdata ->
SendDeviceControlInit -> UpdateIdleTimestamp -> ObserveHeartbeat
```

After a callback returns true, read `NowMs` again. Accept equality with the deadline; reject a later value or a value below the phase start. Set the completion bit only after this validation. Check `start + timeout` for unsigned overflow before calling the transport.

Use this exact timeout mapping:

| Operation | Generated timeout |
| --- | --- |
| `PowerOn`, `CreateFirmwareUat` | `J313_AGX_G2_INITDATA_TIMEOUT_MS` |
| `BootAsc` | `J313_AGX_G2_ASC_BOOT_TIMEOUT_MS` |
| both `StartEndpoint` calls | `J313_AGX_G2_ENDPOINT_TIMEOUT_MS` |
| `PublishInitdata`, `SendInitdata`, `SendDeviceControlInit`, `UpdateIdleTimestamp` | `J313_AGX_G2_INITDATA_TIMEOUT_MS` |
| `ObserveHeartbeat` | `J313_AGX_G2_HEARTBEAT_TIMEOUT_MS` |

`AppleAgxFirmwareRollback` must attempt cleanup in this exact order while continuing after failures:

```text
UnpublishInitdata -> StopEndpoint(0x21) -> StopEndpoint(0x20) ->
StopAsc -> DestroyFirmwareUat -> PowerOff
```

At rollback entry, clear the heartbeat, idle-timestamp, device-control and
initdata-sent observation bits because they own no separately releasable
resource. Clear every resource bit only after its cleanup callback succeeds.
Return `AppleAgxFirmwareResultCleanupFailed` and enter `Failed` if any bit
remains. A state with mask zero returns `AppleAgxFirmwareResultOk` without
touching the transport.

For each cleanup operation, compute a fresh absolute deadline using `J313_AGX_G2_STOP_TIMEOUT_MS`, validate addition overflow, and reject a post-callback time beyond the deadline or below the pre-callback time. Invoke `RecordPhase` after every phase/result change; it is diagnostic-only and its return value cannot affect progress.

- [x] **Step 5: Run lifecycle and existing state tests**

Run:

```bash
./proxyenv/bin/python -m unittest tests.test_apple_agx_firmware tests.test_apple_agx_state tests.test_apple_agx_power -v
git diff --check
```

Expected: all sanitizer-backed suites pass; existing queue/fence state and power broker behavior remain unchanged.

- [x] **Step 6: Commit**

```bash
git add drivers/apple-agx/shared/include/apple_agx_firmware.h drivers/apple-agx/shared/src/apple_agx_firmware.c drivers/apple-agx/shared/tests/apple_agx_firmware_test.c tests/test_apple_agx_firmware.py
git commit -m "gpu: add fail-closed AGX firmware lifecycle core"
```

Record the full hash in `investigation/CHANGES.csv`.

---

### Task 4: Compile the Shared Core into WDK Without Enabling Hardware

**Files:**
- Modify: `drivers/apple-agx/windows/AppleAgx.vcxproj`
- Modify: `tests/test_apple_agx_windows_package.py`

**Interfaces:**
- Consumes: Task 2 and Task 3 shared headers and sources.
- Produces: an ARM64 driver package containing the offline-verified core but no call site from any DDI.

- [x] **Step 1: Write failing WDK reachability tests**

Add assertions:

```python
for source in (
    "..\\shared\\src\\apple_agx_rtkit.c",
    "..\\shared\\src\\apple_agx_firmware.c",
):
    self.assertIn(source, project)
for header in (
    "..\\shared\\include\\apple_agx_rtkit.h",
    "..\\shared\\include\\apple_agx_firmware.h",
):
    self.assertIn(header, project)

adapter = self.read("src/adapter.c")
driver = self.read("src/driver.c")
self.assertNotIn("AppleAgxFirmwareStart", adapter)
self.assertNotIn("AppleAgxFirmwareStart", driver)
self.assertNotIn("AppleAgxFirmwareRollback", adapter)
self.assertIn("return STATUS_NOT_SUPPORTED", adapter)
```

Retain the existing audit that forbids SGX mapping/writes outside the bounded power file.

- [x] **Step 2: Run the package test and verify RED**

Run:

```bash
./proxyenv/bin/python -m unittest tests.test_apple_agx_windows_package -v
```

Expected: FAIL because the project does not list the new shared files.

- [x] **Step 3: Add only compile items**

Add the two sources to `<ClCompile>` and the two headers to `<ClInclude>`. Do not modify `adapter.c`, `driver.c`, the INF, feature flags, callbacks or package scripts.

- [x] **Step 4: Verify focused and canonical offline gates**

Run:

```bash
./proxyenv/bin/python -m unittest \
  tests.test_apple_agx_windows_package \
  tests.test_apple_agx_firmware \
  tests.test_j313_agx_g2_contract -v
./proxyenv/bin/python -m unittest discover -s tests
./proxyenv/bin/python tools/generate_j313_agx_g2_contract.py --check
git diff --check
```

Expected: all focused tests and the full public suite pass; generator reports no stale output; `adapter.c` remains byte-identical.

- [x] **Step 5: Commit**

```bash
git add drivers/apple-agx/windows/AppleAgx.vcxproj tests/test_apple_agx_windows_package.py
git commit -m "gpu: compile firmware lifecycle core in ARM64 package"
```

Record the full hash in `investigation/CHANGES.csv`.

---

### Task 5: Verify WDK CI and Close the Offline Milestone

**Files:**
- Modify: `investigation/CHANGES.csv`
- Modify: `documentation/plans/2026-08-26-j313-agx-g2-firmware-lifecycle-core.md`

**Interfaces:**
- Consumes: Tasks 1 through 4 and their exact commit hashes.
- Produces: a pushed, reproducible offline milestone and an explicit boundary for the next Windows transport/initdata plan.

- [x] **Step 1: Push the implementation commits and observe ARM64 WDK CI**

Run:

```bash
git push origin feature/j313-gpu-acceleration
run_id=$(gh run list --workflow apple-agx-wdk.yml --branch feature/j313-gpu-acceleration --limit 1 --json databaseId --jq '.[0].databaseId')
test -n "$run_id"
gh run watch "$run_id" --exit-status
```

Expected: both normal and qualification ARM64 build jobs pass code analysis, INF validation and signature packaging. Store the actual run ID in the plan result section when closing the task.

- [x] **Step 2: Add one CSV row per implementation commit**

Each row must contain the full commit hash, exact changed interface, reason, pre-change reproduction, test commands, WDK run result, artifact path/hash and this hardware result:

```text
No Windows package was installed and no GPU hardware action occurred; EXP-123 recovery remained unchanged.
```

- [x] **Step 3: Run the final verification set**

Run:

```bash
./proxyenv/bin/python -m unittest \
  tests.test_repository_hygiene tests.test_change_ledger \
  tests.test_j313_agx_g2_contract tests.test_apple_agx_firmware \
  tests.test_apple_agx_state tests.test_apple_agx_power \
  tests.test_apple_agx_windows_package -v
./proxyenv/bin/python -m unittest discover -s tests
git diff --check
git status --short --branch
```

Expected: all tests pass, only the known pre-existing `m1n1_windows` and `mu` submodule dirt is reported, and the branch has no unpushed root-repository change.

- [x] **Step 4: Commit and push the milestone record**

```bash
git add investigation/CHANGES.csv documentation/plans/2026-08-26-j313-agx-g2-firmware-lifecycle-core.md
git commit -m "docs: close AGX firmware lifecycle core milestone"
git push origin feature/j313-gpu-acceleration
```

- [x] **Step 5: State the next explicit boundary**

The next plan may add the Windows SGX/ASC mapping and versioned initdata builder behind a new opt-in compile flag. It must begin with source-first structure provenance, failing offline tests and a separate design review. It may not install a driver, boot firmware or connect an interrupt until a new experiment is preregistered against exact EXP-123 recovery and requires zero Event 129.

## Result

Completed on 2026-08-26 as an offline-only milestone.

- Generated lifecycle contract: `7a0e8f29b0eb8388534a6492fb53e313995fa4c4`.
- Bounded RTKit codec: `1c85c657e3d94b9ddab226da17aedb92a4b63c11`.
- Fail-closed lifecycle coordinator: `5411e4b1f9d8c55053665fbd5a6c3041b5fe298a`.
- Compile-only ARM64 WDK integration: `7e9ece504621ce5b707688ed35f023b7ebbc4eec`.
- Focused final gate: 48/48 tests passed.
- Complete public regression gate: 670/670 tests passed.
- Deterministic generated-contract check and `git diff --check` passed.
- GitHub Actions run `33017120072` passed both the default and power-qualification ARM64 WDK jobs, including code analysis, package signing checks and artifact upload.
- No driver package was installed, no GPU firmware was started, no interrupt was connected and no guest or hardware state changed. The active recovery remains EXP-123.

The next authorized design unit is the Windows SGX/ASC mapping plus a versioned initdata builder behind an opt-in compile flag. Hardware execution remains prohibited until that work receives its own design, preregistered experiment and zero-Event-129 storage gate.
