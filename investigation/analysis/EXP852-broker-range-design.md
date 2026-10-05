# EXP852 — post-EBS ownership for AGX broker backing

2026-09-27. Offline design only. Task: `.local/tandem/NEXT_TASK_M1N1_RANGE.md`
in the main repository. No product changes, firmware/package builds, Air access,
installation or hardware experiment. Only this document is committed; the other
thread owns the experiment/change ledgers and compact-state updates.

**Decision:** retain the immutable launch snapshot and introduce a separate,
EL2-owned post-EBS backing policy. Release only complete 16-KiB frames inside the
loaded FD interval that a trusted firmware handoff proves are boot-only/reusable.
Preserve runtime/ACPI/platform exclusions, including exclusions outside the FD.
A static FDF list alone cannot supply that proof. A guest-writable map or an
unauthenticated “EBS done” HVC is not a safe handoff. The policy change is small;
the existing tree has no demonstrated trusted EBS producer. That prerequisite
blocks enabling release, not writing or testing the policy offline.

## WINDOWS/UEFI CONTRACT

### Evidence and source identity

Start-of-task root: `5ca5e68deee60d3af647cc7e4e3edab67d7d278f`, branch
`integration/ad04-windows-compiler`. Inspected current m1n1:
`8769e5e981730ca5c971ad985e65bd47d005e8c0`; Mu:
`f0f1c50a040d490f78340b8995917ede24fc4220`; Mu BASECORE:
`dcb182ecc8a20aaf191cfe97fabd31e21e02af01`. Submodules were already dirty;
these commit IDs do not assert clean trees or a reproducible firmware build.

The state-dependent input is the saved EXP852 state, not a new live sample.
`investigation/GPU_CURRENT_STATE.md` and
`investigation/analysis/EXP852-protected-range.md` identify the current boundary:
VidMm used PFNs `0x851420..0x851423`; broker ownership refused the corresponding
16-KiB frame. The earlier analysis checked the dump, contract CRCs and Windows
physical-memory run. This task reread its primary Mu log and source evidence
and rehashed these saved artifacts under main `.local/experiments/EXP852-r132-sysframes/`:

| Artifact | SHA-256 |
|---|---|
| `contract.bin` | `65039095770e4c185e50fecebdf85645b2ba039a35a9ba8aa5f1a60b538e3d71` |
| `full.log` | `2c082c9e82ef66734e000766e22448590a92337f8da54ce33e77924159d32b7e` |
| `firmware/m1n1-r110.macho` | `14872dba0a6ecab9d4e49e42237298b44909126f0d5f7267cc5deaed587372b1` |
| `firmware/J313_EFI-r110.fd` | `3a76857cb650cae6b8d7c5d22250debeec437cd28c2dfd032ebecb1cbd0f6eca` |

Saved launch region2 is `[0x8510b4000,0x852e3c000)` (size `0x1d88000`).
Mu's FD declaration is `[0x8510b4000,0x852eb4000)` (size `0x1e00000`):
the `0x78000` tail is not part of the loaded launch region. Neither interval
is interchangeable with FVMAIN_COMPACT (`FD+0x8000`, size `0x1d80000`).
`full.log:260,324` labels the **whole declared FD** type4/BootServicesData;
this is an early DXE map, not the final successful EBS map. Runtime allocations
type5/6 are separately visible around `0x9dd7e0000` and `0x9dd880000`.
There is no measured retained runtime subrange inside the loaded FD, but the
absence of one from an early log cannot certify that every FD frame is reusable.

Primary sources inspected (paths relative to the worktree unless stated):

