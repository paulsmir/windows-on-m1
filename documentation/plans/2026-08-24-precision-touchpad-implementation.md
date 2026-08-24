# J313 Windows Precision Touchpad Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish the J313 built-in Apple trackpad as a native Windows Precision Touchpad through an independent VHF child, with correct multi-contact identity, click, feature reports, recovery and hardware evidence.

**Architecture:** Keep Apple SPI HID transport, validation and post-discovery multitouch initialization in the existing owner layer. Add a portable Apple contact decoder and persistent slot tracker, then encode only proven fields into a project-owned Precision Touchpad descriptor schema whose axis metadata is derived from the validated native descriptor. A second VHF object owns the touchpad feature state and remains independently gated from the already working keyboard.

**Tech Stack:** C11 portable protocol tests, KMDF ARM64, Windows Virtual HID Framework, HID 1.11 usages, Microsoft Windows Precision Touchpad protocol, GitHub Actions WDK build, PowerShell hardware validation.

**Spec:** `documentation/design/2026-08-24-vhf-keyboard-precision-touchpad.md`

## Global Constraints

- Work only in `/Users/pavel/public_windows` on `feature/j313-native-input`; do not use the private checkout or worktrees.
- Preserve the accepted four-E-core firmware and ESP. This plan changes only the Windows AppleInput package.
- Keep the keyboard and trackpad as separate VHF objects. Failure of either frontend must not remove the other or stop Apple SPI transport.
- Do not add a relative-mouse fallback. Completion is a Windows Precision Touchpad.
- Normal Debug and Release packages must never compile raw trackpad capture support.
- Do not publish raw reports, coordinates, keys or arbitrary descriptor bytes in normal diagnostics or the repository.
- Derive axis logical/physical metadata from the owned, structurally valid J313 trackpad descriptor; do not hard-code guessed dimensions.
- Publish no more than five Windows contacts. Track up to eleven Apple contacts so contacts suppressed at the Windows limit remain suppressed for their complete physical lifetime.
- Do not publish pressure, width, height, palm, force or haptics until a separate controlled-delta gate proves those semantics.
- Every defect or feature starts with a failing test, every hardware run has pre/post `investigation/EXPERIMENTS.md` entries, and every implementation commit is followed by a separate `investigation/CHANGES.csv` ledger commit.
- No commit may contain assistant attribution, session URLs or `Co-Authored-By` trailers.

## Primary Contracts Inspected

- Live J313 EXP-20260824-050: 76-byte one-contact frames, 106-byte two-contact frames, click bytes at offsets 1 and 31, contact count at offset 30, 30 bytes per contact, zero capture drops and descriptor digest `9da960157f983b6494a19ce6fde471191c183bbdf54486d9217be4e800abcfef`.
- Asahi Linux `drivers/hid/spi-hid/spi-hid-apple-core.c`: the native descriptor is parsed by the HID core and feature requests are separate from input delivery; the transport does not translate gestures.
- Upstream Linux `drivers/input/keyboard/applespi.c`: `abs_x`/`abs_y` are signed little-endian values, Y is inverted for host coordinates, zero `touch_major` is filtered, and stable multitouch slots are assigned from contact trajectories rather than the Apple `origin` field.
- Current m1n1/Mu contract: ACPI `APPL0001` exposes the physical MMIO and guest interrupt; m1n1 routes the interrupt and neither layer owns Windows HID translation.
- Microsoft Precision Touchpad: mandatory Touch Pad and Configuration collections; mandatory Contact ID, X, Y, Tip, Confidence, Scan Time and Contact Count; maximum 3–5 contacts; Device Capabilities and 256-byte Certification Status feature reports; Input Mode 0/3 and Selective Reporting feature reports.
- Microsoft VHF: create after `WdfDeviceCreate` at `PASSIVE_LEVEL`, submit input through `VhfReadReportSubmit`, service feature reports through `EvtVhfAsyncOperationGetFeature`/`SetFeature`, complete every operation with `VhfAsyncOperationComplete`, and delete synchronously only at `PASSIVE_LEVEL`.

## File Map

