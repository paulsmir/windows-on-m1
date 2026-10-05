# R139 — EXP855B post-Start Code43

## Evidence and owning contract

Offline only; no Air, package, stage or hardware experiment. Task:
`/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R139.md`.
Saved evidence root: main repository `.local/experiments/EXP855B-r138-scanout-pool/hardware-evidence/`.
ETL SHA256 a6243dbb2892a7716361bed03d7069adb117f23fd468d6471bd448458d1aebe7.
Builder tracerpt decoded the saved ETL; thread 4952 names the failure:
StartDevice returns 0; allocator 0xffff918cab89a5f0 reserves
[0x02000000,0x04000000) (GpuVirtualAddressRange Flags0x12), then AzureTriage
reports failure to create the KMD system-process handle. Event549 returns
C0000141/FailureReason3. StopDeviceAndReleasePostDisplayOwnership and StopDevice
return 80000011 only AFTER Event549. PnP ProblemCode43/ProblemStatus0 cannot
attribute the inner failure. Registry CreateInput: SystemProcess1, Started1,
IRQL0, NumPasid1 and hDxgkProcess present. No CreateContext input receipt.

Source attribution: R137 calls ReserveGpuVirtualAddressRange, then rejects
StartVirtualAddress < 2^36 as INVALID_ADDRESS before bootstrap/graph creation.
The OS reservation is 2^25 aligned and one leaf-table span. Its actual placement
is below that guard. This is a KMD post-callback address-validation defect;
not a failure of StartDevice, caps, firmware or a prohibition on system reservation.
The exact callback return value has no dedicated receipt, but successful range
creation immediately before failed CreateProcess and this explicit status branch
identify the replay input. No new receipt is necessary for this attributed call.

Sources inspected: Asahi `drivers/gpu/drm/asahi/mmu.rs` map_node/user VA and
`arch/arm64/boot/dts/apple/t8103.dtsi` GPU resources;
m1n1 `src/hv_agx_gpuva_v5.c` exclusive backing registration; Mu
`Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc`
R64 resources and `Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c`
reserved-memory HOB; KMD gpuva_g3_windows.c, gpuva_g3_paging_windows.c, receipts.c;
shared apple_agx_gpuva_g3_caps.h, apple_agx_gpuva_g3_graph.c and
apple_agx_g3_private_storage.h; R136 reservation design and R138 result.
No external source code is copied. Existing m1n1/Mu dirty state is preserved.

Microsoft callback explicitly permits system and regular process reservation
inside CreateProcess at PASSIVE. Size/alignment are leaf coverage multiples;
BaseAddress0 asks the OS to choose; AllowUserModeMapping0 stays private.
The remark about root entry1 does not justify assuming 64GiB on this measured
Windows build: its allocator reserved leaf slot1 at32MiB. Accept the returned
leaf-aligned range within the advertised 39-bit address space; never overwrite
an occupied ordinary leaf edge. Null/first leaf, misalignment and overflow
remain invalid. The trace is a regression fixture, not an exact-address allowlist.

Sources:
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_reservegpuvirtualaddressrange
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_reservegpuvirtualaddressrange
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createprocess

Ownership: Windows owns VA reservation and VidMm page tables; KMD owns its
reserved leaf and private40/16/8 storage, mapping lifetime and validation;
m1n1 owns broker/physical grants, DMA isolation, firmware power/interrupts;
Mu exposes the inherited R64/ACPI contract. Asahi owns its own VM allocation,
so its placement policy cannot substitute for the Windows callback result.
Accepted assisted full-owner and ordinary EXP377/392 launch/recovery contracts
remain those of EXP855B; no standalone or new hardware claim.
Smallest later hardware checkpoint: system CreateProcess succeeds after the
same successful StartDevice, then observe Code0 or the next named failure.
Recovery: evidence first, exact experiment cleanup, ordinary EXP377/392 Code28;
no such checkpoint is authorized here.

WHY CONTINUE COMPARISON: no historical comparison is needed. Current ETL identifies
the returned VA and failed system CreateProcess; reproduce that current KMD guard.

## Review responses

