# J313 AGX StartDevice boundary qualification

## Purpose

EXP-20260826-119 is a new single-use successor to rejected EXP-118. It uses a
clean, manifest-verified m1n1 binary and a qualification-only Windows driver
that records fail-closed StartDevice boundaries in the ordinary Windows System
event log. The run must either prove the exact synthetic broker transition
`OFF -> ON -> ON (QUERY) -> OFF`, or identify the first driver boundary that
prevents that transition.

This is a control-plane diagnostic. It does not authorize GPU firmware, SGX
MMIO, interrupts, UAT, queues, commands, rendering, presentation or display
ownership.

## Immutable inputs

- root branch `feature/j313-gpu-acceleration`;
- root source commit `82a93ff50613dc531ecf23dc0238f78aaf830ce7`;
- m1n1 commit `035b8ab38b504fa30f15e4db75649b1c5e1e73ae`;
- clean m1n1 embedded tag `035b8ab`;
- Mu commit `c6108366201f869b297912a0ef8323b343256ecc`;
- WDK run `32987511238`, artifact
  `AppleAgx-ARM64-PowerQualification`;
- assisted manifest SHA-256
  `32f71dbe29ce3299c46d46595479a2777993d6916eebf639ab03774aed622e63`;
- m1n1 SHA-256
  `380920f80e460544b74c6ff9439bdb8af6fe02ec44492149cb32ccdebcf6315d`;
- Mu FD SHA-256
  `70b216c01f3d7acd77b2c24d0a3dc4fa0cccefec631031a413a301d271f6c064`;
- SSDT SHA-256
  `a6f8f4911030c23b61a2ed8c3a300d1ca438af74accc41e624918930ef55f65b`;
- driver manifest SHA-256
  `b588bad78887da1993899cc7e0e38d3b06a87fe4598e9cd8facb5dae46418777`;
- driver files:
  - INF `e26db2dbd0bbf45509bdea05baa8b30b73160b5a3f8c4241070a6abfa474bf91`;
  - SYS `cf5e0fe192acef3801d57860746b084dd73bfd85fb8ba42a3a169fbf2c3c125e`;
  - CAT `34782747d6bd49fccc1430d855bdf0aed0504a02e21ded91dd969366dc38a73e`;
  - CER `ce7a0d3a050cf54a8561b119b8076a096842299fee45b8c8401983423d9b6c74`;
  - signature provenance
    `595518d3397caf8325fef522539fe439c3b2a57f5c0b210151089d1397163989`;
  - stage script
    `9717f6019ca541ffc0f629df4033746428a265fa5b48699d6750247099c3cb90`;
  - rollback script
    `c19bb9a86d1c8beaf0fde43243da616ad5ac9aec4a15b7cfeb714f5d1ba919ec`;
- WDK catalog signer SHA-1
  `DC81FF63FD2FFE8CDE24F95052C45BB7C0006731`;
- immutable stable recovery directory
  `.local/recovery/STABLE-j313-8core-native-input-v1/`.

Before staging, all candidate, profile and recovery hashes must pass. The
certificate thumbprint, catalog signer and signature provenance source commit
must match. The fresh evidence directory must not exist:

`investigation/artifacts/EXP-20260826-119-agx-startdevice-boundary/`

Any mismatch rejects the experiment before Windows or hardware mutation.

## Required stable baseline

Stable Windows must report eight logical processors, AppleInput `Running/OK`,
responsive SSH and NVMe, no new critical System event, no present APPL0002,
and zero active AppleAgx package, service, module or signer entry. A historical
non-present APPL0002 record is allowed and must be recorded separately.

## One permitted execution

1. While exact stable Windows is running, import only `AppleAgx.cer` into
   LocalMachine Root and TrustedPublisher. Stage only the exact INF using the
   pinned script. Do not install, rescan or restart a device. Record exactly
   one new `oemNN.inf`, prove APPL0002 remains non-present, and shut down
   normally.
2. Launch `.local/agx-power-exp119-profile/` once through the public assisted
   path with proxy L41, vUART L43, display `both`, debug `monitor`, chainload
   enabled and `WOM1_AGX_G2_POWER_BROKER=1`.
3. Require the host broker mapping to be exactly
   `0x300000000..0x300001000`, ABI 1. No other synthetic or raw PMGR page is
   permitted.
4. Collect every qualification System event in order. Event stages are:
   `1 Entered`, `2 DeviceInformation`, `3 ResourcesValidated`,
   `4 StateValidated`, `5 BrokerAddress`, `6 BrokerTransaction`, and
   `7 FailClosed`. Preserve raw XML so the stage and final NTSTATUS are not
   inferred from formatted UI text.
5. Collect all EL2 broker records. A power-transition pass requires exactly:
   `seq=1 cmd=1 state=3 result=0`,
   `seq=2 cmd=0 state=3 result=0`, and
   `seq=3 cmd=2 state=0 result=0`, with no fourth command or rejection.
6. Within 180 seconds require responsive Windows, eight CPUs, healthy native
   input and NVMe, one present APPL0002 bound to the exact qualification INF,
   MMIO `0x204000000..0x207fffffff` and
   `0x300000000..0x300000fff`, IRQs 880..888, a stopped service, no loaded
   AppleAgx module and the intentional fail-closed Problem 43 result.
7. Shut down normally after evidence capture. No retry is permitted.

## Verdict rules

- Passing stages 1 through 6 plus exact ordered broker receipts proves the
  synthetic power transaction. Stage 7 and Problem 43 are still expected
  because rendering remains intentionally unsupported.
- If stages stop before 6, the last complete stage and the stage-7 NTSTATUS
  identify the next code correction. The run is diagnostic-only and does not
  qualify power.
- Stage 6 without all three exact broker receipts is rejected as a host/client
  contract mismatch.
- Missing evidence, different resources, guest unresponsiveness, BugCheck,
  reset, storage/input regression or any forbidden access rejects the run.

## Forbidden actions

No GPU firmware boot, RTKit endpoint, SGX register access, interrupt enable or
injection, UAT mapping, queue, command, fence, shader, render callback,
present, child/display target, display ownership, raw guest PMGR access,
forced package deletion, second G2 boot or artifact substitution is allowed.

## Rollback

Return using the immutable stable assisted pair. Only after APPL0002 is absent
may rollback remove the one recorded OEM INF and the exact signer from Root and
TrustedPublisher. Ordinary package deletion is required first; the existing
non-force `/uninstall` fallback is permitted only for the exact recorded INF.
`/force` is forbidden.

Final stable state must reproduce all baseline invariants and recovery hashes.
Preserve a SHA-256 index of all raw evidence regardless of verdict. A pass
authorizes only design of a separately preregistered firmware ownership gate;
it does not authorize that gate.
