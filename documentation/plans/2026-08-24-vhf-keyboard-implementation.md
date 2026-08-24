# Native Apple Keyboard VHF Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish the J313 built-in Apple keyboard as a native Windows keyboard through VHF while preserving the hardware-validated Apple SPI HID transport and transport-only rollback.

**Architecture:** The portable protocol layer gains bounded descriptor ownership and a HID input-report size contract parser. The KMDF driver copies discovered descriptors before reassembly-buffer reuse, exposes only descriptor metadata in diagnostics, and creates one keyboard VHF device after discovery reaches `READY`; trackpad messages remain counted but unpublished until the separate Precision Touchpad evidence gate and plan.

**Tech Stack:** Portable C11, KMDF 1.33, Virtual HID Framework (`vhf.h`/`VhfKm.lib`), Windows 11 ARM64 WDK, Python `unittest`, GitHub Actions, test signing, J313 assisted launch diagnostics.

**Spec:** `documentation/design/2026-08-24-vhf-keyboard-precision-touchpad.md`

## Global Constraints

- Work only in `/Users/pavel/public_windows` on `feature/j313-native-input`; do not use `/Users/pavel/windows` or a worktree.
- Do not change m1n1, Mu, ACPI, CPU topology, NVMe, display, xHCI or the stable ESP in this phase.
- Keep the existing physical IRQ 330 to guest INTID 865 level route and EXP-057 firmware pair unchanged.
- Keep external USB keyboard and mouse connected as the recovery path.
- Keep `TransportOnly=1` as the package default; publication requires an explicit development install option.
- Never expose key reports, raw descriptors, coordinates or arbitrary kernel memory in normal diagnostics.
- Change one observable variable per hardware run and record every run before and after in `investigation/EXPERIMENTS.md`.
- Add a regression test before each defect fix or feature implementation.
- After each implementation commit, append its full 40-character hash to `investigation/CHANGES.csv` in a separate ledger commit.
- Do not add assistant attribution, session URLs or `Co-Authored-By` trailers.
- Do not call the keyboard working or stable until the corresponding hardware experiment is recorded as validated.

## Source Contract

Read these sources before implementation and cite them in the first change-ledger row:

- `drivers/apple-input/protocol/include/apple_spihid.h`
- `drivers/apple-input/protocol/src/apple_spihid_discovery.c`
- `drivers/apple-input/protocol/src/apple_spihid_packet.c`
- `drivers/apple-input/protocol/src/apple_spihid_reassembly.c`
- `drivers/apple-input/protocol/tests/apple_spihid_test.c`
- `drivers/apple-input/windows/include/apple_input_device.h`
- `drivers/apple-input/windows/include/apple_input_ioctl.h`
- `drivers/apple-input/windows/src/device.c`
- `drivers/apple-input/windows/src/transport.c`
- `drivers/apple-input/windows/src/diagnostics.c`
- `drivers/apple-input/windows/AppleInput.inf`
- `drivers/apple-input/windows/AppleInput.vcxproj`
- Asahi `drivers/hid/spi-hid/spi-hid-apple-core.c` and its current J313 device-tree data.
- Microsoft VHF documentation for `VHF_CONFIG`, `VhfCreate`, `VhfStart`, `VhfReadReportSubmit` and synchronous `VhfDelete`.

The hardware-validated starting point is EXP-20260824-046: phase `READY`, IRQ 84,
workers 2/2, SPI 21, reset 1, and zero timeout/CRC/fragment/offline failures.
The observed descriptor payload lengths are 182 bytes for device 1 (keyboard)
and 110 bytes for device 2 (trackpad). Lengths are observations, not hard-coded
acceptance constants.

## File Structure