| Source | Contract relevant here |
|---|---|
| Main `.local/reference/asahi-linux-asahi`, commit `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`: `drivers/gpu/drm/asahi/mmu.rs:93–147,485–525` | Separate kernel/user UAT ranges; map owned SG runs with UAT alignment checks. No Mu-FD lifetime policy. |
| Same tree, `arch/arm64/boot/dts/apple/t8103.dtsi:437–491`, `t8103-j313.dts` | Named UAT tables/handoff and calibration reservations; GPU references these explicitly. Zero DT placeholders are not measured physical addresses. |
| `m1n1_windows/src/hv_agx_retained_backing.c`, `hv_agx_retained_platform.c:48–67,382–407`, `hv_agx_gpuva_v5.c:213–244` | Snapshot exclusions, stage-2 validation, then broker OWNERSHIP before grant. |
| `m1n1_windows/src/hv_launch_contract.h`, `hv_launch_j313.c`, `hv_launch_golden_j313.c:52–88` | Launch region taxonomy and distinct assisted/standalone comparison rules. |
| `m1n1_windows/proxyclient/m1n1/hv/__init__.py:2120–2147`, `src/hv_autonomous_runtime.c:305–321` | RAM mapped separately from device/private carveouts in both launch paths. |
| `mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c:228–292,414–442` | FD is BootServicesData; R64 is an explicit reserved allocation with exclusions. |
| `mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.fdf:37–79` | Static container layout, not runtime allocation addresses. |
| `mu/MU_BASECORE/MdeModulePkg/Core/Dxe/Image/Image.c:643–665` | Runtime images receive RuntimeServicesCode/Data allocations. |
| `mu/MU_BASECORE/MdeModulePkg/Core/Dxe/DxeMain/DxeMain.c:792–881`, `Mem/Page.c:2527–2584` | MapKey validation, EBS event order, and continued use of boot memory before return. |
| `mu/MU_BASECORE/MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableProtocol.c:343–388,555–607` | Dynamic ACPI reclaim/NVS allocations, including FACS. |
| `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc:12–80` | Existing AGX resources and dynamic 64-bit R64 resource; no FD reservation. |

The prior analysis covers `DSDT.asl` and the saved ACPI installation log.
No final generated AML extraction or last-EBS descriptor array is available in
the inspected evidence; this design does not infer them from ASL or an early map.
No register, interrupt, power, clock or DMA-address value is newly selected.

### Memory classes and Windows obligations