- Create `drivers/apple-input/protocol/include/apple_trackpad.h`: portable Apple frame, axis contract, physical tracker and Precision Touchpad report interfaces.
- Create `drivers/apple-input/protocol/src/apple_trackpad_axis.c`: bounded HID short-item parser for X/Y logical, physical, unit and exponent metadata.
- Create `drivers/apple-input/protocol/src/apple_trackpad_frame.c`: Apple 48-byte header/30-byte contact decoder and physical contact tracker.
- Create `drivers/apple-input/protocol/src/apple_precision_touchpad.c`: fixed five-contact Windows report encoder and feature-state helpers.
- Create `drivers/apple-input/windows/include/apple_precision_touchpad_descriptor.h`: project-owned descriptor template, report IDs, feature sizes and patch offsets.
- Create `drivers/apple-input/windows/src/vhf_trackpad.c`: second VHF lifetime, feature callbacks and input submission.
- Modify `drivers/apple-input/windows/src/vhf_frontend.c`: independent keyboard/trackpad lifecycle orchestration.
- Modify `drivers/apple-input/windows/src/transport.c`: submit validated device-2 frames only after multitouch READY.
- Modify `drivers/apple-input/windows/include/apple_input_device.h`: touchpad VHF, parser, feature and diagnostic state.
- Modify `drivers/apple-input/windows/include/apple_input_ioctl.h`, `src/diagnostics.c`, and `tools/AppleInputDiag/main.c`: metadata-only versioned counters.
- Modify `drivers/apple-input/windows/AppleInput.vcxproj` and package tests: compile and enforce the new boundary.
- Modify `drivers/apple-input/windows/AppleInput.inf` and install scripts: independent `PublishTrackpad` gate, default off until hardware Gate D2.
- Modify `drivers/apple-input/protocol/tests/apple_spihid_test.c`: all portable golden, malformed, slot and feature tests.
- Modify `documentation/APPLE_INPUT.md`, `investigation/EXPERIMENTS.md`, and `investigation/CHANGES.csv`: operation, rollback and durable evidence.

---

### Task 1: Close the release-frame evidence boundary

**Files:**
- Verify: `drivers/apple-input/windows/tools/AppleInputCapture/main.c`
- Verify: `tests/test_apple_input_windows_package.py`
- Create: `drivers/apple-input/protocol/tests/fixtures/j313_trackpad_release_sanitized.h`
- Modify: `investigation/EXPERIMENTS.md`

**Interfaces:**
- Consumes: the validated 0.1.2.0 capture package and descriptor digest from EXP-20260824-050.
- Produces: one bounded finger-down/finger-up capture tied to the already validated descriptor digest, plus a sanitized zero-contact/release fixture.

- [ ] **Step 1: Verify the existing capture safety contract**

Inspect and run the existing package tests proving that the capture CLI accepts `--count 8`, rejects counts outside 1..16, requires an explicit new output path and opens it with `CREATE_NEW` so evidence is never overwritten. No kernel ABI or production-code change is part of this task.

- [ ] **Step 2: Run the focused safety tests**

Run: `./proxyenv/bin/python -m unittest tests.test_apple_input_package -v`

Use the actual module `tests.test_apple_input_windows_package`; expected result is PASS because this is characterization of the already hardware-validated 0.1.2.0 evidence tool, not a new production behavior.

- [ ] **Step 3: Record the hardware pre-run**

Record the existing kernel limits of 16 reports by 512 bytes, administrator-only capture, exact 0.1.2.0 hashes, active `oem15.inf`, stable descriptor digest, recovery and the single changed physical variable. Do not change the capture ABI, driver, firmware, ESP or descriptor handling.

- [ ] **Step 4: Record the exact already-installed capture package**

Reuse the hardware-accepted `0.1.2.0` capture package from EXP-20260824-050. Record SYS/CAT/INF/CLI SHA-256 values and preserve `oem15.inf` as rollback in the pre-run EXP entry. Do not rebuild the driver merely to collect one missing transition.

- [ ] **Step 5: Capture a physical release transition**

Arm eight reports while one finger is already down, then lift it. Accept the run only if at least one earlier report contains one contact and the terminal report proves the exact zero-contact or tip-clear wire shape. Store raw evidence only under `.local/apple-input/trackpad-captures/<EXP-ID>/` and commit only a sanitized terminal shape.

