# Native read-only V1 Windows integration — offline contract

This source implements an opt-in physical StorPort path. It is not hardware
qualified, and the default project still stops before mapping the broker. No
Air access, installation or launch is authorized by an offline build.

## Inspected primary sources

- Installed WDK 10.0.26100.0 `km/storport.h`, `km/wdm.h`, `shared/nvme.h`.
  Public headers are retained only in the parent `.local/ans-native/references`.
  The C ABI uses the generated kernel-native `ans_u*` types, not user CRT types.
- Parent generated `include/apple_ans_broker_abi.h` and the accepted MMIO request,
  response, generation, pending, cached-CQE and terminal-abort contracts.
- Parent `common/ans_scsi`, `ans_prp`, `ans_order` implementations and tests;
  `documentation/NATIVE_ANS_STORAGE.md` and phases 9–20 of the native plan.
  Their audit records pinned Asahi/m1n1 hardware ownership, Mu exposure and saved
  recovery identities. This driver does not program ANS MMIO, TCB, SART or RTKit.
- Microsoft [HW_BUILDIO](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_buildio),
  [mapping buffers](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/use-of-mapping-buffers-in-the-storport-i-o-model),
  [GetScatterGatherList](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportgetscattergatherlist),
  [GetPhysicalAddress](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportgetphysicaladdress),
  [GetUncachedExtension](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportgetuncachedextension).
- Microsoft [AcquireSpinLock callback/lock tables](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportacquirespinlock),
  [specific AdapterControl table](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_adapter_control),
  [half-duplex limitation](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/half-duplex-mode--not-appropriate-for-shipped-products),
  [QueryPerformanceCounter](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportqueryperformancecounter).
- Microsoft [EnablePassiveInitialization](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportenablepassiveinitialization),
  [InitializeDpc](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportinitializedpc),
  [IssueDpc](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nf-storport-storportissuedpc),
  [HW_DPC_ROUTINE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/nc-storport-hw_dpc_routine).
