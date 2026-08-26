# J313 AGX DriverEntry boundary qualification

## Purpose

EXP-20260826-120 is a new single-use successor to rejected EXP-119. It changes
only the qualification driver diagnostics: two persistent DWORD values bracket
`DxgkInitialize`. The firmware, ACPI resources, synthetic power broker, stable
recovery image and forbidden-hardware boundary remain byte-for-byte unchanged.

The hypothesis is that the Problem 43 boundary occurs before
`DxgkDdiStartDevice` because the current WDDM 2.6 initialization table is
rejected by `DxgkInitialize`. The run must distinguish these outcomes:

- no breadcrumb values: Windows did not enter this exact DriverEntry;
- stage 1 and status `0x00000103`: DriverEntry began but `DxgkInitialize` did
  not return before evidence collection;
- stage 2 and an exact status DWORD: `DxgkInitialize` returned that NTSTATUS;
- stage 2 and success: continue reading the existing StartDevice stages, but
  still fail closed before any unqualified GPU operation.

This is control-flow evidence only. It does not authorize GPU firmware, RTKit,
SGX MMIO, interrupts, UAT, queues, commands, rendering, presentation or
display ownership.

## Source-first basis

The design was checked against:

- the live EXP-119 J313 resources and traces: one exact `APPL0002`, SGX
  `0x204000000..0x207fffffff`, broker `0x300000000..0x300000fff`, IRQs
  880..888, no broker command and no StartDevice event;
- the accepted m1n1 G1 lifecycle and the current narrow EL2 broker, which own
  only the fixed platform power transition and expose no render hot path;
- the Mu opt-in AGX SSDT and live `_CRS`, which already crossed the XSDT and
  matched Windows exactly;
- the Asahi AGX ownership model, used only to understand the hardware
  sequencing and direct firmware/queue architecture;
- Microsoft `DriverEntry of Display Miniport Driver`, `DxgkInitialize`,
  `DRIVER_INITIALIZATION_DATA`, and the official render-only sample. These
  references require a supported interface version and its mandatory callback
  table before `DxgkDdiStartDevice` can be reached.

The present run does not correct that table. It records the exact boundary
needed before one root-cause correction is designed.

## Immutable inputs

- root branch `feature/j313-gpu-acceleration`;
- diagnostic implementation commit
  `ede6e476218070d50a95c13363803356a3cdc005`;
- CI workflow head `7d221d644b003402b9d01a111f343c57a5de2ad9`;
- WDK run `32991981562`, artifact
  `AppleAgx-ARM64-PowerQualification`; both default and qualification jobs
  passed;
- driver manifest `.local/agx-power-exp120-ci/DRIVER-MANIFEST.json`, SHA-256
  `6cf7321e32849418a4dbac70cc027db0fedb4b5ab3fbadf6c3b325357c8262ca`;
- exact driver files:
  - INF `02a5316c3de4ac1939482b6d965805bd809301fac65d99df8579be2b866618bb`;
  - SYS `cf8dcf46c33b0e2cdb6143764a916631b845d58f84eb78cf482ae5c40e2452e3`;
  - CAT `5c8ae8868908a7c701c2e3a1e8068077f9ece7f4bf99bb865530f9fa7cd895d1`;
  - CER `e8a3981fb8fd729a340976fdfbcffebe6af5420c7e9432aa331bc1217e54de18`;
  - signature provenance
    `d1dd25325394cf7d74937d551f213bdba61010f3d1a13fa4f97de392df411ada`;
- WDK catalog signer SHA-1
  `442D150255F1F27A6D10CFD8E4BF5F35E8AD28BB`;
- exact scripts:
  - stage `dca0f0c3645b5dee687701f1a2536d3f8b85d67f7378c026803be97d8fb83ff3`;
  - collect `472b57334413354f84874814acda7ca3d149eddb7c3d4d4bd9e5f5610b340494`;
  - rollback `8ea74d7fccf9f40a90c6c96e08eb540e07ef846b6698e9bd5b0fc7fe3c95c70d`;
- separate assisted directory `.local/agx-power-exp120-profile/` with:
  - manifest `32f71dbe29ce3299c46d46595479a2777993d6916eebf639ab03774aed622e63`;
  - clean m1n1 `380920f80e460544b74c6ff9439bdb8af6fe02ec44492149cb32ccdebcf6315d`
    and embedded tag `035b8ab`;
  - Mu FD `70b216c01f3d7acd77b2c24d0a3dc4fa0cccefec631031a413a301d271f6c064`;
  - AGX SSDT `a6f8f4911030c23b61a2ed8c3a300d1ca438af74accc41e624918930ef55f65b`;