- [ ] **Step 6: Add a failing parser-fixture assertion, then sanitize**

Before creating the fixture, add a portable test include/reference that fails because `j313_trackpad_release_sanitized.h` is absent. Then add only the proven terminal release bytes and provenance needed by Task 3; run the focused and complete suites.

- [ ] **Step 7: Commit evidence and ledger separately**

Commit the sanitized fixture and EXP result. Append one CHANGES process row with that commit hash, then commit only the ledger row.

### Task 2: Parse native descriptor axis metadata

**Files:**
- Create: `drivers/apple-input/protocol/include/apple_trackpad.h`
- Create: `drivers/apple-input/protocol/src/apple_trackpad_axis.c`
- Modify: `drivers/apple-input/protocol/tests/apple_spihid_test.c`
- Modify: `tests/test_apple_spihid_protocol.py`

**Interfaces:**
- Produces:
  - `enum ai_status ai_trackpad_axis_contract_parse(const uint8_t *descriptor, size_t length, struct ai_trackpad_axis_contract *out);`
  - `struct ai_trackpad_axis { int32_t logical_min, logical_max, physical_min, physical_max; uint32_t unit; int8_t unit_exponent; bool valid; };`
  - `struct ai_trackpad_axis_contract { struct ai_trackpad_axis x, y; bool valid; };`

- [ ] **Step 1: Write failing synthetic descriptor tests**

Add a compact HID descriptor with a Touch Pad/Finger collection, signed X/Y logical ranges, physical ranges, unit and exponent. Assert exact scalar extraction. Add rejection cases for truncated items, long items, missing X/Y, duplicate conflicting axes, `min >= max`, unit mismatch, global-stack overflow and arithmetic overflow.

- [ ] **Step 2: Run the portable test and verify RED**

Run: `./proxyenv/bin/python -m unittest tests.test_apple_spihid_protocol -v`

Expected: compile failure for the missing header/function.

- [ ] **Step 3: Implement the minimal bounded HID short-item parser**

Track Usage Page, Logical/Physical Min/Max, Unit, Unit Exponent and a four-entry Push/Pop stack. Record X (Generic Desktop 0x30) and Y (0x31) only inside a Finger logical collection beneath Touch Pad. Return `AI_ERR_PROTOCOL` on ambiguity and zero `out` on every failure.

- [ ] **Step 4: Run portable sanitizers and package tests**

Run the focused unittest, then compile the native test once with `-fsanitize=address,undefined` and run it. Expected: all cases pass and no sanitizer report.

- [ ] **Step 5: Fix the parser ownership boundary**

Keep this task portable: the parser accepts a caller-owned descriptor and returns only scalar metadata. Task 6 calls it on the context-owned native descriptor before creating VHF, and Task 7 exposes the returned scalars plus the existing descriptor digest through normal diagnostics. No raw descriptor is added to an IOCTL or log.

- [ ] **Step 6: Commit implementation and ledger separately**

Commit parser/tests first. Append an implemented CHANGES row with no hardware validation claim, then commit the ledger.

### Task 3: Decode Apple contact frames and maintain physical lifetimes

**Files:**
- Create: `drivers/apple-input/protocol/src/apple_trackpad_frame.c`
- Modify: `drivers/apple-input/protocol/include/apple_trackpad.h`
- Modify: `drivers/apple-input/protocol/tests/apple_spihid_test.c`

**Interfaces:**
- Produces:
  - `enum ai_status ai_apple_trackpad_decode(const uint8_t *report, size_t length, struct ai_apple_trackpad_frame *out);`
  - `enum ai_status ai_trackpad_tracker_update(struct ai_trackpad_tracker *tracker, const struct ai_apple_trackpad_frame *frame, struct ai_trackpad_output_frame *out);`
  - Constants `AI_APPLE_TRACKPAD_MAX_CONTACTS=11`, `AI_PTP_MAX_CONTACTS=5`, header `48`, contact stride `30`.
  - Each decoded contact contains signed `x`, signed `y`; unknown fields remain opaque and unpublished.

- [ ] **Step 1: Write failing fixture tests**