- Create `drivers/apple-input/protocol/src/apple_hid_contract.c`: bounded HID short-item parser and input-report length validation.
- Create `drivers/apple-input/protocol/src/apple_spihid_descriptors.c`: fixed-storage descriptor ownership for devices 1 and 2.
- Modify `drivers/apple-input/protocol/include/apple_spihid.h`: portable descriptor and input-contract types and functions.
- Modify `drivers/apple-input/protocol/tests/apple_spihid_test.c`: descriptor lifetime, malformed descriptor and report-size tests.
- Create `drivers/apple-input/windows/src/vhf_keyboard.c`: low-level keyboard VHF object creation, submission and deletion.
- Create `drivers/apple-input/windows/src/vhf_frontend.c`: `VhfFrontendManager` state machine, publication gate and keyboard/trackpad frontend boundary.
- Modify `drivers/apple-input/windows/include/apple_input_device.h`: descriptor store, keyboard contract, VHF handle, wait lock and lifecycle state.
- Modify `drivers/apple-input/windows/src/device.c`: device handle, wait lock, service-parameter gate and cleanup ordering.
- Modify `drivers/apple-input/windows/src/transport.c`: descriptor capture, `READY` publication and keyboard report dispatch.
- Modify `drivers/apple-input/windows/include/apple_input_ioctl.h`: version-3 bounded diagnostic metadata.
- Modify `drivers/apple-input/windows/src/diagnostics.c`: publish descriptor/VHF scalar metadata.
- Modify `drivers/apple-input/windows/tools/AppleInputDiag/main.c`: print version-3 metadata without payloads.
- Modify `drivers/apple-input/windows/AppleInput.vcxproj`: compile the new portable and VHF sources.
- Modify `drivers/apple-input/windows/scripts/install-driver.ps1`: explicit `-PublishKeyboard` service-parameter switch.
- Modify `tests/test_apple_input_windows_package.py`: source, package, privacy and lifecycle contracts.
- Modify `documentation/APPLE_INPUT.md`: exact hardware evidence and rollback commands after validation.
- Modify `investigation/EXPERIMENTS.md` and `investigation/CHANGES.csv` according to `AGENTS.md`.

The specification's component names map to code as follows:

- `DescriptorStore` is `struct ai_descriptor_store` in the portable protocol layer;
- `AppleKeyboardVhf` is implemented by `vhf_keyboard.c`;
- `VhfFrontendManager` is implemented by `vhf_frontend.c` and is the only
  component called directly by transport and PnP lifecycle code;
- `AppleTrackpadParser` and `ApplePrecisionTouchpadVhf` are reserved for the
  separate post-Gate-D1 Precision Touchpad plan.

---

### Task 1: Own discovered descriptors independently of reassembly

**Files:**
- Modify: `drivers/apple-input/protocol/include/apple_spihid.h`
- Create: `drivers/apple-input/protocol/src/apple_spihid_descriptors.c`
- Modify: `drivers/apple-input/protocol/tests/apple_spihid_test.c`
- Modify: `drivers/apple-input/windows/AppleInput.vcxproj`
- Test: `tests/test_apple_input_windows_package.py`

**Interfaces:**
- Produces: `struct ai_descriptor_slot { bool valid; uint8_t device; uint16_t length; uint8_t bytes[AI_DESCRIPTOR_MAX]; }`.
- Produces: `struct ai_descriptor_store { struct ai_descriptor_slot keyboard; struct ai_descriptor_slot trackpad; }`.
- Produces: `void ai_descriptor_store_reset(struct ai_descriptor_store *store)`.
- Produces: `enum ai_status ai_descriptor_store_put(struct ai_descriptor_store *store, uint8_t device, const uint8_t *bytes, size_t length)`.
- Produces: `const struct ai_descriptor_slot *ai_descriptor_store_get(const struct ai_descriptor_store *store, uint8_t device)`.

- [ ] **Step 1: Add failing portable ownership tests**

Add this test shape to `apple_spihid_test.c` and call it from `main`:

```c
static void test_descriptor_store_owns_bytes(void)
{
    struct ai_descriptor_store store;
    uint8_t source[] = {0x05, 0x01, 0x09, 0x06};
    const struct ai_descriptor_slot *slot;

    ai_descriptor_store_reset(&store);
    assert(ai_descriptor_store_put(&store, 1, source, sizeof(source)) == AI_OK);
    source[0] = 0xff;
    slot = ai_descriptor_store_get(&store, 1);
    assert(slot && slot->valid && slot->device == 1);
    assert(slot->length == 4 && slot->bytes[0] == 0x05);
    assert(ai_descriptor_store_get(&store, 2) == NULL);
}
```

Also assert rejection of device 0/3, null pointers, zero length and
`AI_DESCRIPTOR_MAX + 1`, and assert reset clears both valid flags.

- [ ] **Step 2: Run the portable test and verify RED**

Run:

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -Idrivers/apple-input/protocol/include \
  drivers/apple-input/protocol/tests/apple_spihid_test.c \
  drivers/apple-input/protocol/src/apple_spi_plan.c \
  drivers/apple-input/protocol/src/apple_spihid_crc.c \
  drivers/apple-input/protocol/src/apple_spihid_discovery.c \
  drivers/apple-input/protocol/src/apple_spihid_packet.c \
  drivers/apple-input/protocol/src/apple_spihid_reassembly.c \
  drivers/apple-input/protocol/src/apple_spihid_transport.c \
  -o /tmp/apple_spihid_test && /tmp/apple_spihid_test