- immutable stable recovery
  `.local/recovery/STABLE-j313-8core-native-input-v1/`.

Every hash and manifest must pass immediately before mutation. The fresh
evidence directory
`investigation/artifacts/EXP-20260826-120-agx-driverentry-boundary/` must not
exist. Any mismatch rejects the experiment before staging.

## Mandatory cleanup and stable preflight

EXP-120 must not execute until stable Windows is responsive over SSH and
proves all of the following after EXP-119 cleanup:

- eight logical processors and AppleInput `Running/OK`;
- no new critical System event and responsive NVMe;
- no present `ACPI\APPL0002\0`;
- zero active AppleAgx package, service, loaded module and signer entry;
- exact recovery hashes still pass.

Remove only EXP-119 `oem17.inf` and signer
`DC81FF63FD2FFE8CDE24F95052C45BB7C0006731`. Ordinary package deletion is
required first; the already preregistered non-force `/uninstall` fallback is
allowed only for `oem17.inf`. `/force` is forbidden. Failure of any cleanup or
baseline check leaves EXP-120 blocked and causes no new mutation.

## One permitted execution

1. Under stable firmware, run only the exact stage script against the exact
   package directory. Record the new `oemNN.inf`, prove APPL0002 remains
   absent, then shut Windows down normally.
2. Start the exact EXP-120 assisted profile once with proxy L41, vUART L43,
   display `both`, debug `monitor`, chainload enabled, and
   `WOM1_AGX_G2_POWER_BROKER=1`:

   ```sh
   WOM1_AGX_G2_POWER_BROKER=1 M1N1VUART=/dev/cu.usbmodemC02HDNCCQ6L43 \
   scripts/run-assisted.sh \
     --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
     --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
     --firmware .local/agx-power-exp120-profile/J313_EFI.fd \
     --m1n1 .local/agx-power-exp120-profile/m1n1.macho \
     --display both --debug monitor --chainload \
     --contract-output investigation/artifacts/EXP-20260826-120-agx-driverentry-boundary/launch-contract.bin \
     --foreground
   ```

3. Require only the exact broker mapping
   `0x300000000..0x300001000`, ABI 1, and guest handoff. No broker command is
   required for this boundary experiment.
4. Within 180 seconds collect the two service-registry values, PnP binding,
   resources, service state, package identity, loaded-module count, CPUs,
   AppleInput state, critical events, System log, hv.log and guest UART.
5. Shut down normally after evidence capture. No retry or second G2 boot is
   allowed.

## Verdict rules

- Exact stage 2 plus a non-success DWORD identifies the immediate
  `DxgkInitialize` rejection and confirms the next software-only correction
  boundary.
- Stage 1 plus pending status is inconclusive for return status but confirms
  entry into the driver; preserve any hang or BugCheck evidence and reject.
- No values rejects the loader/signing/package hypothesis boundary and requires
  Code Integrity plus SetupAPI analysis before any driver change.
- Success from `DxgkInitialize` permits interpretation of the existing
  StartDevice stages, but does not qualify power or any GPU hardware use.
- Any artifact mismatch, wrong resources, broker command, forbidden GPU access,
  guest unresponsiveness, BugCheck, reset, storage/input regression or missing
  evidence rejects the run.

## Forbidden actions

No GPU firmware boot, RTKit endpoint, SGX register access, interrupt enable or
injection, UAT mapping, queue, command, fence, shader, render callback, present,
child/display target, display ownership, raw PMGR access, forced package
deletion, artifact substitution or retry is allowed.

## Rollback

Return through the immutable stable assisted pair. Only after APPL0002 is
absent may the exact recorded EXP-120 OEM INF and signer
`442D150255F1F27A6D10CFD8E4BF5F35E8AD28BB` be removed. Try ordinary package
deletion first; non-force `/uninstall` is the only fallback. Remove no other
package or certificate.

Final stable state must reproduce every preflight invariant and all recovery
hashes. Preserve a SHA-256 index of all evidence regardless of verdict. A
result authorizes only one separately reviewed correction at the identified
Windows initialization boundary.