Decode all four sanitized fixtures. Assert click byte equality, contact count, exact 76/106 lengths, selected signed X/Y values and two distinct contacts. Add malformed tests for unequal click bytes, count > 11, formula mismatch, integer overflow and null arguments.

- [ ] **Step 2: Write failing lifetime tests**

Feed synthetic trajectories for crossing contacts, reorderings, one lift, all lift, five admitted plus one suppressed, an admitted lift while the suppressed contact remains down, and a new contact after a slot is free. Assert IDs remain stable, one tip-clear release is emitted at the last position, and the pre-existing suppressed contact is never promoted mid-lifetime.

- [ ] **Step 3: Run and verify RED**

Run the focused protocol unittest. Expected: missing decoder/tracker symbols.

- [ ] **Step 4: Implement bounded decoding**

Validate `length == 48 + 30 * count - 2` for nonzero frames and the exact release shape proven by Task 1 for zero contacts. Decode only click, count and signed little-endian X/Y. Do not interpret `origin`, pressure, major/minor, orientation, multi or unknown header bytes.

- [ ] **Step 5: Implement deterministic trajectory assignment**

Maintain eleven physical slots. For each frame, choose the minimum total squared-distance assignment with deterministic slot-index tie-breaking; max 11 permits a bounded exhaustive/DP solution with no allocation. Mark at most five newly arriving physical lifetimes admitted. Preserve the admitted flag until physical lift and emit one release record before reusing a Windows ID.

- [ ] **Step 6: Run sanitizers and commit**

Run focused tests, ASan/UBSan, then the complete suite. Commit implementation/tests; append and separately commit the CHANGES row.

### Task 4: Encode the Microsoft Precision Touchpad contract

**Files:**
- Create: `drivers/apple-input/protocol/src/apple_precision_touchpad.c`
- Modify: `drivers/apple-input/protocol/include/apple_trackpad.h`
- Modify: `drivers/apple-input/protocol/tests/apple_spihid_test.c`

**Interfaces:**
- Produces:
  - `enum ai_status ai_ptp_encode_input(const struct ai_trackpad_axis_contract *axes, const struct ai_trackpad_output_frame *frame, uint16_t scan_time_100us, const struct ai_ptp_feature_state *features, uint8_t *report, size_t capacity, size_t *length);`
  - `enum ai_status ai_ptp_get_feature(struct ai_ptp_feature_state *, uint8_t report_id, uint8_t *buffer, size_t capacity, size_t *length);`
  - `enum ai_status ai_ptp_set_feature(struct ai_ptp_feature_state *, uint8_t report_id, const uint8_t *buffer, size_t length, bool contacts_active, bool *neutral_required);`
  - Defaults: input mode 0, surface=1, button=1, max contacts=5, depressible click-pad type=0.

The immutable wire contract is:

```c
enum ai_ptp_report_id {
    AI_PTP_REPORT_INPUT = 1,
    AI_PTP_REPORT_CAPABILITIES = 2,
    AI_PTP_REPORT_CERTIFICATION = 3,
    AI_PTP_REPORT_INPUT_MODE = 4,
    AI_PTP_REPORT_SELECTIVE = 5,
};

struct ai_ptp_wire_contact {       /* exactly 6 bytes */
    uint8_t flags;                  /* bit 0 Confidence, bit 1 Tip */
    uint8_t contact_id;             /* stable Windows ID 0..4 */
    uint8_t x_le[2];                /* normalized 0..4095 */
    uint8_t y_le[2];                /* normalized and inverted 0..4095 */
};

/* ID + 5 contacts + scan time + contact count + button/padding = 35 bytes. */
#define AI_PTP_INPUT_REPORT_SIZE 35u
#define AI_PTP_CAPABILITIES_REPORT_SIZE 2u
#define AI_PTP_CERTIFICATION_REPORT_SIZE 257u
#define AI_PTP_INPUT_MODE_REPORT_SIZE 2u
#define AI_PTP_SELECTIVE_REPORT_SIZE 2u
```

Mode 0 emits no pointer input because this design intentionally has no Mouse collection. Windows must select mode 3 before touchpad reports are submitted.

- [ ] **Step 1: Write failing golden report tests**

