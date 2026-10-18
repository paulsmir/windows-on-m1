# Typed relocation capture checkpoint — mission remains active

ARCHITECT_DECISION: continue the physical/patch-list architecture; reuse the
existing KMD DynamicJobMaterialize. New per-device/request capture retains BO
identities via callbacks, registers exact disjoint subranges and typed pointer
fields, revalidates identities on seal and emits the existing Win32 wire ABI.
Only exact owner/generation/request/fence retirement releases submitted BO refs.
These references do NOT claim to pin VidMm residency or placement.

Added agx_win32_reloc_capture.h/c and executable capture->transport->validator->
materializer tests. Two placements change expected resolved fields while keeping
non-address bytes and the first output image intact. Tests cover stale/foreign
identity, source serial changes, bounds, overlapping fields/subranges, capacity,
reference retention/abort, request reuse, stale retirement and aliased callback
storage on restart. Serialized non-reentrant device-context use is required.

## Confirmed owning-layer defect

Real pinned Mesa AGX_USC_UNIFORM pack/unpack exposed that dynamic_patch's old
0xffffff mask removed bits24/25 of size_halfs. size48 became the zero encoding
(decoded64). Source-native size64 alone did not expose it because64 encodes0.
Fixed only that production mask to0x3ffffff: addresses are4-byte aligned and
occupy bits26..63; control bits0..25 must survive. All sizes1..64 compare byte-
exactly with native pack output after relocation. No shader/PBE/firmware/caps
change. This is an offline regression proof, not a hardware verdict.

Pinned generated header SHA256:
aedee39dd8eb0305acdd21512c1334f2923b4b3a99db0dd2ef61912471415ce8.
The test uses native USC fields, not a complete native Asahi draw encoder or
real agx_screen/agx_context. Generic encoder references in this fixture remain
synthetic. Do not advertise complete pointer-graph closure from this test.

## Verification / raw evidence

- Host clang ASan/UBSan: capture composition, Mesa transport, dynamic job and
  Win32 ABI suites GREEN.
- FRYZZING LLVM20.1.8: x64 build/link/execution and ARM64 build/link PASS.
- Four current capture/test/materializer source hashes independently match
  Windows input manifest. ARM64 executable not run.
- investigation/evidence/AD04-typed-reloc/windows contains result/input/artifact
  manifests and x64 stdout; builder result directory:
  C:\Users\pauls\AD04-fullcompiler-001\reloc-capture-7ac671dac1c74e6791146d913e4c2358.
- Frozen source archive .local/phase3/reloc-input.rntcjY.tar.gz SHA256
  474a0b762a5548577a9d032fc1dffe9537ae81691d9487cae934c2bc931ec807.
  Executed runner is build-reloc-capture-004.ps1; archive's earlier runner is
  not the authority for the successful process-wait/manifest behavior.
- Earlier builder attempts: missing render_allocation.h; a stuck Start-Process
  wait after compiler/link exit; missing exit-status observation with the first
  workaround; AppleDouble ._. metadata broke hashing. Preserved attempt dirs,
  no compiler/GPU conclusions from these runner failures. Exact owned waiting
  PowerShell PID42400 was stopped, no other builder process was terminated.

## Next executable boundary

Wire provenance capture into actual native BO/pool/USC emission and compile the
real Asahi frontend/platform seam. Native pool already offers
agx_pool_alloc_aligned_with_bo, which yields its owning BO; prefer that to
recovering provenance by scanning integer values. Real agx_screen/context still
require Windows-owned device/queue/sync/BO operations replacing Linux DRM calls.
Do not stop at this helper checkpoint or claim a standard D3D hardware device.

Hardware authorization is now standing. User reported Running proxy; passive
USB inventory independently found m1n1 uartproxy and the expected two serial
endpoints. No active launcher was observed at that check. No Windows SSH health,
current package state, stage, install, boot or hardware experiment was performed.
Read GPU_ENGINEERING_PROCEDURE and preregister a falsifiable exact candidate
before hardware; never launch old EXPs solely because the machine is available.
