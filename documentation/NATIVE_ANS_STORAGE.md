# J313 native ANS — current architecture audit

2026-09-05. Implementation specification: `plans/J313_NATIVE_ANS_REQUEST.md`.
This is an offline audit, not a native-storage hardware qualification.

## Source identities and evidence

Audited local root `6b021e1deda82da79a5e4e82d12010cf69638dc4`, m1n1
`9cf6c9d9fffdf191108d222411df025e5148fef5`, Mu
`f1ef718e08db0e4c30fdb5d8555973513ad9a004`. Linux reference is pinned to
`77cb8f24c2381a8abb7272d7bbdec548d6426a8a` in the existing local reference
repository; its sparse checkout omits apple.c, so that file was read from the
pinned Git object. Linux source is GPL-2.0: consulted as protocol evidence only;
no Linux implementation is copied into the MIT code.

Inspected m1n1 `src/nvme.c`, `src/hv_nvme.c`, `src/hv_nvme_queue.h`,
`src/hv_pci.c`, `src/sart.c`, `src/rtkit.c`, `src/hv_vm.c`; Linux
`drivers/nvme/host/apple.c` and `arch/arm64/boot/dts/apple/t8103.dtsi`; Mu
`Platform/MacBookAirMid2020Pkg/AcpiTables/{DSDT.asl,MCFG.aslc}`,
`MacBookAirMid2020.fdf`, and
`Silicon/Apple/AppleSiliconPkg/Application/BootLaunchApp/BootLaunchApp.c`.

Official Windows references inspected:
- https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/hardware-initialization-with-storport
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportinitialize
- https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/storport-i-o-model

Pinned WDK 10.0.26100.0 header inspection, miniport analysis and signing are
still gates, not implied by reading online documentation.

Saved proof is EXP377/392 and EXP474 final ordinary recovery; verified artifact
hashes and capture identities are in `investigation/ans/RECOVERY_BASELINE.json`.
The exact assisted G2 pair is preserved as separate read-only files under
`.local/ans-native/recovery/`. The earlier golden FD e8312... is a different
artifact from the ordinary G2 FD 16c177... and is not substituted. No fresh ADT,
register, IRQ or namespace read was taken: user forbids Air access before the
offline work is complete. GPU fixtures are not an ANS ADT snapshot.

## CURRENT OWNERSHIP

Windows stornvme and UEFI use a synthetic PCI NVMe 00:00.0. EL2 hv_pci traps
ECAM and hv_nvme mediates commands into synchronous nvme_read/write wrappers.
The backend owns ANS ASC/RTKit, SART, queue allocations, TCBs and controller
lifetime. nvme_exec_command always uses tag 0, polls CQ phase, invalidates TCB,
and rings the CQ head. This is not a native concurrent queue engine.

## TARGET OWNERSHIP

Physical StorPort miniport AppleAns owns SRBs, SCSI translation, logical SQEs,
tags, CQ consumption and disk semantics. EL2 owns controller/RTKit lifecycle,
SART, TCB, stage-2 authorization, shadow PRPs, physical IRQ routing and recovery.
Hardware owns payload DMA and completion creation. V1 metadata can be copied;
READ/WRITE payload cannot. No unrestricted firmware mapping or vendor command.
Only one frontend may submit, including during UEFI handoff and failures.

## RESOURCE MAP

| Resource | Evidence | Guest access |
|---|---|---|
| ANS controller 0x27bcc0000 / 0x40000 | Pinned T8103 DT; m1n1 dynamically reads ANS reg index 3 | None in V1 |
| ANS coprocessor 0x277400000 / 0x4000 | Pinned T8103 DT | None |
| SART 0x27bc50000 / 0x10000 | Pinned T8103 DT | None |
| Broker control page | Must allocate a synthetic IPA and check all platform ranges | Only broker ABI |
| ECAM 0x690000000 / 0x100000 | Current layout and Mu MCFG/DSDT | Recovery/UEFI PCI enumeration |
| PCI BAR allocation 0x400000000 / 0x20000000 | Current DSDT | No native runtime NVMe device |

Physical values are source-derived expectations, not a fresh live measurement.
At eventual launch resolve `/arm-io/ans` and `/arm-io/sart-ans` from actual ADT
and reject disagreement. Do not turn unknown synthetic allocation into a
hardware-looking literal.

## IRQ MAP

Physical ANS completion is AIC 590, Level/ActiveHigh in pinned T8103 DT. Its
native guest GSIV needs an explicit non-overlapping allocation. Recovery GSIV
64 remains owned by existing virtual INTx. GPU has guest 880..888, input 865.
Native IRQ must use AIC reception and dedicated level state, draining both CQs;
never share vnvme_intx_delivery. Live ADT correspondence remains unverified.

## DMA ADDRESS MODEL

CPU granule is 16 KiB; ANS PRPs use 4 KiB. Current low-memory stage-2 alias maps
guest 0x100000 to host 0x8a0100000; therefore direct guest-address programming
is invalid. hv_ipa_to_pa verifies hardware PTE type but alone does not enforce
owner RAM, protected ranges, complete transfer coverage, or DMA permissions.
Native translation must check all of those, snapshot guest descriptor metadata,
and retain shadow backing until proven TCB invalidation. Limit payload to
128 KiB, 4 KiB LBAs, at most one PRP list (up to 33 data pages for an offset
PRP1). No chained PRP lists, payload copies, or SART grants for guest payload.