Assert exact byte layouts for neutral, one contact, two contacts, click, release and five contacts. Assert X maps linearly from native logical min/max to 0..4095, Y maps inversely to 0..4095, scan time wraps at 16 bits, contact count never exceeds five and confidence is 1 for every currently admitted contact because no palm evidence exists.

- [ ] **Step 2: Write failing feature-state tests**

Assert Device Capabilities returns max 5 and type 0; Certification returns the Microsoft-documented 256-byte default blob; Input Mode accepts 0/3 and maps all other values to 0; Selective Reporting independently gates surface/button; mode changes while contacts are down request exactly one neutral report and defer new-mode reports until all contacts are physically up.

- [ ] **Step 3: Run and verify RED**

Run the focused protocol unittest. Expected: missing encoder/feature functions.

- [ ] **Step 4: Implement exact fixed-size encoding**

Use five parallel six-byte finger blocks with one flags byte, one 8-bit Contact ID, 16-bit X and 16-bit Y, followed by Scan Time, Contact Count and Button 1 plus seven padding bits. Zero all unused finger blocks. Use checked 64-bit arithmetic for coordinate normalization and reject invalid axis contracts.

- [ ] **Step 5: Implement bounded feature handlers**

Feature functions perform no I/O and no allocation. Reject unknown report IDs and wrong lengths. Store only input mode, selective reporting and latency state. Certification blob is immutable.

- [ ] **Step 6: Run full software verification and commit**

Run focused tests, sanitizers and complete suite. Commit implementation/tests; append and separately commit the CHANGES row.

### Task 5: Define and statically validate the VHF descriptor

**Files:**
- Create: `drivers/apple-input/windows/include/apple_precision_touchpad_descriptor.h`
- Modify: `drivers/apple-input/windows/AppleInput.vcxproj`
- Modify: `tests/test_apple_input_package.py`

**Interfaces:**
- Produces `AiPrecisionTouchpadReportDescriptorTemplate`, report IDs and patch offsets for X/Y physical ranges, units and exponents. Logical X/Y ranges are fixed at 0..4095 to match the portable encoder.
- Consumes the native axis contract and portable feature/input report layouts from Tasks 2–4.

- [ ] **Step 1: Write failing source-contract tests**

Parse the descriptor bytes in Python and assert exactly two mandatory top-level collections: Digitizer/Touch Pad and Digitizer/Configuration. Assert five Finger collections, mandatory usages, exact report sizes/IDs, Device Capabilities, Certification, Input Mode and Selective Reporting. Assert there is no Mouse collection, pressure, width, height, force, palm or haptics usage.

- [ ] **Step 2: Run and verify RED**

Run: `./proxyenv/bin/python -m unittest tests.test_apple_input_package -v`

Expected: missing descriptor header and project source entries.

- [ ] **Step 3: Add the project-owned descriptor template**

Base the schema on Microsoft's parallel/hybrid sample report descriptor. Keep the schema, report IDs and logical ranges immutable; patch only checked physical range, unit and exponent items into a context-owned copy before `VhfCreate`. The patch routine must fail closed if the native metadata cannot be represented in HID short items.

- [ ] **Step 4: Verify report/descriptor agreement**

Add assertions that every portable encoder report size and feature size equals the size calculated from the descriptor parser. Run package and protocol tests.

- [ ] **Step 5: Commit implementation and ledger separately**

Commit descriptor/tests/project changes; append and separately commit the CHANGES row.

### Task 6: Add an independent trackpad VHF frontend

**Files:**
- Create: `drivers/apple-input/windows/src/vhf_trackpad.c`
- Modify: `drivers/apple-input/windows/src/vhf_frontend.c`
- Modify: `drivers/apple-input/windows/include/apple_input_device.h`
- Modify: `drivers/apple-input/windows/AppleInput.vcxproj`
- Modify: `tests/test_apple_input_package.py`

**Interfaces:**
- Produces:
  - `NTSTATUS AiTrackpadVhfStart(PAI_DEVICE_CONTEXT Context);`
  - `NTSTATUS AiTrackpadVhfSubmit(PAI_DEVICE_CONTEXT Context, const UCHAR *Report, SIZE_T Length);`
  - `VOID AiTrackpadVhfStop(PAI_DEVICE_CONTEXT Context);`
  - VHF GetFeature/SetFeature callbacks using `Context->TrackpadFeatures`.