```

Expected: compile failure for missing descriptor-store types/functions.

- [ ] **Step 3: Implement fixed descriptor ownership**

Implement the exact interfaces above. Select the destination only for device 1
or 2, validate arguments before modifying the destination, zero the full slot,
copy the bytes with `AI_MEMCPY`, then set metadata and `valid=true` last.

```c
if (!store || !bytes || length == 0 || length > AI_DESCRIPTOR_MAX)
    return AI_ERR_ARGUMENT;
slot = device == 1 ? &store->keyboard :
       device == 2 ? &store->trackpad : NULL;
if (!slot)
    return AI_ERR_PROTOCOL;
AI_MEMSET(slot, sizeof(*slot));
AI_MEMCPY(slot->bytes, bytes, length);
slot->device = device;
slot->length = (uint16_t)length;
slot->valid = true;
```

- [ ] **Step 4: Add the source to both host and Windows builds**

Add `apple_spihid_descriptors.c` to the explicit portable test command where
used and to `AppleInput.vcxproj`. Extend the Python package test to require that
source and reject dynamic allocation symbols in it.

- [ ] **Step 5: Run focused and full software tests**

Run:

```bash
/tmp/apple_spihid_test
proxyenv/bin/python -m unittest tests.test_apple_input_windows_package -v
proxyenv/bin/python -m unittest discover -s tests -v
```

Expected: all pass; no hardware or installed driver changes.

- [ ] **Step 6: Commit implementation and ledger row**

```bash
git add drivers/apple-input tests/test_apple_input_windows_package.py
git commit -m "input: retain discovered HID descriptors"
```

Append a `status=implemented` CHANGES row using the resulting full commit hash,
then commit only the ledger:

```bash
git add investigation/CHANGES.csv
git commit -m "docs: index descriptor ownership change"
```

### Task 2: Derive exact keyboard input-report sizes from its HID descriptor

**Files:**
- Modify: `drivers/apple-input/protocol/include/apple_spihid.h`
- Create: `drivers/apple-input/protocol/src/apple_hid_contract.c`
- Modify: `drivers/apple-input/protocol/tests/apple_spihid_test.c`
- Modify: `drivers/apple-input/windows/AppleInput.vcxproj`
- Test: `tests/test_apple_input_windows_package.py`

**Interfaces:**
- Produces: `#define AI_HID_REPORT_ID_CAPACITY 256u`.
- Produces: `struct ai_hid_input_contract { bool valid; bool uses_report_ids; uint16_t bytes_by_id[AI_HID_REPORT_ID_CAPACITY]; }`.
- Produces: `enum ai_status ai_hid_input_contract_parse(const uint8_t *descriptor, size_t length, struct ai_hid_input_contract *out)`.
- Produces: `bool ai_hid_input_report_valid(const struct ai_hid_input_contract *contract, const uint8_t *report, size_t length, uint8_t *report_id)`.

- [ ] **Step 1: Add failing HID short-item parser tests**

Use a synthetic boot-keyboard descriptor so no private hardware payload enters
the repository:

```c
static const uint8_t keyboard_descriptor[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
    0x85, 0x01, 0x75, 0x08, 0x95, 0x08, 0x81, 0x02,
    0xc0,
};
struct ai_hid_input_contract contract;
uint8_t good[9] = {1};
uint8_t id = 0;
assert(ai_hid_input_contract_parse(keyboard_descriptor,
                                   sizeof(keyboard_descriptor),
                                   &contract) == AI_OK);
assert(contract.uses_report_ids && contract.bytes_by_id[1] == 9);
assert(ai_hid_input_report_valid(&contract, good, sizeof(good), &id));
assert(id == 1);
assert(!ai_hid_input_report_valid(&contract, good, 8, &id));
```

Add vectors for no Report ID, multiple Input items contributing bits to one ID,
round-up from bits to bytes, global Push/Pop, truncated short items, forbidden
long item `0xfe`, Report ID zero, zero Report Size/Count at Input, stack
underflow/overflow, bit-count overflow and a descriptor with no Input item.

- [ ] **Step 2: Run the portable test and verify RED**

Run the Task 1 compile command with `apple_spihid_descriptors.c` included.
Expected: compile failure for missing HID-contract interfaces.

- [ ] **Step 3: Implement a bounded HID short-item parser**

Parse HID item prefix size/type/tag, accepting only bounded short items. Track
global `Report Size`, `Report Count` and `Report ID` with a four-entry Push/Pop
stack. On each Input main item, add `size * count` bits to that report ID with
checked arithmetic. Ignore unrelated local/global/main items only after their
encoded length has been bounds checked.