REVIEW R113: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R111: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R110: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R109: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R108: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R107: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R106: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R105: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R104: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R103: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R102: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R100: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R99: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R98: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R97: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R96: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R95: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R94: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R91: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R90: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R88: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R86: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R85: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R74: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R71: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R69: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R65: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R64: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R63: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R57: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R55: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R54: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R49: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R48: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R47: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R45: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R40: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R37: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.
REVIEW R64: DEFER — historical item outside the EXP855B CreateProcess reservation boundary; no hardware/firmware/package change in R139.

## Implementation and verification

The four guards now validate the minimum non-null leaf span (2^25), not one
native root-entry span (2^36). Size, alignment,39-bit limit, callback error,
backing/leaf collision, root parking refusal and lifetime rules are unchanged.
The OS callback is modeled; CreateProcess, private initialization, graph,
outer paging DDIs, escape, completion/notification and broker are real C.

Pinned builder WDK26100 `shared/d3dkmddi.h:8708-8732` inspected, SHA256
`c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`.
Its SizeInBytes comment explicitly requires leaf-table coverage multiples.
No Windows binary was built. Builder SSH was used only to decode the saved
ETL with tracerpt and read this header. The tracerpt-rendered timezone suffix
is malformed (+01:59); absolute UTC here comes from original EVTX record157
(2026-09-27T20:36:50.789028Z), while ETL relative ordering uses its deltas.
StartDevice exit precedes Event549 by173.150ms; both Stop calls follow it.
The callback-success/address attribution is an inference from the allocator's
range-create event, immediately following driver C0000141 and named failed
system-process handle, corroborated by the identical real-C refusal.

RED: `CC=clang G3_REPLAY_R137=1 G3_REPLAY_RESERVE_BASE=0x2000000 python3 tests/g3_vidmm_replay.py`
returns `R137 reservation: status c0000141` before the change.
GREEN: `CC=clang python3 -m unittest discover -s tests -p test_g3_vidmm_replay.py -k r139 -v`
passes3 tests covering four valid bases, both system/regular process, invalid
bases, both profiles16/64 and private escape/completion/root-reuse paths.
The low-root replay's ordinary parking-root conflict uses a different root
index from the private branch; its previous fixed index0 becomes the private
branch in this scenario and is no longer an ordinary conflict. The existing
nonempty shared-middle refusal and preservation tests also execute.
Focused suite:34 PASS (G3 VidMm/private storage/private pool/caps/contracts,
CreateProcess/start resource and change ledger).
Independent read-only review: no findings; reviewer independently ran all three
R139 tests successfully (11.843s). Full suite:
`CC=clang python3 -m unittest discover -s tests -v`:1140 tests in105.493s,
15 failures/41 errors/2 skips. The exact56 failure/error names equal R138:
new0/removed0, saved in `failure-comparison.json`. This is not a green full
suite. Exact names are also listed in the unchanged baseline report
[R138-scanout-pool.md](R138-scanout-pool.md#unchanged-baseline-failureserrors-exact-names).

Saved input manifest:8/8 SHA256 verified. Driver source manifest SHA256
`b81eaea81c3e13bad07bf4886049a4ffef99969e0439a32454b830cd5f49da44`.
Tandem REVIEW SHA256 (unchanged at final review):
`753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5`.
Base root `e1f834a59a1de49702f11fa84d4a1cff886a4114`;
m1n1 `8769e5e981730ca5c971ad985e65bd47d005e8c0`, pre-existing diff
`68387a2e4a778333004ae9e8e035304dfd70c5d83c8e1ec4dcff4c949427f0e7`;
Mu `f0f1c50a040d490f78340b8995917ede24fc4220`, pre-existing diff
`2e654da05fbcd83288511c5161cc749c8b87d69e222cfc3f6fed3fceaea79a7d`.
Offline outputs are in this worktree `.local/experiments/R139-offline/`.


Final evidence manifest SHA256 `e54fbedd603ab234ba22879cb9056daa03152741799565d8b20b0acdd616d27f`; `git diff --check` passes.
Offline verdict: confirmed faulty KMD reservation guard, corrected and replayed.
No Code0, G4 hardware completion, DWM frame or hardware validation is claimed.
Next causal target is separately authorized observation of system CreateProcess
and subsequent adapter admission, with the accepted EXP855B ordinary recovery.