The Windows boundary uses the exact VHF callback types and a context-owned client pointer:

```c
EVT_VHF_ASYNC_OPERATION AiTrackpadVhfGetFeature;
EVT_VHF_ASYNC_OPERATION AiTrackpadVhfSetFeature;

typedef struct _AI_TRACKPAD_VHF_STATE {
    VHFHANDLE Handle;
    BOOLEAN Running;
    struct ai_ptp_feature_state Features;
    UCHAR ReportDescriptor[AI_PTP_DESCRIPTOR_SIZE];
} AI_TRACKPAD_VHF_STATE;
```

- [ ] **Step 1: Write failing lifecycle/callback tests**

Assert the source has a second `VHFHANDLE`, independent lifecycle state, GetFeature and SetFeature callbacks, `VhfAsyncOperationComplete` on every callback path, exact buffer bounds, no SPI call in callbacks and synchronous delete only under the passive frontend lock.

- [ ] **Step 2: Run and verify RED**

Run package tests. Expected: missing trackpad VHF source and callbacks.

- [ ] **Step 3: Implement VHF creation and feature callbacks**

Create the trackpad object only when multitouch init is READY and the native axis contract is valid. Set `VHF_CONFIG.VhfClientContext = Context`, register both asynchronous feature callbacks and set vendor/product/version fields explicitly. Feature callbacks recover the device context from VHF, translate portable status to NTSTATUS and call `VhfAsyncOperationComplete` exactly once on every path.

- [ ] **Step 4: Make lifecycle independent**

`AiVhfFrontendStart` may leave keyboard Running while trackpad stays DescriptorsReady. Trackpad start/submit failure increments only trackpad counters. Stop first blocks new submissions, emits a neutral report when possible, deletes trackpad, then keyboard, before MMIO unmap.

- [ ] **Step 5: Run package/full suite and commit**

Run tests and `git diff --check`. Commit implementation; append and separately commit the CHANGES row.

### Task 7: Integrate device-2 delivery and privacy-preserving diagnostics

**Files:**
- Modify: `drivers/apple-input/windows/src/transport.c`
- Modify: `drivers/apple-input/windows/src/diagnostics.c`
- Modify: `drivers/apple-input/windows/include/apple_input_ioctl.h`
- Modify: `drivers/apple-input/windows/tools/AppleInputDiag/main.c`
- Modify: `drivers/apple-input/windows/AppleInput.inf`
- Modify: `scripts/install-apple-input.ps1`
- Modify: `tests/test_apple_input_package.py`

**Interfaces:**
- Consumes Apple device-2 reports only after `AI_TRACKPAD_INIT_READY`.
- Produces diagnostic ABI v4 counters/state and independent `PublishTrackpad` service gate.

- [ ] **Step 1: Write failing ordering/privacy tests**

Assert parse/submit occurs after CRC/reassembly and MT READY, never in the ISR, and never while TransportOnly or PublishTrackpad is false. Assert diagnostics contain counts/status/rejection reasons/active count only and no report payload, coordinates or keys.

- [ ] **Step 2: Run and verify RED**

Run package tests. Expected: missing submit path, gate and v4 fields.

- [ ] **Step 3: Connect the data path**

Under the existing passive worker, decode, update physical slots, encode and submit. Use one monotonically wrapping 100µs scan-time counter. A malformed frame is dropped and counted without resetting transport; repeated VHF failures stop only the trackpad frontend.

- [ ] **Step 4: Add fail-closed publication controls**

Default INF values remain `TransportOnly=1`, `PublishKeyboard=0`, `PublishTrackpad=0`. The installer requires explicit `-PublishKeyboard` and `-PublishTrackpad`; uninstall restores all gates off before package removal.

- [ ] **Step 5: Add diagnostic ABI v4**

Expose axis-valid flags and scalar ranges, trackpad VHF lifecycle, decoded/rejected/submitted counts, last rejection enum, active/admitted/suppressed counts, feature callback counts and last NTSTATUS. Preserve v1–v3 query compatibility.

- [ ] **Step 6: Run complete software verification and commit**