At completion, reject an unbalanced stack or zero input reports. Convert bit
counts to byte counts and add the leading Report ID byte only when report IDs
are used. Set `out->valid=true` last.

- [ ] **Step 4: Implement exact input-report validation**

```c
if (!contract || !contract->valid || !report || length == 0)
    return false;
id = contract->uses_report_ids ? report[0] : 0;
if (contract->bytes_by_id[id] == 0 ||
    contract->bytes_by_id[id] != length)
    return false;
if (report_id)
    *report_id = id;
return true;
```

- [ ] **Step 5: Add build contract and run all tests**

Require `apple_hid_contract.c` in `AppleInput.vcxproj` and the package test.
Run the focused portable test, package test and complete Python suite. Expected:
all pass with no change to the Air.

- [ ] **Step 6: Commit implementation and ledger row**

```bash
git add drivers/apple-input tests/test_apple_input_windows_package.py
git commit -m "input: validate HID input report contracts"
```

Append the full commit hash to CHANGES with `status=implemented`, then commit the
ledger as `docs: index HID report contract parser`.

### Task 3: Capture descriptors at the discovery boundary and expose metadata

**Files:**
- Modify: `drivers/apple-input/windows/include/apple_input_device.h`
- Modify: `drivers/apple-input/windows/include/apple_input_ioctl.h`
- Modify: `drivers/apple-input/windows/src/transport.c`
- Modify: `drivers/apple-input/windows/src/diagnostics.c`
- Modify: `drivers/apple-input/windows/tools/AppleInputDiag/main.c`
- Modify: `tests/test_apple_input_windows_package.py`

**Interfaces:**
- Context fields: `struct ai_descriptor_store Descriptors; struct ai_hid_input_contract KeyboardInputContract;`.
- Produces: `NTSTATUS AiCaptureDiscoveryDescriptor(PAI_DEVICE_CONTEXT context, enum ai_discovery_phase phase, const struct ai_protocol_message *message)`.
- Produces diagnostic version 3 with keyboard/trackpad descriptor length, 32-byte SHA-256 digest, keyboard-contract validity and no payload bytes.

- [ ] **Step 1: Add failing package/privacy tests**

Require descriptor capture before `ai_discovery_accept`, and assert the helper
copies only phases `AI_DISCOVERY_KEYBOARD_DESCRIPTOR` and
`AI_DISCOVERY_TRACKPAD_DESCRIPTOR`. Require snapshot version 3 fields:

```python
for field in (
    "KeyboardDescriptorLength", "TrackpadDescriptorLength",
    "KeyboardDescriptorSha256", "TrackpadDescriptorSha256",
    "KeyboardContractValid",
):
    self.assertIn(field, ioctl)
self.assertNotRegex(ioctl, r"(?i)(payload|rawreport|descriptorbytes)\\s*\\[")
```

Require the CLI to emit lengths/digests but never raw descriptor/report data.

- [ ] **Step 2: Run focused tests and verify RED**

```bash
proxyenv/bin/python -m unittest tests.test_apple_input_windows_package -v
```

Expected: failures for missing capture helper and version-3 fields.

- [ ] **Step 3: Capture descriptors before advancing discovery**

In `AiTransportProcessPacket`, save `phase = Context->Discovery.phase`, validate
the response structurally, and call `AiCaptureDiscoveryDescriptor` before
`ai_discovery_accept`. The helper must:

1. select device 1 only for keyboard-descriptor phase and device 2 only for
   trackpad-descriptor phase;
2. call `ai_descriptor_store_put` using `message->payload` and
   `message->payload_length`;
3. immediately parse the stored keyboard bytes into `KeyboardInputContract`;
4. fail discovery on any copy or keyboard-contract parse error.

Reset both structures in `AiTransportStart` before releasing the controller
reset.

- [ ] **Step 4: Add SHA-256 descriptor metadata at passive level**

Use the Windows CNG SHA-256 provider at the passive discovery boundary and add
`Cng.lib` to the project. Hash only the stored bounded descriptor. On hashing
failure, leave the digest zero, expose a `DescriptorDigestStatus` scalar and do
not expose raw bytes; hashing failure does not invalidate an otherwise valid
transport descriptor.

- [ ] **Step 5: Upgrade diagnostics and CLI to version 3**

Append fields to the versioned structure, update `sizeof` validation and JSON
output, and retain the existing version-2 header fields. Print each digest as
exactly 64 lowercase hexadecimal characters.