Linux ANS2 defines 64 total shared tags, two reserved admin tag slots, one I/O queue,
one admin queue and one IRQ. TCB direction bits are FROM_DEVICE bit 0 and
TO_DEVICE bit 1; recovery currently grants both. Native must choose exact
command direction. Native TCB length is the protocol's NLB field, not a byte
count guessed from the payload.

## UEFI → WINDOWS HANDOFF

UEFI keeps vNVMe. Native handoff must gate new virtual submission, drain,
flush, prove controller disabled and RTKit stopped, invalidate generation,
hide PCI function and arm native ownership. Failure cannot continue EBS.
An ordinary VOID ExitBootServices event notification cannot veto EBS; use a
return-status-capable pre-EBS boundary and preserve MapKey semantics. Current
BootLaunchApp TraceExitBootServices wraps the original call but only traces it.
Production correctness cannot depend on a diagnostic-only wrapper.

Two concrete current-source gaps prevent treating shutdown as a receipt:
1. nvme_shutdown returns void and continues freeing after disable/shutdown errors.
2. rtkit_sleep stores a bool result into int, checks `< 0`, then returns 0;
   it does not propagate rtkit_switch_power_state false. Power ACK loops in
   that helper are unbounded. A native bounded, truthful stop primitive is needed.

Linux TCB_STAT offset is 0x28120 versus m1n1 0x29120. This is a source
conflict, not hardware proof of which read is effective. Preserve recovery;
resolve the native choice against more primary evidence and document it.

## BROKER ABI

Single canonical generated definition, fixed-width LE fields and explicit sizes.
Header: magic/version/size/features/generation/state/error/sequence. Operations:
QUERY, CLAIM, REGISTER_QUEUES, AUTHORIZE_SUBMIT, REAP_COMPLETION, QUIESCE,
RESET, RELEASE. States OFF/BOOTING/READY/CLAIMED/QUIESCING/FAULTED.
Per tag FREE→PREPARED→IN_FLIGHT→COMPLETED→FREE. Nonzero monotonic request
sequence and generation checks; no wrap reuse, guessed success or silent repair.
QUERY must allow discovery without letting untrusted input choose generation.
Reset must prove DMA stopped before retiring shadows or completing uncertain SRBs.
Read-only enforcement is required independently in both halves.

## FILES TO CHANGE

New: config/j313-ans.json; tools/generate_j313_ans_contract.py;
ans-only host tests; m1n1 src/hv_ans_broker*; Windows drivers/storage/appleans/.
Later reviewed shared edits: m1n1 nvme.c/h, hv_pci.c, hv_vm.c and IRQ dispatch;
Mu generated J313AppleAns.asl.inc plus DSDT/profile and EBS integration;
launcher/profile contract. Never alter GPU driver sources or GPU_CURRENT_STATE.

## Work isolation and first hardware checkpoint

All root/m1n1/Mu edits live in separate worktrees on
feature/j313-ans-native-offline, rooted at `.worktrees/ans-native`.
No branch switch, stash, reset, checkout or commit in the GPU working copies.
No symlink to their writable build/output/cache directories. Reference evidence
is read only; recovery bytes are separately copied. Journal updates occur in
the ANS worktree only. Merge must reconcile appended ledger rows by record ID,
not overwrite GPU history. Shared platform-file changes require explicit diff
review against the then-current GPU branch. No merge or hardware launch now.

ANS001 is the first possible hardware checkpoint, only after all offline gates
and explicit user approval: independent USB/RAM WinPE, ANS0 present/unbound,
virtual NVMe absent, no AppleAns package or disk commands. Recovery is the
hash-verified ordinary G2 assisted pair. Hardware exclusivity must then be
coordinated with the GPU operator; no concurrent GPU experiment is allowed.

## Offline foundation review

The first helper review found missing input/GPU physical-MMIO collision checks
and insufficient SQE CID-to-authorized-tag binding. Both have deterministic
regressions and fixes. Native synthetic allocation is now 0x300010000/16KiB,
GSIV889, deliberately allocated in the guest namespace and checked against
pinned GPU/input/PCI/RAM allocations. It is not a measured physical address.
Generated ASL remains unconnected to DSDT until the native profile is admitted.

CQE DWORD2 is reserved in current m1n1's ANS completion structure. Pinned Linux
apple_nvme_handle_cqe selects the tagset from its source queue and never reads
SQID. Accordingly this ANS helper validates the trusted source CQ and CID and
does not reject arbitrary reserved DWORD2 as a generic PCI NVMe SQID violation.
Source-CQ identity is mandatory at the eventual hardware drain boundary.

Source-audit correction: Linux uses AQ_DEPTH=2 and AQ_MQ_TAG_DEPTH=1; its I/O
tagset reserves **two** slots despite only one blk-mq admin request. Canonical
V1 reservation is corrected to two slots, rather than mistaking active admin
request depth for reserved tag count. Initial foundation used one slot; this
is an offline correction, with no hardware artifact launched under either.

## Source-required ACPI identity correction

The supplied original spec says APPLANS0. Actual iasl ERROR6035 rejects its
non-hex product suffix. ACPI _HID requires NNNN#### with four hexadecimal
product digits; Microsoft documents the same rule. Canonical native ANS HID is
therefore **APPL0003**; ACPI, INF and validation change together. APPL0001/input
and APPL0002/GPU remain unchanged. The original user attachment is preserved
unmodified in plans/J313_NATIVE_ANS_REQUEST.md, with this explicit correction.
No installed package or launched image ever used either native ID.
Sources: https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/06_Device_Configuration/Device_Configuration.html
and https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/device-management-namespace-objects .