Run the complete suite, descriptor parser tests, ASan/UBSan, XML parse, INF checks and `git diff --check`. Commit implementation; append and separately commit the CHANGES row.

### Task 8: Build, install and prove Gate D2 on J313

**Files:**
- Modify before and after run: `investigation/EXPERIMENTS.md`
- Modify after evidence: `documentation/APPLE_INPUT.md`
- Modify after implementation commit: `investigation/CHANGES.csv`

**Interfaces:**
- Consumes the exact ARM64 package from Tasks 2–7 and the accepted firmware baseline.
- Produces one reversible Gate D2 verdict without touching ESP.

- [ ] **Step 1: Create the pre-run record**

Record root commit, unchanged m1n1/Mu commits, dirty hashes, exact WDK run, package hashes, signer SHA1, current `oem15.inf`, active SYS hash, accepted firmware artifact and rollback commands. Change one variable: install the new AppleInput package with keyboard on and trackpad off.

- [ ] **Step 2: Validate transport-only and axis metadata first**

Confirm APPL0001 Started, service Running, discovery phase 8, MT phase READY, stable descriptor digest, valid scalar axis contract, keyboard functional and zero transport errors. Any failure rolls back before VHF publication.

- [ ] **Step 3: Enable only PublishTrackpad**

Set `PublishTrackpad=1`, restart APPL0001 and verify a new HID Touch Pad and Configuration collection appear with no Problem Code. Capture GetFeature/SetFeature counts proving Windows requested capabilities, certification and Input Mode 3.

- [ ] **Step 4: Perform the physical acceptance matrix**

Verify cursor direction and full-surface reach, tap, held drag, physical primary click, Windows-configured right click, two-finger vertical/horizontal scroll, pinch zoom, three-finger and four-finger Windows gestures, simultaneous keyboard input, every contact release and devnode restart. If direction/range is wrong, change only the axis transform in a new experiment.

- [ ] **Step 5: Validate recovery**

Disable PublishTrackpad and restart APPL0001 to prove the keyboard remains. Re-enable it, then perform normal shutdown and cold boot. On failure, set `PublishTrackpad=0`; if necessary set `TransportOnly=1` or reinstall recorded rollback INF.

- [ ] **Step 6: Record the verdict and commit evidence**

Append exact observed counts, PnP IDs, elapsed time and failures. Mark validated only if the complete matrix passes. Update APPLE_INPUT.md, commit evidence, then append/commit the validated or rejected CHANGES row.

### Task 9: Prove Gate D3 stability and finish the public milestone

**Files:**
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `documentation/APPLE_INPUT.md`
- Modify: `documentation/ROADMAP.md`
- Modify: `investigation/CHANGES.csv`

**Interfaces:**
- Consumes a Gate D2 hardware-validated package.
- Produces the only evidence that permits calling the native keyboard/trackpad milestone stable.

- [ ] **Step 1: Record the 30-minute pre-run**

Keep firmware, driver and gates identical to successful D2. Record hashes and recovery before starting.

- [ ] **Step 2: Run mixed input for 30 minutes**

Mix typing, modifiers, pointer motion, click-drag, two-finger scroll and multi-finger gestures while monitoring diagnostics, System log, PnP, SSH and display. Failure is any bugcheck, hang, stuck key/contact, lost VHF child, interrupt storm, reset loop, CRC/fragment/timeout/offline increase or unbounded submission failure.

- [ ] **Step 3: Exercise lifecycle boundaries**

Perform three APPL0001 devnode restarts, one normal restart, one normal shutdown/cold boot and a test-signing-safe Driver Verifier run only after the normal path passes.

- [ ] **Step 4: Finish documentation and ledger**

Record the post-run verdict. Update public install, rollback, diagnostics and limitations. Mark the roadmap milestone complete only with the EXP ID and exact artifact hash. Commit docs, append the validated process row and commit the ledger separately.

## Completion Boundary

The plan is complete only when Windows identifies the built-in device as a Precision Touchpad, all physical gesture and lifecycle checks pass, and the 30-minute mixed-input run has no transport, VHF, PnP or system regression. Gate D1 evidence alone does not validate a driver, a successful pointer motion alone does not validate Precision Touchpad semantics, and a software-only test never permits `status=validated`.