- [ ] **Step 6: Run portable, package, privacy and full tests**

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -Idrivers/apple-input/protocol/include \
  drivers/apple-input/protocol/tests/apple_spihid_test.c \
  drivers/apple-input/protocol/src/*.c \
  -o /tmp/apple_spihid_test && /tmp/apple_spihid_test
proxyenv/bin/python -m unittest tests.test_apple_input_windows_package -v
proxyenv/bin/python -m unittest discover -s tests -v
```

Expected: all pass; snapshot source contains no descriptor/report payload array.

- [ ] **Step 7: Commit implementation and ledger row**

Commit as `input: retain validated keyboard descriptor metadata`, then append
the full hash to a `status=implemented` CHANGES row and commit the ledger.

### Task 4: Create the keyboard VHF frontend behind an explicit gate

**Files:**
- Create: `drivers/apple-input/windows/src/vhf_keyboard.c`
- Create: `drivers/apple-input/windows/src/vhf_frontend.c`
- Modify: `drivers/apple-input/windows/include/apple_input_device.h`
- Modify: `drivers/apple-input/windows/src/device.c`
- Modify: `drivers/apple-input/windows/AppleInput.vcxproj`
- Modify: `drivers/apple-input/windows/AppleInput.inf`
- Modify: `drivers/apple-input/windows/scripts/install-driver.ps1`
- Modify: `tests/test_apple_input_windows_package.py`

**Interfaces:**
- Defines `enum AI_VHF_STATE { AiVhfAbsent, AiVhfDescriptorsReady, AiVhfStarting, AiVhfRunning, AiVhfStopping }`.
- Context fields: `WDFDEVICE Device; WDFWAITLOCK FrontendLock; VHFHANDLE KeyboardVhf; enum AI_VHF_STATE KeyboardVhfState; BOOLEAN TransportOnly;`.
- Produces: `NTSTATUS AiKeyboardVhfStart(PAI_DEVICE_CONTEXT context)`.
- Produces: `NTSTATUS AiKeyboardVhfSubmit(PAI_DEVICE_CONTEXT context, const UCHAR *report, SIZE_T length)`.
- Produces: `VOID AiKeyboardVhfStop(PAI_DEVICE_CONTEXT context)`.
- Produces: `NTSTATUS AiVhfFrontendStart(PAI_DEVICE_CONTEXT context)`.
- Produces: `NTSTATUS AiVhfFrontendSubmitKeyboard(PAI_DEVICE_CONTEXT context, const UCHAR *report, SIZE_T length)`.
- Produces: `VOID AiVhfFrontendStop(PAI_DEVICE_CONTEXT context)`.

- [ ] **Step 1: Add failing VHF source-contract tests**

Require `vhf_keyboard.c` and `vhf_frontend.c` in the project and assert:

```python
for symbol in ("VHF_CONFIG_INIT", "VhfCreate", "VhfStart",
               "VhfReadReportSubmit", "VhfDelete"):
    self.assertIn(symbol, vhf)
self.assertIn("WdfDeviceWdmGetDeviceObject", vhf)
self.assertIn("VhfDelete(handle, TRUE)", vhf)
self.assertNotIn("EvtVhfReadyForNextReadReport", vhf)
```

Require `TransportOnly=1` in the INF and an explicit `-PublishKeyboard` switch
in the installer. Require `WdfDriverOpenParametersRegistryKey` and reject any
default value that enables publication.

- [ ] **Step 2: Run focused tests and verify RED**

Run the package test. Expected: failure because the keyboard object, frontend
manager and publication gate do not exist.

- [ ] **Step 3: Create context lifetime and service-parameter gate**

Create `FrontendLock` during `AppleInputCreateDevice`, store `Device`, and read
`HKLM\\System\\CurrentControlSet\\Services\\AppleInput\\Parameters\\TransportOnly`
through `WdfDriverOpenParametersRegistryKey` and `WdfRegistryQueryULong`.
Missing, invalid or nonzero values mean `TRUE`; only exact DWORD zero means
publication is permitted.

The install script's `-PublishKeyboard` path sets the service parameter to zero
after package install and restarts only `ACPI\\APPL0001`; its default path sets
the value to one. The uninstall path restores one before removing the package.

`vhf_frontend.c` owns the `Absent -> DescriptorsReady -> Starting -> Running ->
Stopping -> Absent` transition and calls the keyboard object only after the
descriptor and report contract are valid. A later Precision Touchpad frontend
will be added behind this manager without changing transport call sites.

- [ ] **Step 4: Implement VHF create/start with default buffering**

At `PASSIVE_LEVEL`, under `FrontendLock`, require a valid stored keyboard
descriptor and parsed input contract. Initialize VHF with the exact owned
descriptor:

```c
VHF_CONFIG_INIT(&config,
    WdfDeviceWdmGetDeviceObject(context->Device),
    context->Descriptors.keyboard.length,
    context->Descriptors.keyboard.bytes);
config.VhfClientContext = context;
status = VhfCreate(&config, &context->KeyboardVhf);
if (NT_SUCCESS(status))
    status = VhfStart(context->KeyboardVhf);
```

Do not register `EvtVhfReadyForNextReadReport`; this deliberately uses VHF's
default buffering, allowing the worker-owned report buffer to be reused after
`VhfReadReportSubmit` returns.

- [ ] **Step 5: Implement exact report submission**

Validate via `ai_hid_input_report_valid`, fill one stack
`HID_XFER_PACKET` whose buffer covers the complete report, set `reportId` to the
validated ID (zero when no IDs are used), and call `VhfReadReportSubmit` only in
`AiVhfRunning`. Return `STATUS_INVALID_BUFFER_SIZE` for invalid report shape and
`STATUS_DEVICE_NOT_READY` when not running.

- [ ] **Step 6: Implement synchronous idempotent teardown**

Set state `Stopping`, clear the public handle under the wait lock, stop new
submissions, then call `VhfDelete(handle, TRUE)` at `PASSIVE_LEVEL`. Set state
`Absent` afterward. Calling stop with a null handle must succeed without side
effects. Do not call VHF from the ISR.

- [ ] **Step 7: Run package and full tests**

```bash
proxyenv/bin/python -m unittest tests.test_apple_input_windows_package -v
proxyenv/bin/python -m unittest discover -s tests -v
```

Expected: all pass; default package remains transport-only.

- [ ] **Step 8: Commit implementation and ledger row**

Commit as `input: add gated keyboard VHF frontend`, then append its full hash to
CHANGES with `status=implemented` and commit the ledger.

### Task 5: Connect discovery, report dispatch and teardown

**Files:**
- Modify: `drivers/apple-input/windows/src/transport.c`
- Modify: `drivers/apple-input/windows/src/device.c`
- Modify: `drivers/apple-input/windows/include/apple_input_ioctl.h`
- Modify: `drivers/apple-input/windows/src/diagnostics.c`
- Modify: `drivers/apple-input/windows/tools/AppleInputDiag/main.c`
- Modify: `tests/test_apple_input_windows_package.py`

**Interfaces:**
- `AiTransportProcessPacket` calls `AiVhfFrontendStart` once when final discovery acceptance returns `AI_COMPLETE`.
- Device-1 `AI_PACKET_READ` messages in `READY` call `AiVhfFrontendSubmitKeyboard`.
- PnP/transport teardown calls `AiVhfFrontendStop`.
- Version-3 diagnostics add keyboard VHF state, accepted/rejected/submitted counts, submission-failure count and last `NTSTATUS`.

- [ ] **Step 1: Add failing lifecycle/dispatch tests**

Require this ordering in source:

1. descriptor capture before final discovery acceptance;
2. `AI_COMPLETE` before `AiVhfFrontendStart`;
3. report direction/device checks before `AiVhfFrontendSubmitKeyboard`;
4. `Stopping=TRUE` before `AiVhfFrontendStop`;
5. VHF stop before MMIO unmap.

Require no `Vhf` call in `AiInputInterruptIsr` and require distinct diagnostic
counters for invalid report and VHF submission failure.

- [ ] **Step 2: Run focused tests and verify RED**

Run the package test. Expected: missing start/dispatch/teardown ordering failures.

- [ ] **Step 3: Start the frontend only at final discovery completion**

When `ai_discovery_accept` returns `AI_COMPLETE`, leave transport phase `READY`.
Call `AiVhfFrontendStart`; the manager returns success without creating a VHF
object when transport-only and otherwise starts the keyboard. On publication
failure it records VHF state/status but keeps the transport and devnode alive so
the diagnostic client and external USB recovery remain available.

- [ ] **Step 4: Dispatch only valid device-1 input messages**

In `READY`, preserve the existing report counters. For device 1 and read
direction, call submit with `message.payload/message.payload_length` after the
message CRC and exact HID-contract validation. Count accepted, rejected,
submitted and VHF-failed reports separately. Device 2 remains count-only.

- [ ] **Step 5: Order quiesce and deletion before resource release**

`AiTransportStop` sets `Stopping`, prevents new workers/submissions, resets the
portable queue/reassembler and calls `AiVhfFrontendStop` from the passive PnP
path. `AppleInputEvtDeviceReleaseHardware` calls transport stop before unmapping
MMIO. Repeated D0 exit/release calls remain idempotent.

- [ ] **Step 6: Extend bounded diagnostics and CLI**

Print VHF state and scalar counters in text/JSON. Keep all report bytes and key
values absent. Add tests asserting no field or printf label contains `KeyCode`,
`PressedKeys`, `RawReport` or payload arrays.

- [ ] **Step 7: Run focused and complete tests**

Run the portable test, package test and full Python suite. Expected: all pass,
with no Air change.

- [ ] **Step 8: Commit implementation and ledger row**

Commit as `input: publish validated keyboard reports through VHF`, then append
its full hash to CHANGES with `status=implemented` and commit the ledger.

### Task 6: Build and verify the ARM64 package without installing it

**Files:**
- Modify if required by a failing contract only: `.github/workflows/apple-input-wdk.yml`
- Modify: `documentation/APPLE_INPUT.md`
- Modify: `investigation/CHANGES.csv`

**Interfaces:**
- Produces an unsigned ARM64 development package containing `AppleInput.sys`,
  `AppleInput.inf`, generated `AppleInput.cat`, PDB and `AppleInputDiag.exe`.
- Does not install or launch anything on the Air.

- [x] **Step 1: Run all local verification from a clean tracked root**

```bash
git diff --check
proxyenv/bin/python -m unittest discover -s tests -v
cd m1n1_windows && ./tests/run_host_tests.sh
```

Expected: all pass. The nested repositories may remain intentional untracked
gitlinks; tracked root files must be clean after commits.

- [x] **Step 2: Push the feature branch and wait for Apple input WDK CI**

```bash
git push origin feature/j313-native-input
gh run list --workflow "Apple input ARM64 WDK" --branch feature/j313-native-input --limit 1
gh run watch <run-id> --exit-status
```

Expected: successful ARM64 build and artifact publication.

- [x] **Step 3: Download and verify the artifact**

Download into an ignored local directory, verify PE machine type ARM64, the
generated INF/catalog outputs and SHA-256 for every packaged file.
Confirm the package default remains `TransportOnly=1`.

- [x] **Step 4: Record software-only result**

Update `documentation/APPLE_INPUT.md` with CI run, artifact name and hashes but
state explicitly that keyboard VHF is not hardware validated. Append/update the
CHANGES process row with `status=implemented`, not `validated`, then commit and
push documentation.

### Task 7: Hardware Gate C1 — validate descriptor ownership only

**Files:**
- Modify before run: `investigation/EXPERIMENTS.md`
- Modify after run: `investigation/EXPERIMENTS.md`
- Modify after validation: `investigation/CHANGES.csv`

**Interfaces:**
- Uses the new package with `TransportOnly=1`.
- Produces repeated version-3 descriptor length/digest snapshots without a VHF keyboard child.

- [ ] **Step 1: Create the pre-run experiment entry**

Record a new experiment ID, UTC time, branch/root commit, nested m1n1/Mu commits,
all diff hashes, exact CI package and SHA-256, unchanged EXP-057 assisted
firmware pair, oem12 rollback, install command, snapshot paths and these gates:

- expected: phase 8, keyboard length 182, trackpad length 110, stable nonzero
  digests, valid keyboard contract, VHF state absent, no new HID child;
- failure: boot regression, phase below 8, changing digest, parser rejection,
  timeout/CRC/fragment/offline count, bugcheck or loss of SSH/display/USB.

Commit this pre-run entry before installation.

- [ ] **Step 2: Install exactly the transport-only package**

Verify host and Air hashes match, preserve the installed oem12 INF/package, run
the installer without `-PublishKeyboard`, restart only `ACPI\\APPL0001`, and do
not replace the ESP or firmware.

- [ ] **Step 3: Capture repeated snapshots**

Capture at least four `AppleInputDiag.exe status --json` snapshots over eight
seconds plus PnP/service/DriverStore state, SSH reachability, display status and
fatal `hv.log` markers. Verify raw descriptors or reports do not appear.

- [ ] **Step 4: Record verdict and rollback if needed**

Append the post-run result without rewriting the pre-run entry. If any failure
criterion occurs, restore oem12 and stop. If all criteria pass, mark Gate C1
validated, append a CHANGES process row referencing the experiment, commit and
push the evidence.

### Task 8: Hardware Gate C2 — publish and validate the native keyboard

**Files:**
- Modify before run: `investigation/EXPERIMENTS.md`
- Modify after run: `investigation/EXPERIMENTS.md`
- Modify after validation: `investigation/CHANGES.csv`
- Modify after validation: `documentation/APPLE_INPUT.md`

**Interfaces:**
- Uses the exact Gate C1-validated package with only the service parameter changed by `-PublishKeyboard`.
- Produces a working built-in keyboard VHF child or a falsifiable rollback result.

- [ ] **Step 1: Create the pre-run experiment entry**

Record the same artifact hashes and firmware pair as Gate C1. The single changed
variable is `TransportOnly: 1 -> 0`. Expected: keyboard VHF state running, new
HID keyboard child, increasing accepted/submitted counters on built-in key
activity and no transport error. Failure includes any stuck key, report reject,
submission failure, devnode problem, hang, bugcheck or loss of external USB.

Commit the pre-run entry before changing the parameter.

- [ ] **Step 2: Enable keyboard publication and restart only APPL0001**

Run the package installer with `-PublishKeyboard`, confirm the service parameter
is exactly zero, and restart `ACPI\\APPL0001`. Do not reboot or change firmware
unless PnP explicitly requires it; if a reboot is required, record that fact
before performing it.

- [ ] **Step 3: Validate keyboard behavior**

With external USB recovery still attached, validate:

- letters, numbers and punctuation in a disposable text field;
- left/right Shift, Ctrl, Alt and Windows modifiers;
- key repeat and simultaneous modifiers;
- Caps Lock state as observed by Windows;
- function-row reports that Windows recognizes;
- complete key release after every chord;
- Windows sign-in input after one controlled reboot;
- devnode restart followed by resumed input;
- normal Windows shutdown.

Capture snapshots before activity, during activity and after idle. Do not record
typed text or raw input payloads.

- [ ] **Step 4: Run bounded stability validation**

Run 30 minutes of mixed built-in keyboard plus external mouse use. Require no
bugcheck, hang, stuck key, unbounded IRQ/worker/SPI rate, reset loop, VHF failure
or transport error. Verify display, USB, SSH, storage and all accepted baseline
CPUs remain alive.

- [ ] **Step 5: Record verdict and recovery**

On failure, set `TransportOnly=1`, restart APPL0001 and confirm oem12 rollback is
still installable. On success, append the post-run evidence, mark only the
keyboard feature validated, update `documentation/APPLE_INPUT.md`, append the
validated CHANGES process row and commit/push all evidence.

### Task 9: Open Precision Touchpad Gate D1 without implementing a mouse

**Files:**
- Create locally only: `.local/apple-input/trackpad-captures/`
- Modify: `investigation/EXPERIMENTS.md`
- Create after review: `drivers/apple-input/protocol/tests/fixtures/j313_trackpad_sanitized.h`
- Create after evidence review: `documentation/plans/2026-08-24-precision-touchpad-implementation.md`

**Interfaces:**
- Consumes the hardware-validated transport and keyboard frontend.
- Produces sanitized, controlled-delta Apple trackpad fixtures and the separate Precision Touchpad implementation plan.

- [ ] **Step 1: Design an explicit test-only capture build**

After keyboard validation, return to brainstorming/TDD for a bounded raw-capture
mechanism that is impossible to enable in production. It must cap report count
and size, require administrator access, exclude keyboard device 1 entirely, and
store raw device-2 reports only under ignored `.local/` evidence.

- [ ] **Step 2: Capture controlled trackpad deltas**

Record separate experiments for no contact, one stationary contact, X-only
motion, Y-only motion, physical click and two contacts. Change only one physical
gesture per capture and record exact artifact/build hashes.

- [ ] **Step 3: Sanitize and review fixtures**

Derive only fields proven by controlled deltas, remove timestamps and unrelated
payloads, document the descriptor hash and provenance, and commit the minimum
reviewed fixture set. Do not guess confidence, pressure, palm or geometry fields.

- [ ] **Step 4: Write the Precision Touchpad plan**

Use the approved design plus proven fixture layout to define exact parser field
offsets, contact capacity, coordinate ranges, report IDs, required feature
callbacks, Input Mode 0/3 behavior, selective reporting and hardware gates.
The plan must not introduce the old basic relative-mouse milestone.

## Completion Boundary

This plan is complete when Gate C2 validates the built-in keyboard and Gate D1
has produced enough sanitized evidence for an exact Precision Touchpad plan. It
does not claim the trackpad is implemented. The next plan begins only from the
proven Apple report layout and implements the fixed Microsoft Precision
Touchpad descriptor, feature callbacks, contact translator and Gate D2/D3.
