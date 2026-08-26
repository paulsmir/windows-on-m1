# J313 AGX G2 power-broker qualification gate

## Purpose

EXP-20260826-118 permits exactly one bounded Windows qualification of the new
synthetic EL2 AGX power broker.  It proves only that the fixed J313
`/arm-io/gfx-asc` and `/arm-io/sgx` domains can transition
`OFF -> ON -> ON (QUERY) -> OFF` and produce exact receipts.  It does not start
GPU firmware and it does not authorize SGX MMIO, interrupts, UAT, queues,
commands, rendering or display ownership.

The broker is control-plane only.  It is intentionally outside the future
render/data path, so this design does not impose a steady-state performance
tax.

## Immutable inputs

- root branch `feature/j313-gpu-acceleration`;
- m1n1 commit `035b8ab38b504fa30f15e4db75649b1c5e1e73ae`;
- Mu commit `c6108366201f869b297912a0ef8323b343256ecc`;
- Windows qualification source commit
  `74e824bfbbd1923db2ee278b4808f219e8bf5f23`;
- WDK run `32979986789`, job `build-arm64 (power-qualification)`, artifact
  `AppleAgx-ARM64-PowerQualification`;
- Mu run `32980992246`, job `build-and-verify (agx-g2, TRUE)`, artifact
  `J313-EFI-AGX-G2`;
- assisted profile manifest SHA-256
  `fd3058016fe866258eefefc79d5e72e6136479682b0390b1c2cb00f8293177c2`;
- m1n1 SHA-256
  `0135f6d3a7d5de5b582073f77ff5f5121c35e591608063ad367f7aac6f65cf33`;
- Mu FD SHA-256
  `70b216c01f3d7acd77b2c24d0a3dc4fa0cccefec631031a413a301d271f6c064`;
- SSDT SHA-256
  `a6f8f4911030c23b61a2ed8c3a300d1ca438af74accc41e624918930ef55f65b`;
- driver manifest SHA-256
  `9c5ab00a08c856e0dd2459cd1850acebe3a8a382812f61801d838989022a9f75`;
- driver file SHA-256 values:
  - INF `65cb45a460c76bed2007c7f7e88e199d86df1c8a141ebb360268219b0e68966e`;
  - SYS `0417b597fd7d377105842ad5bb6acbd242441ff2036c957cf678cdff2cb9c1d7`;
  - CAT `dda6c603707eb89c7e4d59802c2f9f780b0c07082d6e50ec32e39c334fbf114d`;
  - CER `48e08e439d3a2765e95540b634dbebd8339c3c53cfbf066aa77252f568a2cbce`;
- catalog signer SHA-1
  `E85192E5FD6D15A43C05B2D9E652B9867EB22825`;
- immutable stable recovery directory
  `.local/recovery/STABLE-j313-8core-native-input-v1/`, whose five payload
  hashes and manifest must pass before and after execution.

## Exact precondition

The live stable Windows baseline collected at `2026-08-26T14:41:06Z` has eight
logical processors, AppleInput `Running` and `OK`, no critical System event,
zero present APPL0002 devices, zero AppleAgx package/service/module/certificate,
and exactly one non-present historical `ACPI\\APPL0002\\0` record with Problem
45, class `Display` and friendly name
`Apple AGX G13 render adapter (G2 development)`.

Before mutation, reproduce that state, verify every candidate and recovery
hash, verify the catalog signer, and require a fresh absent evidence path:

`investigation/artifacts/EXP-20260826-118-agx-power-broker-qualification/`

Any mismatch rejects the experiment without staging or booting G2.

## One permitted execution

1. Under the already-running stable Windows instance, import only the exact
   public test certificate into LocalMachine Root and TrustedPublisher.  Stage
   the exact package with `pnputil /add-driver` only; do not use `/install`,
   device restart, rescan or reboot.  Record exactly one `oemNN.inf`, prove the
   device remains non-present and then shut Windows down normally.
2. Launch the exact assisted profile once, using proxy L41, vUART L43, display
   `both`, debug `monitor`, `--chainload`, and
   `WOM1_AGX_G2_POWER_BROKER=1`.  No artifact substitution, retry or second G2
   boot is permitted.
3. Require these exact host facts before guest handoff:
   - `HV: AGX power broker mapped at 0x300000000..0x300001000 (ABI 1)`;
   - the Python reserved tracer covers exactly that page.
4. Require exactly three broker command receipts, in order:
   - `seq=1 cmd=1 state=3 result=0` (`ON`);
   - `seq=2 cmd=0 state=3 result=0` (`QUERY`);
   - `seq=3 cmd=2 state=0 result=0` (`OFF`).
   No fourth receipt and no rejected request is allowed.
5. Within 180 seconds require responsive Windows with eight CPUs, healthy
   AppleInput and NVMe, exactly one present APPL0002 bound to `AppleAgx`, exact
   MMIO resources `0x204000000..0x207fffffff` and
   `0x300000000..0x300000fff`, exact IRQs 880..888, stopped service, no loaded
   AppleAgx module and Problem 43 after the intentional final
   `STATUS_NOT_SUPPORTED`.
6. Require no BugCheck, critical System event, reset, exception, storage reset,
   input loss or guest unresponsiveness.  Shut down normally after evidence is
   captured.

## Forbidden actions

No GPU firmware boot, RTKit endpoint, SGX register access, interrupt enable or
injection, UAT mapping, queue, command, fence, shader, render callback, present,
child/display target, display ownership or raw guest PMGR access is permitted.
The KMD may map only the synthetic broker resource and must always unmap it.

## Rollback and verdict

After the single G2 boot, return through the immutable stable assisted pair.
Only after APPL0002 is non-present may rollback delete the recorded package and
the exact signer entries.  Use ordinary non-force deletion first; the existing
exact `/uninstall` fallback is permitted only when Windows reports the recorded
package still associated.  `/force` is forbidden.

Final stable state must reproduce the full baseline, have zero active
qualification state and pass all immutable recovery hashes.  Any missing or
out-of-order receipt, non-OFF final state, unexpected access, different PnP
result or loss of responsiveness rejects the run and requires evidence
preservation plus stable rollback, never a retry.

A pass authorizes only preregistration of the next firmware/RTKit ownership
gate.  It does not authorize that gate.
