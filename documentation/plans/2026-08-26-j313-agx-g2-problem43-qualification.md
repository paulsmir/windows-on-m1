# J313 AppleAgx G2 Problem-43 Qualification Plan

**Goal:** Qualify once, without reinterpretation, the already observed Windows
PnP result for the immutable G2 firmware and immutable fail-closed AppleAgx
package: exact resource validation followed by `CM_PROB_FAILED_POST_START`
(Problem 43), with no AGX hardware access and complete stable rollback.

**Why this successor exists:** EXP-116 proved ACPI enumeration, package match,
service selection and exact translated resources, but its literal contract
required Problem 10.  Windows instead mapped the intentional
`StartDevice -> STATUS_NOT_SUPPORTED` refusal to Problem 43.  EXP-117 changes
only that expected PnP classification and the now-known non-present ghost
metadata.  It does not change either candidate or authorize GPU activity.

## Immutable inputs

- Work only in `/Users/pavel/public_windows` on
  `feature/j313-gpu-acceleration`.
- Stable recovery remains
  `.local/recovery/STABLE-j313-8core-native-input-v1/`; all five hashes must
  pass before and after the run.
- G2 manifest SHA-256:
  `596ed2f2ad1465fd75e1dd560adc3d5da94ea62d41a68e98e2a955bf0804f2ea`.
- G2 `m1n1.macho` SHA-256:
  `0055ef339c5ae9099014e3d8e5158a0533c2df2adb235ad3646abf7fa31ca3d5`.
- G2 `J313_EFI.fd` SHA-256:
  `3d2a2dd1360c073e8413c1fcebb3d3c072c33c3acfc7f1be27873a75e87b3070`.
- Driver manifest SHA-256:
  `ee9ac4532e4432e2b4e7faedc70ef1f101efd454f1db8f236fbb2710b26e217d`.
- Driver checksum-index SHA-256:
  `4e4ff25513bb56b8567996d30b264c6686119d3423386345aa9522caf2a6737e`.
- Signer thumbprint:
  `7772864CB7326B7BFDA2C81C12D07CEF64135A57`.
- Fresh evidence path:
  `investigation/artifacts/EXP-20260826-117-agx-g2-problem43-qualification/`.
  It must be absent before approval and may be created exactly once.

## Execution contract

1. Under immutable stable firmware require responsive SSH, eight CPUs,
   AppleInput `Running`, healthy APPL0001, no critical event, zero present
   APPL0002, zero AppleAgx package/service/module/certificates, and exactly one
   non-present `ACPI\APPL0002\0` Problem-45 record retaining class `Display`
   and friendly name `Apple AGX G13 render adapter (G2 development)`.
2. Recompute every candidate hash on both host and Windows.  Import only the
   exact signer, require the catalog signature `Valid`, stage only the exact
   package, record its single `oemNN.inf`, prove no active device/service/module
   and shut Windows down normally.
3. Boot the exact G2 candidate once through the public assisted path using
   proxy L41, vUART L43, display `both` and debug `monitor`.  No rebuild,
   substitution, retry, rescan or G2 reboot is permitted.
4. Within 180 seconds require responsive SSH, eight CPUs, healthy AppleInput
   and NVMe, one present `ACPI\APPL0002\0`, service `AppleAgx`, the recorded
   OEM INF, MMIO `0x204000000..0x207fffffff`, vectors 880..888, stopped service,
   no loaded AppleAgx module and exactly Problem 43
   (`CM_PROB_FAILED_POST_START`).
5. Reject a different Problem code, Started adapter, child/display target,
   missing or shared resource, nonresponsive guest, BugCheck, watchdog,
   storage reset or input loss.  Also reject any AGX MMIO access, clock,
   firmware, UAT, queue, command, interrupt injection, power or display
   ownership marker.  Shut G2 down normally.
6. Launch only immutable stable recovery.  After proving APPL0002 non-present,
   remove only the recorded package with the hashed non-force script.  If and
   only if that exact package remains associated, the previously bounded
   `/uninstall` fallback is allowed; `/force` is forbidden.  Remove only the
   exact certificate from Root and TrustedPublisher.
7. Require zero active AppleAgx package/service/module/certificates, zero
   present APPL0002, eight CPUs, healthy native input, unchanged present
   display inventory, responsive SSH, no new critical event and all five
   stable-recovery hashes passing.  Preserve and hash all evidence.

Any mismatch stops the experiment and restores stable recovery; it never
broadens permission.  A passing result authorizes only a new, separately
preregistered firmware/power ownership plan.  It does not authorize firmware
startup, MMIO reads or writes, clocks, interrupts, UAT, queues, commands,
rendering or display ownership.

A new explicit user approval is mandatory after this plan and EXP-117 are
committed and pushed.  Preregistration performs no Windows or hardware
mutation.