- [NVMe SCSI Translation Reference 1.5](https://nvmexpress.org/wp-content/uploads/NVM_Express_-_SCSI_Translation_Reference-1_5_20150624_Gold.pdf):
  sections 3.10/3.11/3.13, 6.1.1 and 6.3.3.2. Product uses first 16 MN bytes;
  revision uses last four graphic FR characters; vendor is `NVMe` plus four spaces.
  Current WCE comes from Get Features FID 6, not Identify VWC presence.
  Task-order sources and exact HEAD behavior are recorded by `common/ans_order.h`.

## Ownership and Windows synchronization

Windows owns SRBs, logical task attributes, guest DMA addresses, logical commands,
SCSI responses and request completion. EL2 owns all physical controller/RTKit
lifecycle, hardware queues, physical IRQ, host PRP/TCB publication, mapping checks,
TCB invalidation, stop proof and terminal containment. Hardware performs payload
DMA. The only CPU copies to SRB DataBuffer are bounded SCSI response metadata;
READ payload is never mapped/copied by this driver.

FullDuplex is explicit. BuildIo touches only the supplied SRB/SrbExtension and
reads its Storport-owned SG list; it allocates nothing and accesses no MMIO.
StartIo acquires InterruptLock while already under Storport's StartIo lock.
ResetBus also acquires InterruptLock. Initialize and ISR already hold it. The
specific AdapterControl table specifies InterruptLock for Stop/Restart and takes
precedence over the generic page's broad AdapterControl entry. The shutdown DPC
acquires InterruptLock. No callback recursively acquires a held interrupt lock.

The shared task scheduler admits each task before execution: SIMPLE concurrently,
ORDERED behind all older tasks, HEAD according to the documented queued LIFO and
nonpreemption rules, UNTAGGED only on an empty LU. Unknown attributes are rejected.
Metadata and CHECK CONDITION tasks also obey ordering. INQUIRY CMDQUE is enabled
only with this actual scheduler. ACA, task abort, extended inquiry task-support
bits, hibernation/dump and Fast Startup are not advertised.

## Memory and transport

FindAdapter validates exactly one generated broker memory resource and a present
translated level-sensitive interrupt described by a vector or level. It rejects
a present second vector or level. The portable validator covers both supported
representations, absence and wrong mode. It never equates the translated Windows vector with
the raw ACPI GSIV. Native profiles map only that resource using GetDeviceBase.
One uncached extension contains 64 PRP-list pages and two Identify pages plus
alignment slack. Each 4KiB page's DMA address, alignment and complete physical
span are verified with GetPhysicalAddress. Windows DMA addresses must correspond
to guest IPA in the accepted Mu/EL2 DMA model; the profile cannot be transplanted
to an unrelated DMA-remapped platform by assuming its bus addresses are IPA.

PRP pages are per-tag, immutable while deferred/selected/in flight/quarantined,
and reused only after authenticated completion/reap or stopped retirement. SG
conversion and read-only packet construction are pure tested helpers. The driver
supports 128KiB requests, 4KiB LBAs and at most one PRP list. Identify rejects an
unsupported smaller nonzero MDTS, metadata/protection formats or invalid size.

All wire exchanges are serialized. The request is fully written in aligned DWORDs,
then a full barrier precedes the low-32-bit sequence doorbell. The complete
response is read twice with a barrier and must match sequence, generation, queue,
tag, result and state. Sequence cannot wrap. RESET/RELEASE alone may advance
exactly one generation, with a matching header snapshot and sequence reset to zero.
Torn, stale, changed-generation or faulted responses quarantine local ownership.
A nonfaulted authenticated prepublication rejection ends a task without DMA.

The PENDING low-DWORD read drains physical CQs and latches the 64-bit bitmap;
the following high-DWORD read returns the same snapshot. The ISR validates
cached CQE identity, submits REAP, compares the echoed CQE, then retires the task
and completes its SRB. It drains multiple completions and dispatches newly eligible
queued tasks. It never waits for a completion. Hardware CQEs do not carry software
epochs; actual controller stop/reset must exclude old DMA and late completions.

## Initialization and qualification ladder

`AppleAnsNativeReadOnly=false` (default) remains resource-only and deliberately
returns FALSE from Initialize before broker access. Native profiles use the same
source with explicit `AppleAnsQualificationStop`:

- `1`: QUERY → logical CLAIM → stop/release; no physical controller boot.
- `2`: REGISTER_QUEUES performs bounded EL2 admin boot → stop before Identify.
- `3`: Identify Controller and Namespace → stop before Get Features/queue creation.
- `0`: full read-only candidate, still not installed or hardware qualified.

The full path performs QUERY, CLAIM, REGISTER_QUEUES, both Identify commands,
current VWC Get Features when supported, Create I/O CQ (PC+IEN) and Create I/O SQ.
Only actual successful CQEs permit progress. Admin polling is initialization-only,
limited by a one-second performance-counter interval and a finite iteration cap.
The physical namespace is ready before Initialize returns TRUE. Only registration
of the preallocated shutdown DPC is deferred to PassiveInitialize, as required by
Storport. The callback performs no deferred controller boot or fake readiness.

RCD=0 is explicit SNT policy: READ does not request a cache-disable/bypass operation;
MODE SELECT and changeable cache settings are unsupported. The NVMe translation
reference leaves other caching fields unspecified. RCD is not claimed as a
measured hardware bit. WCE is obtained from the actual current feature value, or
false when Identify states no volatile write cache exists.

## Shutdown, reset and failure containment

Shutdown is a software ordering fence and stops new admissions. Its NVMe FLUSH is
asynchronous; CQE+REAP schedules the already allocated DPC. The DPC obtains real
QUIESCE/RELEASE receipts before completing SHUTDOWN. Success requires both FLUSH
success and stop proof. A rejected/failed FLUSH still produces failure after stop,
never a synthesized successful flush.

Healthy CLAIMED ownership is first QUIESCED before RESET; the FAULTED recovery
path can reset directly. The actual Windows reset body is regression-tested against
the actual EL2 service, which rejects a direct healthy CLAIMED reset.
RESET must return stopped retirement and exactly the next generation before any
old SRB is failed or its metadata reused. Old tasks complete with BUS_RESET, then
actual controller/namespace initialization is repeated. The current backend can
prove only graceful drained stop. There is no invented forced-abort receipt.

On missing or uncertain stop proof, this driver writes the generated one-way
ANS_ABORT_OFFSET. EL2 must nonreturningly hold all guest vCPUs and preserve all
pinned backing. If the abort write unexpectedly returns, the callback deliberately
never returns to Storport, which would otherwise free/reuse DMA memory. This is a
terminal platform-contract failure, not successful reset, memory retirement or a
Windows bug check hiding a hypervisor ownership defect. Actual all-vCPU containment
remains a platform/hardware qualification gate; a pointer retained in an extension
alone is not DMA lifetime protection.

## Read-only telemetry

[TELEMETRY.md](TELEMETRY.md) defines the validated SRB_IO_CONTROL snapshot query,
Windows counter semantics and optional separate broker record. Counters are
preallocated, updated under the existing lock and never initiate hardware work.
Deferred software tasks are not counted as submitted NVMe commands. The exposed
QD maximum comes from confirmed broker admissions minus authenticated completions.

## Verification boundary

Portable ASan/UBSan tests cover request packing, poisoned non-dereferenceable
payload addresses, namespace parsing, response snapshot/generation/sequence rules,
shared SCSI/SG translation and task-order/epoch lifetime. Actual WDK builds and
analysis require source freeze plus parent ANSBUILD002 preregistration. Prior
ANSBUILD001-H evidence applies only to the older qualification skeleton, not this
native implementation. Signature trust remains pending under the unchanged-store
rule. No offline test is a native boot or storage proof.

The first eventual hardware checkpoint remains parent ANS001 (external WinPE,
ANS0 unbound, no package/no disk commands); ANS002–004 remain separate bounded
qualification profiles. The preserved ordinary G2 assisted pair is recovery.