After successful ExitBootServices, BootServicesCode/Data can be reused; runtime
memory must survive. Loader memory still contains the running loader and cannot
be treated as immediately dead merely because its type is reclaimable by the OS.
ACPI reclaim has a later lifecycle than EBS; keep it denied in this first policy,
even if Windows eventually reclaims it. NVS remains retained. See
[UEFI 2.10 §7.2/§7.4.6, table 7.6](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
and [ACPI §15.3, table 15.6](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/15_System_Address_Map_Interfaces/uefi-getmemorymap-boot-services-function.html).

| Final descriptor / independent owner | First-release policy |
|---|---|
| RuntimeServicesCode/Data **or any descriptor with `EFI_MEMORY_RUNTIME`** | Deny the physical extent, regardless of FD membership or virtual remapping. Attribute overrides a nominally reusable type. |
| ACPI NVS and ACPI reclaim | Deny; no guest claim of “ACPI copied/enabled” releases them. |
| Reserved, unusable, MMIO/port space, PAL, persistent/unaccepted memory, OEM/unknown type | Deny ordinary system backing. Unknown semantics never imply reusable RAM. |
| BootServicesCode/Data, no runtime attribute | Candidate only inside the bounded FD release window, after successful trusted finalization and exclusion of all live references. |
| ConventionalMemory, no runtime attribute | Candidate subject to complete coverage, stage-2 and independent owner exclusions. |
| LoaderCode/Data | Preserve an existing FD denial in this first change; no loader-liveness inference or unrelated reclamation. |
| Missing, overlapping, conflicting or malformed descriptor coverage | No release; reject the handoff rather than guess. |
| EL2 code/data/heap/stack, ADT/boot args, UAT roots/tables, DART/TZ carveouts, framebuffer, ramdisk | Independent platform ownership wins over every Mu descriptor. |
| R64 local reserve | Remains reserved to its existing owner; never becomes general Windows system backing. Preserve the existing exact local-owner access contract. |

“Deny every non-conventional descriptor” would incorrectly retain boot-only FD
forever. Conversely, “allow anything not runtime” would expose ACPI/reserved
memory. Classification must combine descriptor type, attributes, lifecycle and
independent physical owner. All permanent exclusions are rounded **outward** to
AGX's 16-KiB frames; releasable coverage is rounded **inward**.

VidMm owns residency and logical GPU page-table updates; the driver translates
those to hardware tables. A legal system PFN does not authenticate a firmware
handoff. Keep the WDDM contract and R133 logical-only handling of unpublishable
groups. The Microsoft [GpuMmu architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-memory-in-wddm-2-0)
and [UpdatePageTable DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable)
support this separation and the 4-KiB logical-entry/16-KiB hardware-page model;
sequential allocation offsets do not guarantee physically contiguous pages.

## M1N1 CONTRACT

### Ownership and unchanged launch invariants

| Layer | Initialization / runtime / recovery ownership |
|---|---|
| Mu | Boot allocations, runtime images, final descriptor truth, ACPI reservations and successful EBS finalization. Must finish boot-only references before release. |
| m1n1 EL2 | Inherited hardware containment, physical owner exclusions, stage-2 translation, grant eligibility, UAT publication/invalidation and one-shot policy state. Guest input cannot remove its exclusions. |
| Windows VidMm / KMD | OS residency; logical PTEs; complete-frame grouping, rights/generations, submit/fence ordering and teardown through the broker. No replacement allocator or new Windows driver. |
| Existing power/IRQ/firmware services | Continue the accepted R110 ownership and routing. This policy neither initializes AGX nor changes its interrupt, power, RTKit or DART sequences. |
| Operator/host recovery contract | Evidence first, exact package cleanup, immutable ordinary EXP377/392, emergency GPU-hidden image if recovery requires it. |

Asahi distinguishes owned GEM memory from explicit firmware/UAT reservations.
Mu distinguishes lifetimes via EFI types. Windows reuses expired boot storage.
m1n1 currently retains a pre-Mu exclusion with no EBS lifecycle: this is the
contract mismatch to correct. A grant is DMA eligibility, not a CPU stage-2
unmap/remap and not proof of Windows allocation ownership against a malicious OS.

The saved EXP852 assisted R110 contract is the executed reference. Current
standalone `map_stage2` and `j313_standalone_rules` preserve portable RAM/CPU/
IRQ invariants but deliberately allow different software-trap mappings. Keep
these rules and their historical launch snapshots unchanged. A post-EBS record
is a new record, not an edit to `HV_CONTRACT_REGION_FIRMWARE` or the golden file.
The supplied current evidence does not identify a hardware-validated standalone
post-EBS handoff; do not claim that EXP852 validates standalone release. Before
enabling a standalone candidate, bind its last accepted launch artifact/contract
and compare the portable invariants with the same new policy tests.

### Three ways to establish post-EBS truth

1. **Recommended: full final Mu map, consumed through an EL2-controlled firmware
   finalization boundary.** It captures dynamic runtime/ACPI allocations and
   distinguishes the loaded FD from the declared FD tail. Cost: a Mu producer
   and a real trust/lifecycle boundary, not just an m1n1 filter edit.
2. **Static image-bound ownership manifest.** An immutable host manifest may
   establish the maximum FD release window and exclusions; FDF offsets alone
   cannot enumerate relocated runtime images or later ACPI allocations. It is
   sufficient only if a separately proven Mu allocation policy confines *all*
   persistent allocations to fixed protected arenas, forbids persistent aliases
   into the release window, and supplies trusted boot-use completion. Such a
   contract is not present in the inspected R110 evidence. Do not select this
   shortcut or hard-code `0x851420000` as an allowed frame.
3. **Keep all of FD permanently reserved.** Safe conservative fallback for the
   broker; making Windows agree would require an intentional Mu reservation and
   reduce usable RAM. It does not implement narrowing and is not a correction
   to an established Mu defect. Current no-handoff behavior remains unchanged.

A **runtime-only** descriptor list is insufficient: it omits NVS, ACPI reclaim,
non-runtime reservations and holes. Use the complete physical descriptor map
plus explicit surviving objects/references. SetVirtualAddressMap changes runtime
virtual addresses; it neither releases physical runtime pages nor authorizes a
guest replacement map.

### Trusted producer requirement — not solved by an HVC number

Trusted entities are the host-selected firmware policy and EL2. Windows, KMD,
guest mailbox memory and arbitrary EL1 HVC callers are not authorities for
removing exclusions. Pinning Mu's file hash at load is necessary provenance,
but does not protect its mutable DXE map after guest code runs. A CRC, nonce in
guest RAM, signed static FD, expected call PC, or first-caller-wins HVC is not
sufficient: EL1 can forge data or branch into the expected call site. A public
hash is a receipt, not authentication.

The producer must run with **EL2-enforced** control of its code, map input,
stack and mailbox, and with other vCPUs and DMA writers unable to mutate them
until EL2 copies and seals the result. Input must originate from protected Mu
allocation state; freezing a map already corrupted by an untrusted loader is
not sufficient. This can be implemented by an isolated firmware finalizer with
protected allocation bookkeeping, or by an EL2-maintained ownership mirror
whose trusted firmware transitions are enforced throughout the relevant boot
phase. Neither mechanism is demonstrated by the current sources. Merely
trusting Windows' loader to behave would be a weaker trust model and must not
be presented as satisfying this task's “without trusting the guest” requirement.

Recommended producer sequence, as a requirement for that boundary:

1. Reserve fixed-capacity handoff storage while memory services are valid; its
   own pages remain retained. No allocation while committing the final map.
2. Mu validates the actual current MapKey. A failed EBS attempt produces no
   release, including the first attempt that ran BeforeExitBootServices events.
3. Capture all final descriptors and explicit surviving-object exclusions after
   EBS callbacks and memory-protection work have completed. `CoreTerminateMemoryMap`
   itself is too early: `Page.c` explicitly preserves boot contents because EBS
   has more work to do; `CoreExitBootServices` signals callbacks afterward.
4. Transfer through protected finalization code outside the release interval,
   with a stack outside it; EL2 observes successful completion, all boot DMA
   users of the release window stopped, and no subsequent firmware reference
   into it. This is the semantic commit point, not an arbitrary EBS event callback.
5. EL2 validates/copies/normalizes the map and commits one immutable policy before
   any retained-root or v5 activation. Close the producer interface for this
   boot. Runtime calls cannot reopen it. Refuse live policy replacement.

Proposed record fields (new versioned firmware/EL2 interface, not v5 opcodes):
version/length/count/descriptor size and version, EL2 boot generation, pinned
Mu image/policy identity, successful map generation/MapKey, descriptor records
`{Type, PhysicalStart, NumberOfPages, Attribute}`, and explicit retained spans.
Do not dereference guest VirtualStart as a physical address. Validate all
lengths, multiplication/addition overflow, count capacity, 4-KiB alignment,
nonzero spans and overlap. MMIO descriptors outside RAM are legitimate map
entries but never grant candidates. Holes inside a candidate are a refusal.

## TRANSLATION

### Smallest m1n1 policy change

Add bounded normalized release/deny spans and state to the retained backing
policy in `hv_agx_retained_backing.[ch]`, with integration in
`hv_agx_retained_platform.c`. State transitions are `BOOT_DENY -> SEALED_POST_EBS`
or `BOOT_DENY -> RELEASE_DISABLED` on a bad/unsupported handoff; a new EL2 boot
creates a new generation. No reboot/StopDevice/retained-root close may reuse a
previous boot's sealed data. There is no guest-accessible “set allowlist” op.

Let `F` be the image-bound loaded FD interval, `U` the union of complete
reusable coverage in the final map, `D` the union of runtime/ACPI/other retained
descriptors and explicit surviving references, and `P` the independent platform
exclusions. The only new permission is:

`released_frame(p) = SEALED && [p,p+16KiB) subset (F intersect U) && disjoint(D union P)`.

On a FIRMWARE intersection, replace unconditional refusal with that predicate;
other launch-region refusals remain. Apply final retained-map exclusions to
ordinary system grants **throughout guest RAM**, not just within F: relocated
runtime images outside F must not become eligible through the existing generic
RAM path. Preserve the exact existing R64 local-owner path as an explicit
owner-qualified exception; a Reserved descriptor alone never creates that
exception and a system-PFN claim cannot borrow it. The implementation must
demonstrate that distinction through the actual platform dispatch before
enabling a global descriptor filter; do not silently deny all current R64 jobs.

This adds a conservative deny outside F, no new allow there. It does not expand
the release window to the declared FD tail. Runtime/reserved denial in that
tail still applies. A later release of unrelated boot allocations requires a
separate policy decision. Table registration uses the same translator; test it
as well as backing registration, without weakening table/backing collision rules.

Keep `translate_guest`'s complete leaf checks: aligned IPA and PA, guest RAM
containment, PA width, root exclusion, stage-2 resolution at both ends of each
4-KiB quarter, physical contiguity, and ramdisk exclusion. Test aliases by **PA**,
not only by guest IPA. Keep v5 process/allocation generations, rights, shared
grant rules, table collision checks, context0 isolation and TLB completion.
Sealing requires no active roots/grants/jobs; otherwise refuse it. Therefore
no live mappings require selective revocation during this first handoff.

Publish a bounded receipt in EL2: policy version, boot generation, state,
image/map digest, released/protected byte counts, and first refusal category
with IPA/PA/type/intersecting span. Diagnostics and production use the same
eligibility function. Missing/invalid provenance keeps the old FD denial;
failure must not publish half a policy or convert a failed translation into
success. Existing v5 OWNERSHIP remains compatible; map/handoff identity needs
an independently versioned platform capability checked before staging.

### Offline RED-to-GREEN requirements for a later implementation

These tests are proposed, **not written or run in this design task**. Use real C
through the actual platform translator/dispatch, with ASan/UBSan. A synthetic
trusted-producer stub can test policy logic but cannot establish handoff security.

| Gate | Required cases and falsifiable result |
|---|---|
| Backing interval policy | Existing `tests/agx_retained_backing_test.c`/`test_agx_retained_backing.py`: boot FD denied; successful handoff permits complete reusable frames; exact EXP852 frame as regression plus relocated/generated ranges; first/last byte and mixed 4-KiB type boundaries; declared FD tail never accidentally released. |
| Map parser and atomic seal | New real-C tests: short record, version/count/size overflow, zero length, unsorted disjoint entries, overlap, holes, runtime bit on Conventional/BootData, unknown type, fixed-capacity exhaustion. Failure leaves no new permission; copy survives mutation of source buffer. |
| EBS provenance/lifecycle | Failed MapKey and retry, callback after map validation, failed or forged finalization, wrong image/boot generation, PC spoof and mutable-map attack; no release before final trusted completion. Repeated/replayed seal, warm-reset replay and active broker state are denied without policy mutation. Integration must exercise real enforcement, not `trusted=true`. |
| Permanent owners | Runtime code/data and ACPI NVS/reclaim both inside/outside F; heap, EL2/UAT/DART tables, framebuffer, ADT, boot args, TZ and ramdisk aliases remain denied. R64 exact local-owner positive control survives; generic system grant cannot acquire it. |
| v5 real broker | Extend `m1n1_windows/tests/hv_agx_gpuva_v5_test.c` and MMIO tests via `tests/test_gpuva_broker_v5_contract.py`: register before/after handoff, stale process/allocation/boot generation, PA alias, rights, table collision, shared grant, context0 unchanged, revoke/unmap/TLB failure/teardown. No new operation bypasses `translate_guest`. |
| Retained platform integration | Extend `tests/g3_vidmm_replay.py` (extracts actual `translate_guest` and `gpuva_execute`, links broker/backing C), with generated non-identity stage-2 maps. EXP852 protected logical-only frame becomes publishable only after seal; partial/scatter groups stay logical-only. Existing R133 failure propagation remains. |
| Retained/launch regression | Run `test_agx_retained_root.py`, `test_agx_retained_mmio.py`, `test_agx_retained_firmware_io.py`, `test_launch_contract.py`, `test_launch_contract_proxy.py` and affected G3/G4 tests. Both assisted and standalone use the same native policy; old snapshots/golden fields remain unchanged; no Python-only release authority. |

First obtain RED on the current unconditional FD rejection using a valid sealed
fixture; GREEN must retain all negative ownership tests. Separately demonstrate
RED-to-GREEN for the authenticated producer and late-EBS boundary before release
can be enabled. Run the affected suites first and a full host suite once before
an implementation commit; record pre-existing failures rather than declaring a
partial run a full pass. Hash the exact final source tree and firmware pair.

### Future hardware discriminator and recovery

**WHY THIS HYPOTHESIS:**

1. EXP852 dump places `0x851420000` in Windows RAM and VidMm's system-PTE input;
   an invalid guest PFN is less likely than a lifetime mismatch for this frame.
2. Mu's saved log and PEI source label that address BootServicesData, whereas
   the broker source retains the pre-Mu FIRMWARE exclusion. This directly
   explains OWNERSHIP without changing AGX power, IRQ or UAT format.
3. Runtime allocations live elsewhere in the saved log; protecting the whole
   FD is not equivalent to protecting live firmware. A full-map discriminator
   can distinguish reusable FD frames from retained ones without a PFN whitelist.

No run is authorized here. The smallest later checkpoint is a sealed ownership
receipt followed by a **non-executing broker grant/revoke** for an actually
OS-owned, resident complete frame intersecting F, with a retained-frame negative
control refused before any write. Obtain it through supported allocation/residency
and prove final-map membership; do not write an arbitrary physical address or
force the historical PFN. If no eligible frame is allocated, classify the run
inconclusive rather than changing allocator rules during the experiment.

Measure: no grant before seal; release reason/map generation matches the Mu
record; post-seal grant resolves the exact IPA-to-PA frame; retained candidate
returns OWNERSHIP; revoke and teardown leave no grant; context0 stays unchanged.
This proves eligibility only. Actual GPU read/write, completion, invalidation
and stale-access rejection require a subsequent separately authorized checkpoint;
absence of the old bugcheck alone proves neither DMA safety nor a DWM frame.

Before that run, first qualify the producer receipt with unchanged deny policy;
then change only policy activation with the same qualified Mu producer,
diagnostic package and other profiles pinned. Each run gets its own BEFORE/AFTER
ledger entry, exact build/launch commands, manifests, artifact/diff hashes,
expected checkpoint and bounded failure criteria. This prevents bundling a new
Mu trust channel, a new package and a new allow policy into an uninterpretable run.

Failure criteria: provenance disagreement, early/duplicate release, a retained
grant, context0 change, reset/bugcheck, lost pinned SSH or ambiguous frame identity.
Collect the receipt, final descriptor bytes/digest, launch contracts, first
translation refusal, events/ETL and dump before cleanup. Preserve immutable
ordinary GPU-visible EXP377/392 and emergency GPU-hidden images. Stage/arm only
for the direct cold full-owner transition; never boot ordinary with an armed
experimental package. Use the existing evidence-first exact-package rollback,
emergency GPU-hidden cleanup when required, then verify one inert APPL0002 Code28,
no package/service/module/signer/arm, CPU8 and baseline storage/USB health.
Probe both control planes before requesting any physical action. No such probe
or port access occurs in this offline task.

## WHAT IS STILL UNKNOWN

- Exact final descriptor splits and surviving references within F for R110:
  early DXE type4 is strong evidence, not a final interval list. The precise
  retained subset can only be computed from the qualified handoff. No fixed
  retained addresses are invented in this design.
- A non-forgeable Mu-to-EL2 EBS boundary is absent from the demonstrated contract.
  The design specifies its obligations, not an already implemented security
  mechanism. **No safe m1n1-only unconditional FD release is justified today.**
  The next offline deliverable is an enforceable producer/isolation design and
  its adversarial replay, or proof of a static persistent-arena contract.
- Complete enumeration of platform-owned PA ranges beyond the launch region
  array, and owner-qualified R64 behavior under a full descriptor filter, must
  be audited/tested before enabling it. Launch regions are not an EFI map.
- No final generated AML dump, authenticated final map or standalone handoff
  hardware receipt was obtained here. Assisted success cannot fill these gaps.
- Cache coherency and DMA access to newly reusable pages are not established by
  the dump's WriteComb PFN state. No CPU alias/cache type is changed; future DMA
  validation must retain the existing architecture's memory-attribute contract.
- ACPI reclaim and loader-page later reclamation, arbitrary 4-KiB scatter,
  runtime map updates, suspend/resume, hotplug and live resealing are outside
  this first policy. They remain denied where the policy lacks proof.

### Tandem dispositions and document verification

Read main `.local/tandem/REVIEW.md`; review snapshot SHA-256
`753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5`.
The following dispositions address every header still containing OPEN, including
historical rejected/done headers and both R64 entries; they do not reopen old
hardware investigations.

- REVIEW R113: ACCEPT — direct cold full-owner transition remains mandatory.
- REVIEW R111: ACCEPT — preserve R64 CPU memory type; no new CPU alias.
- REVIEW R110: ACCEPT — preserve the dynamic 64-bit ACPI reserve resource.
- REVIEW R109: REJECT — recorded firmware hypothesis already rejected; no new evidence.
- REVIEW R108: DEFER — historical firmware construction defects are outside FD lifetime.
- REVIEW R107: DEFER — old 0x101 attribution is unrelated to this ownership proof.
- REVIEW R106: ACCEPT — durable transitions and reserve ownership remain requirements.
- REVIEW R105: ACCEPT — preserve the EXP836 matrix verdict; no matrix rerun.
- REVIEW R104: REJECT — retain its EXP821 rejection; no cap reinterpretation.
- REVIEW R103: DEFER — AllocateCb harness is beyond this offline design.
- REVIEW R102: DEFER — the old AllocateCb boundary is already crossed.
- REVIEW R100: REJECT — preserve the recorded rejection, no archaeology.
- REVIEW R99: DEFER — old CreateDevice failure is not the current boundary.
- REVIEW R98: DEFER — full-size rendering replay belongs to output validation.
- REVIEW R97: ACCEPT — one current-source design pass, no historical template comparison.
- REVIEW R96: ACCEPT — native builder unchanged; this design owns backing eligibility.
- REVIEW R95: ACCEPT — retain grouping, residency, rights and generation checks.
- REVIEW R94: ACCEPT — firmware objects and process GPU objects remain distinct.
- REVIEW R91: DEFER — no new screen or presentation evidence.
- REVIEW R90: DEFER — panel observations do not affect ownership.
- REVIEW R88: ACCEPT — grant, GPU completion and displayed frame remain separate gates.
- REVIEW R86: DEFER — range-grant scaling is separate from protected-range lifetime.
- REVIEW R85: ACCEPT — bind policy capability, source, profile and artifact identities.
- REVIEW R74: DEFER — context/DMA pool contract remains unchanged.
- REVIEW R71: REJECT — a cap does not prove arbitrary physical contiguity; keep complete-frame checks.
- REVIEW R69: ACCEPT — no live bind or hardware access.
- REVIEW R65: ACCEPT — preserve existing G4 submit/root/fence contract.
- REVIEW R64: ACCEPT — preserve R64 carveout and current G4 ABI (both OPEN entries).
- REVIEW R63: ACCEPT — local reserve never becomes generic reclaimed system RAM.
- REVIEW R57: DEFER — preserve accepted bounded R133; general fatal DDI recovery is separate.
- REVIEW R55: DEFER — repeated StartDevice recovery is not an EBS ownership change.
- REVIEW R54: DEFER — no hardware series started or authorized.
- REVIEW R49: ACCEPT — preserve pinned WDK/caps; no admission changes.
- REVIEW R48: ACCEPT — no ports opened; later launcher exclusivity still required.
- REVIEW R47: ACCEPT — later firmware must pin build profile and ABI; no build here.
- REVIEW R45: ACCEPT — proposed tests include real dispatch and ordered grant/table teardown.
- REVIEW R40: DEFER — historical EL2 watchdog timing is not this lifetime boundary.
- REVIEW R37: DEFER — historical disarmed-start defect does not justify a new Air run.

Verification for this deliverable: source/evidence interval arithmetic, saved
artifact hashes, source-path and Markdown structure checks, OPEN-header coverage,
and explicit-path Git diff/commit inspection. Product tests above are future
implementation gates, not claimed results. Existing dirty submodules and other
thread ledger edits are excluded from this document-only commit.
