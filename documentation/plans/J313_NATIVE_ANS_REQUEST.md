# J313 / T8103 — Native Windows Apple ANS Storage Plan

Цель проекта:

Реализовать нативный Windows storage path для внутреннего Apple ANS SSD на J313 / T8103.

Конечная Windows-схема:

`Windows → storport.sys → AppleAns.sys → Apple ANS → SSD`

Текущий virtual NVMe сохранить как recovery path, но в native runtime он не должен участвовать.

## NON-NEGOTIABLE ARCHITECTURE

Использовать физический StorPort miniport.

НЕ использовать StorPort virtual miniport.

НЕ пытаться заставить inbox `stornvme.sys` напрямую управлять Apple ANS.

НЕ отдавать Windows unrestricted ownership над:

* SART;
* RTKit private memory;
* NVMMU TCB tables;
* firmware-private mappings.

Первый production architecture:

### WINDOWS OWNS

* StorPort adapter;
* request lifecycle;
* SCSI→NVMe command translation;
* command tags;
* logical SQ commands;
* CQ processing;
* timeouts;
* completion of SRBs;
* scatter/gather extraction;
* logical PRP construction;
* disk/namespace presentation;
* flush/write/read semantics.

### M1N1 / EL2 OWNS

* ANS RTKit lifecycle;
* SART;
* NVMMU;
* hardware TCB arrays;
* guest IPA → host PA validation/translation;
* hardware PRP shadow construction when required;
* controller reset/quiesce;
* protected command authorization;
* physical ANS interrupt routing to the guest.

### HARDWARE OWNS

* actual SSD execution;
* DMA;
* CQ production.

Normal READ/WRITE data must NOT pass through a bounce buffer.

Desired transfer:

`ANS DMA ↔ Windows data pages`

m1n1 may inspect/translate descriptors but must not memcpy payload data.

---

# PHASE 0 — FREEZE CURRENT RECOVERY BASELINE

Before changing storage architecture:

1. Record current known-good:

   * root commit;
   * m1n1 commit;
   * Mu commit;
   * current virtual NVMe hardware proof;
   * current Windows boot proof.

2. Preserve ordinary virtual-NVMe G2 as immutable recovery baseline.

3. Add storage modes:

`VNVME_RECOVERY`
`ANS_NATIVE`

4. Never allow both storage frontends to own ANS simultaneously.

Required invariant:

`exactly one storage owner`

At runtime:

`VNVME_RECOVERY`
→ current hv_pci + hv_nvme + stornvme path.

`ANS_NATIVE`
→ no Windows-visible emulated NVMe controller;
→ ACPI Apple ANS device;
→ AppleAns.sys.

5. Add:

`config/j313-ans.json`

This becomes the single machine-readable J313 ANS contract.

Store there:

* ACPI HID;
* ACPI UID;
* broker guest resource;
* guest ANS GSIV;
* ADT paths:

  * `/arm-io/ans`
  * `/arm-io/sart-ans`;
* queue-depth contract;
* hardware generation;
* broker ABI version.

Hardware MMIO/IRQ values must come from live ADT/current source, not guessed literals.

Generate ASL/config headers from this file.

6. Create:

`documentation/NATIVE_ANS_STORAGE.md`

and keep durable results in the existing:

`investigation/EXPERIMENTS.md`
`investigation/CHANGES.csv`

Follow existing AGENTS.md evidence discipline.

---

# PHASE 1 — DEFINE THE EL2 ANS BROKER ABI

Create a versioned ABI shared between m1n1 and AppleAns.sys.

Suggested files:

m1n1:

`src/hv_ans_broker.c`
`src/hv_ans_broker.h`
`src/hv_ans_broker_abi.h`

Windows:

`drivers/storage/appleans/include/apple_ans_broker_abi.h`

Generate both ABI headers from one canonical definition if practical.

Do not maintain two manually diverging copies.

## Broker header

Define:

* magic;
* ABI major;
* ABI minor;
* structure size;
* feature bits;
* firmware/controller generation;
* state;
* last error;
* sequence number.

States:

`OFF`
`BOOTING`
`READY`
`CLAIMED`
`QUIESCING`
`FAULTED`

## Minimum operations

Implement:

`QUERY`
`CLAIM`
`REGISTER_QUEUES`
`AUTHORIZE_SUBMIT`
`REAP_COMPLETION`
`QUIESCE`
`RESET`
`RELEASE`

All commands carry:

* ABI version;
* generation;
* monotonically changing request sequence;
* command tag where relevant.

Reject stale generation.

## Tag lifecycle

Implement explicit state machine:

`FREE`
→ `PREPARED`
→ `IN_FLIGHT`
→ `COMPLETED`
→ `FREE`

Fail closed on:

* double submit;
* reap before completion;
* tag reuse while in flight;
* stale generation;
* invalid queue;
* malformed PRP;
* unmapped guest page;
* forbidden physical address;
* unsupported opcode;
* invalid length;
* direction mismatch.

Never silently repair a malformed request.

## Broker security contract

Windows gives guest physical/IPA addresses.

ANS requires real host/device-visible addresses.

Therefore m1n1 must:

1. walk the complete Windows PRP/SG description;
2. translate every guest IPA using the authoritative stage-2 mapping;
3. reject anything not backed by valid guest RAM;
4. verify access direction;
5. construct the hardware-visible PRP representation;
6. construct/program the NVMMU TCB;
7. publish DMA barriers;
8. only then trigger the ANS tag.

Never program a guest IPA directly into ANS unless hardware evidence explicitly proves that the address is already the correct device address.

---

# PHASE 2 — REFACTOR CURRENT M1N1 ANS DRIVER

Current `src/nvme.c` already understands:

* ANS RTKit;
* SART;
* controller boot;
* 64-entry queues;
* NVMMU TCB;
* linear SQ;
* Apple tag doorbells;
* completion;
* shutdown/reset.

Do not rewrite this from scratch.

Refactor it into:

### controller ownership

`ans_controller_boot()`
`ans_controller_shutdown()`
`ans_controller_reset()`

### queue ownership

`ans_queue_prepare()`
`ans_queue_destroy()`

### native broker

`ans_native_register_queues()`
`ans_native_authorize_tag()`
`ans_native_reap_tag()`

### compatibility wrappers

Keep existing:

`nvme_read()`
`nvme_write()`
`nvme_read_blocks()`
`nvme_write_blocks()`
`nvme_flush()`

working on top of the refactored common backend.

This guarantees virtual recovery does not regress.

## Remove tag-0 assumption from common backend

The current synchronous implementation effectively uses one tag and polls.

Native runtime must support all usable hardware tags concurrently.

Implement a tag allocator compatible with T8103 shared tag space.

Treat admin and I/O queue tags as one shared namespace.

Reserve admin tags explicitly.

Do not assume admin and I/O may reuse the same numeric tag simultaneously.

Use the exact current T8103 limit from source/live hardware.

## Hardware queue contract

Keep:

* one admin queue;
* one I/O queue;
* one shared hardware IRQ.

Do NOT invent multiple hardware queues.

Performance comes from multiple outstanding commands within the single hardware queue.

---

# PHASE 3 — IMPLEMENT SHADOW PRP / NVMMU TRANSLATION

This is a critical layer.

Windows physical addresses are guest physical addresses.

They cannot be trusted as ANS host/device addresses.

For every command:

Windows:

`StorPort SG`
→ build logical PRP description
→ command stored with guest addresses.

m1n1:

`guest PRP`
→ walk
→ IPA→PA
→ validate
→ construct host PRP representation
→ hardware SQE/TCB
→ submit.

## Initial limits

Start conservatively:

`MaximumTransferLength = 128 KiB`

Keep the currently proven 4-KiB LBA model.

Support:

* PRP1;
* PRP2 direct second page;
* one PRP list.

Do not start with 4-MiB chained PRP lists.

After read/write stability:

128 KiB
→ 512 KiB
→ 1 MiB
→ up to proven hardware limit.

Each increase is a separate deterministic change.

## Required tests

Synthetic stage-2 maps must test:

* one aligned 4K page;
* unaligned PRP1;
* exactly two pages;
* 128K fragmented transfer;
* crossing 4GB boundary;
* invalid IPA;
* partially unmapped buffer;
* malformed PRP list;
* recursive/invalid PRP pointer;
* PRP pointing into protected m1n1 memory;
* write direction;
* read direction;
* zero-length invalid command;
* maximum valid transfer.

Private m1n1/firmware regions must always be rejected.

---

# PHASE 4 — QUEUE MEMORY V1

For first hardware proof, favor correctness over zero-copy queue metadata.

Payload must already be zero-copy.

It is acceptable in V1 for m1n1 to own:

* hardware SQ backing;
* hardware CQ backing;
* hardware TCB backing.

Windows may communicate request metadata through the broker.

Copying:

64-byte command
or
16-byte completion

is acceptable for V1.

Copying READ/WRITE payload is NOT acceptable.

This gives the first target:

`zero-copy payload + brokered command metadata`.

Once this works, move to V2.

---

# PHASE 5 — QUEUE MEMORY V2

After V1 is stable, remove command/completion mediation where possible.

Target:

Windows-visible DMA common buffers become actual hardware SQ/CQ backing.

Use StorPort DMA/common-buffer APIs.

For any Windows-provided queue physical address:

m1n1 must verify:

* full range mapped;
* physical translation;
* alignment;
* device visibility;
* lifetime.

If the hardware requires host physical contiguity and Windows guest physical memory does not preserve it through stage-2 translation, fail closed rather than assuming contiguity.

The broker may continue owning only:

* TCB;
* host PRP shadow;
* tag authorization;
* TCB invalidation.

Target fast path:

`AppleAns.sys writes SQE`
→ `AUTHORIZE_SUBMIT(tag)`
→ m1n1 patches/creates host translation + TCB
→ hardware executes
→ hardware CQE
→ Windows ISR
→ `REAP_COMPLETION(tag)`
→ StorPort completion.

No payload memcpy.

---

# PHASE 6 — PHYSICAL INTERRUPT PATH

Do not reuse the current virtual NVMe INTx state machine.

ANS_NATIVE gets a separate interrupt route.

Use live J313 ADT to identify the physical ANS completion interrupt.

m1n1 owns physical AIC reception and translates it to one stable guest GSIV.

Mu ACPI advertises that guest GSIV.

Windows `AppleAns.sys` receives a normal level-sensitive interrupt.

One hardware interrupt is shared by admin + I/O completion queues.

ISR must drain all available CQEs before leaving.

Do not require one IRQ per completion.

Counters:

* physical ANS IRQs;
* guest IRQ injections;
* ISR entries;
* CQEs processed;
* CQEs per IRQ;
* spurious IRQs;
* missed-IRQ polling recoveries.

There must be no dependency on the current synthetic PCI INTx delivery object.

---

# PHASE 7 — MU / ACPI ENUMERATION

Create generated file:

`Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAns.asl.inc`

Include it from DSDT.

Define:

`Device(ANS0)`

Canonical HID:

`APPLANS0`

UID:

`0`

Add:

`_CCA = 1`

Expose only the resources Windows is allowed to access.

At minimum:

* broker MMIO/control page;
* guest interrupt resource.

If direct safe ANS MMIO ranges are exposed later, add only explicitly allowed ranges.

Do not expose:

* SART programming region;
* raw NVMMU tables;
* private RTKit memory.

## Native vs recovery enumeration

VNVME_RECOVERY:

`ANS0._STA = 0`

ANS_NATIVE:

`ANS0._STA = 0xF`

The existing emulated PCI root can remain for architectural compatibility, but in ANS_NATIVE there must be no NVMe 00:00.0 device for Windows to bind.

A Windows query must show:

`AppleAns.sys present`
`stornvme virtual device absent`

---

# PHASE 8 — UEFI → WINDOWS STORAGE HANDOFF

Do not require a native ANS UEFI driver for V1.

Keep the existing virtual NVMe usable during UEFI/winload.

This solves bootstrapping.

Sequence:

m1n1 starts in:

`UEFI_VNVME`

Mu/Windows boot manager loads:

* bootmgfw;
* winload;
* kernel;
* HAL;
* boot-start AppleAns.sys

through the existing virtual NVMe.

At ExitBootServices in ANS_NATIVE profile:

Mu calls a new m1n1 storage handoff operation:

`PREPARE_ANS_NATIVE_HANDOFF`

m1n1 then:

1. stops accepting new virtual NVMe commands;
2. drains outstanding operations;
3. flushes writes;
4. cleanly shuts down the native ANS controller;
5. shuts down RTKit cleanly;
6. leaves controller disabled;
7. invalidates old queue generation;
8. marks virtual PCI NVMe as absent;
9. arms ANS native broker;
10. returns success.

Only after success may ExitBootServices continue.

This intentionally hands Windows a clean disabled ANS device.

Do not retain stale UEFI queue ownership into Windows.

## Failure

If handoff cannot prove:

`drained + flushed + disabled`

fail ANS_NATIVE boot before ExitBootServices.

Never leave both frontends alive.

Recovery remains:

`VNVME_RECOVERY`.

---

# PHASE 9 — CREATE WINDOWS DRIVER PROJECT

Create:

`drivers/storage/appleans/`

Suggested structure:

`AppleAns.vcxproj`
`AppleAns.inf`
`driver.c`
`adapter.c`
`adapter.h`
`queue.c`
`queue.h`
`prp.c`
`prp.h`
`srb.c`
`srb.h`
`scsi.c`
`scsi.h`
`broker.c`
`broker.h`
`reset.c`
`reset.h`
`trace.c`
`trace.h`
`hw.h`
`include/apple_ans_broker_abi.h`

Use pinned:

`WDK 10.0.26100.0`

Target:

`ARM64`

No KMDF dependency.

This is a physical StorPort miniport.

---

# PHASE 10 — INF / BOOT DRIVER CONTRACT

INF class:

`SCSIAdapter`

Bind:

`ACPI\APPLANS0`

Service:

`AppleAns`

Binary:

`AppleAns.sys`

Start type:

`SERVICE_BOOT_START`

Service group:

`SCSI Miniport`

Use standard StorPort miniport INF structure from the pinned WDK contract.

Do not invent a custom storage class.

Build gates:

* ARM64 PE;
* imports only expected kernel/StorPort APIs;
* InfVerif GREEN;
* Code Analysis GREEN;
* signing GREEN;
* `signtool verify` GREEN.

---

# PHASE 11 — DRIVERENTRY

`DriverEntry`

must only:

1. initialize deterministic global constants;
2. fill zeroed `HW_INITIALIZATION_DATA`;
3. register:

   * HwStorFindAdapter;
   * HwStorInitialize;
   * HwStorBuildIo;
   * HwStorStartIo;
   * HwStorInterrupt;
   * HwStorResetBus;
   * HwStorAdapterControl;
4. configure:

   * DeviceExtensionSize;
   * SrbExtensionSize;
   * NumberOfAccessRanges;
   * MapBuffers/DMA contract as required by current WDK;
5. call `StorPortInitialize`.

No hardware access from DriverEntry.

No registry-dependent hardware initialization.

---

# PHASE 12 — HWSTORFINDADAPTER

Use only PnP-supplied resources.

Map assigned resources with StorPort-supported APIs.

Validate:

* expected broker resource exists;
* length is exact or compatible ABI size;
* interrupt exists;
* no duplicate resource;
* ABI magic/version valid.

Set conservative initial capabilities:

* one bus;
* one target;
* one LUN;
* scatter/gather enabled;
* bus-master DMA;
* full 64-bit DMA if the actual platform contract permits it;
* `MaximumTransferLength = 128 KiB`;
* NumberOfPhysicalBreaks sufficient for 128K fragmented I/O;
* tagged queuing enabled.

Allocate queue/common state using StorPort-supported DMA APIs.

No pageable allocations in runtime storage path.

---

# PHASE 13 — HWSTORINITIALIZE

The boot disk must be fully reachable before returning success.

Sequence:

`broker QUERY`
→ validate ABI
→ `CLAIM`
→ verify fresh generation
→ setup admin queue
→ controller/RTKit boot through broker
→ ANS boot-status validation
→ controller disable/configure/enable sequence
→ admin queue ready
→ IDENTIFY controller
→ IDENTIFY namespace
→ create I/O CQ
→ create I/O SQ
→ I/O queue ready
→ enable interrupt handling
→ return TRUE.

Do not return success with a partially initialized namespace.

Record exact failure stage in crash-durable diagnostics.

Stages should have stable numeric IDs.

Example:

1 broker-query
2 broker-claim
3 rtkit
4 ans-boot
5 controller-disable
6 admin-memory
7 admin-enable
8 identify-controller
9 identify-namespace
10 io-cq
11 io-sq
12 interrupt
13 ready

---

# PHASE 14 — ADMIN QUEUE ENGINE

Implement the admin queue before normal disk I/O.

Required initial commands:

* Identify Controller;
* Identify Namespace;
* Create I/O Completion Queue;
* Create I/O Submission Queue.

Later:

* Delete SQ;
* Delete CQ;
* Get Log Page if useful.

No AER dependency initially if unsupported by T8103 contract.

Admin request timeout must cause:

`FAULTED`
→ controlled broker reset.

Never fake successful admin completion.

---

# PHASE 15 — SCSI → NVME TRANSLATION

StorPort/classpnp will present SRBs/CDBs.

Implement minimum logical disk behavior.

Required:

### discovery

* INQUIRY;
* TEST UNIT READY;
* REPORT LUNS;
* READ CAPACITY(10);
* READ CAPACITY(16).

### data

* READ(10);
* READ(16);
* WRITE(10);
* WRITE(16).

### cache

* SYNCHRONIZE CACHE(10);
* SYNCHRONIZE CACHE(16).

### basic compatibility

* MODE SENSE required by Windows disk stack;
* REQUEST SENSE semantics;
* START STOP UNIT with truthful supported behavior.

Unsupported commands:

return correct SCSI status/sense.

Do not report support and silently ignore behavior that affects persistence.

TRIM/UNMAP is NOT part of first writable milestone.

---

# PHASE 16 — HWSTORBUILDIO

Do as much preprocessing here as StorPort permits.

For READ/WRITE:

1. validate CDB;
2. calculate LBA;
3. calculate block count;
4. overflow-check;
5. enforce namespace bounds;
6. call `StorPortGetScatterGatherList`;
7. validate SG count;
8. encode logical guest PRPs;
9. store prepared command state in SrbExtension.

Do not touch hardware.

Do not allocate large dynamic memory per I/O.

SrbExtension owns request-local metadata.

---

# PHASE 17 — HWSTORSTARTIO

Must be extremely short.

StorPort may deliver another I/O after it returns.

Sequence:

1. obtain free hardware tag;
2. bind `tag → SRB`;
3. copy prepared command to queue slot;
4. DMA write barrier;
5. call `AUTHORIZE_SUBMIT(tag)`;
6. if accepted:
   `IN_FLIGHT`;
7. return.

Do not synchronously wait for SSD completion.

Do not poll the hardware here.

No 10-second operation inside HwStorStartIo.

This is the primary difference from current m1n1 storage implementation.

---

# PHASE 18 — ISR / COMPLETION

HwStorInterrupt:

1. inspect admin CQ;
2. inspect I/O CQ;
3. while phase indicates a new CQE:

   * acquire tag;
   * validate it is IN_FLIGHT;
   * record NVMe status;
   * advance CQ head/phase;
   * call broker `REAP_COMPLETION`;
   * release host PRP/TCB mapping;
   * mark request completed;
   * notify StorPort RequestComplete.
4. publish CQ head.
5. return whether interrupt belonged to adapter.

Drain multiple completions per interrupt.

Never require:

`1 command = 1 IRQ`.

Detect:

* unknown tag;
* duplicate CQE;
* invalid phase;
* CQ overflow;
* completion for stale generation.

Any impossible queue state must fault the controller instead of corrupting another SRB.

---

# PHASE 19 — READ-ONLY QUALIFICATION MODE

The first hardware drivers must be compile-time/runtime read-only.

Broker and Windows driver BOTH enforce this.

Reject:

* WRITE;
* DSM/UNMAP;
* destructive admin operations outside queue setup.

Allow:

* Identify;
* READ;
* queue lifecycle;
* non-destructive status.

Do not rely on Windows policy alone.

m1n1 must independently reject a write in qualification mode.

---

# PHASE 20 — WINPE HARDWARE LADDER

Do NOT first test native storage by trying to boot C:.

Use WinPE booted independently from USB/RAM.

This separates:

`driver can access internal ANS`

from:

`Windows can boot from AppleAns`.

## EXP ANS001 — ENUMERATION ONLY

WinPE external boot.

ANS_NATIVE platform.

No AppleAns package.

Expected:

* virtual NVMe device absent;
* ANS0 ACPI device present Code28;
* internal disk inaccessible;
* USB/CPU/input stable.

No hardware mutation.

## EXP ANS002 — DRIVER LIFECYCLE ONLY

Install AppleAns with qualification stop before controller boot.

Require receipts:

`DriverEntry`
`HwFindAdapter`
`HwInitialize entered`
`broker QUERY`
`broker CLAIM`

STOP.

No disk commands.

## EXP ANS003 — ANS BOOT / ADMIN READY

Allow:

RTKit
→ ANS boot
→ controller admin queue.

STOP before Identify.

Require:

* correct boot status;
* no RTKit crash;
* controller ready.

## EXP ANS004 — IDENTIFY

Execute only:

Identify Controller
Identify Namespace.

Compare with values collected through current working m1n1 virtual path:

* namespace block count;
* LBA size;
* serial/model data where available.

STOP.

## EXP ANS005 — SINGLE DIRECT READ

Read known non-destructive LBAs:

* GPT header;
* partition table area.

Calculate SHA-256.

Compare against the exact same LBA captured through known-good virtual storage.

No filesystem mount needed.

PASS requires byte-identical data.

This is the first proof of:

`Windows → AppleAns.sys → ANS → SSD`.

## EXP ANS006 — WINDOWS DISK ENUMERATION

Expose the namespace to classpnp.

Keep disk read-only/offline where practical.

Require:

* Get-Disk sees correct capacity;
* GPT parses;
* partitions match;
* no write;
* no Event129;
* no reset.

## EXP ANS007 — READ STRESS

Disk still non-destructive.

Run:

* QD1;
* QD4;
* QD16;
* QD32.

Measure:

* commands;
* completions;
* IRQs;
* maximum outstanding;
* timeouts;
* resets;
* MB/s;
* IOPS.

PASS requires actual concurrent outstanding commands > 1.

---

# PHASE 21 — FIRST WRITE

Only after all read-only experiments pass.

Use a dedicated sacrificial test area/partition.

Do NOT write:

* GPT;
* EFI System Partition;
* Windows partition;
* recovery partition.

Before test:

save exact preimage.

Test:

1. read preimage;
2. SHA-256;
3. write deterministic 4K pattern;
4. FLUSH;
5. read back;
6. compare;
7. restore original bytes;
8. FLUSH;
9. read restored bytes;
10. compare with original SHA-256.

Only then mark WRITE proven.

Then repeat:

4K
→ 64K
→ 128K.

---

# PHASE 22 — FILESYSTEM QUALIFICATION

Create/use a sacrificial test partition.

Run:

* create filesystem;
* small files;
* large sequential file;
* random files;
* metadata-heavy workload;
* flush;
* delete;
* remount;
* checksum verification.

Do not use the real Windows volume yet.

Run sustained workload.

Required:

* zero Event129;
* zero unexpected controller reset;
* zero data mismatch;
* zero CQ/tag leak.

---

# PHASE 23 — NATIVE BOOT PREPARATION

Only after WinPE native READ+WRITE is proven.

Offline-install AppleAns.sys into real Windows.

Requirements:

* BootStart;
* signed;
* INF in DriverStore;
* service registry correct;
* binary hash recorded.

Disable initially:

* Fast Startup;
* hibernation.

Preserve exact BCD/system recovery.

Keep a VNVME_RECOVERY boot profile.

Never remove virtual recovery until native has long-term proof.

---

# PHASE 24 — FIRST NATIVE WINDOWS BOOT

Boot process:

UEFI uses temporary vNVMe
→ winload loads boot-start AppleAns
→ ExitBootServices native handoff
→ vNVMe becomes absent
→ kernel starts
→ StorPort loads AppleAns
→ ANS native controller starts
→ root volume discovered
→ Windows continues boot.

Primary PASS:

* Windows login/SSH reached;
* disk is backed by AppleAns;
* no virtual QEMU/m1n1 NVMe device;
* AppleAns service running;
* correct physical disk;
* 8/8 CPUs;
* input works;
* xHCI works;
* no Event129;
* no Event41;
* no bugcheck;
* no storage reset.

If Windows returns:

`INACCESSIBLE_BOOT_DEVICE`

do not retry unchanged.

Use WinPE/recovery evidence to establish the exact last driver stage.

---

# PHASE 25 — RESET / TIMEOUT RECOVERY

After normal native boot works, implement truthful recovery.

Timeout path:

1. poll CQ once to detect missed IRQ;
2. if command completed:
   complete normally;
3. otherwise:
   freeze new submission;
4. broker QUIESCE;
5. drain whatever is provably completed;
6. reset controller;
7. new generation;
8. rebuild queues;
9. fail requests whose completion cannot be proven;
10. resume.

Never report an unknown write as successful.

Never reuse old generation tags after reset.

---

# PHASE 26 — SHUTDOWN

Handle StorPort shutdown.

Sequence:

stop new I/O
→ drain
→ FLUSH
→ delete queues if required
→ controller shutdown
→ RTKit clean shutdown
→ broker RELEASE.

Record durable:

`clean_storage_shutdown = true`

only after all required stages succeed.

---

# PHASE 27 — RESTART

Restart must always create a fresh generation.

Old:

* tags;
* PRP shadows;
* TCBs;
* queue heads;
* queue phases

must never survive a real controller reset as active state.

Add tests for multiple complete lifetimes:

start
→ I/O
→ stop
→ start
→ I/O.

---

# PHASE 28 — FAST STARTUP / HIBERNATION

Do this only after clean shutdown/restart is stable.

Implement:

`ScsiStopAdapter`
`ScsiRestartAdapter`

and required hibernation storage behavior.

Treat:

normal boot
and
resume boot

as different lifecycle inputs.

Never reuse stale queue/TAG/RTKit state solely because RAM content was restored.

Broker generation is authoritative.

If hibernation image contains generation N but broker reports N+1:

driver must rebuild controller state.

Do not enable Fast Startup by default until this passes repeated hardware testing.

---

# PHASE 29 — CRASH DUMP

Late milestone.

AppleAns is boot storage, so crash dump path eventually matters.

Implement explicit dump mode detection.

In dump mode:

* no normal dynamic infrastructure assumptions;
* no DPC dependency;
* minimal preallocated memory;
* boot device available by completion of HwStorInitialize;
* one command at a time is acceptable;
* polling completion is acceptable if required;
* no RTKit functionality depending on pageable/user runtime.

Maintain a deliberately simple dump path even if normal runtime is async.

Test only after ordinary native storage is proven.

---

# PHASE 30 — PERFORMANCE V2

Only optimize after correctness.

## A. Queue depth

Hardware only has one I/O queue.

Exploit outstanding tags.

Target practical QD:

4
→ 16
→ 32
→ maximum proven safe hardware depth.

Do not emulate nonexistent hardware queues.

## B. Maximum transfer

Start 128K.

Then test:

512K
1M
4M

only when PRP chaining is proven.

## C. ISR batching

Always drain all available CQEs per IRQ.

Collect:

`cqes / irq`.

## D. Broker batching

Once correctness is proven, reduce EL2 exits.

Instead of:

one trapped AUTHORIZE write per command,

support a shared authorization ring:

Windows prepares tags:
3,4,5,6

then one broker doorbell:

`producer = 4`

m1n1 validates/submits all four.

Likewise batch REAP.

Do not introduce batching before single-command semantics are solid.

## E. Translation overhead

Measure:

* SG build;
* IPA→PA translation;
* PRP shadow build;
* broker entry;
* hardware latency.

Only optimize measured bottlenecks.

## F. Direct queue DMA

If V1 still copies SQE/CQE metadata, V2 removes those copies.

Payload must already be zero-copy before this phase.

---

# PHASE 31 — OPTIONAL FULL WINDOWS OWNERSHIP

NOT required for the first production-native driver.

Only investigate after brokered-native path is stable.

Possible future target:

Windows directly owns:

* RTKit;
* SART;
* NVMMU;
* TCB.

Then m1n1 storage involvement could disappear entirely.

But do NOT do this merely for architectural purity.

If EL2 broker overhead is negligible, keeping privileged NVMMU validation in m1n1 is acceptable and closely matches the hardware's intended security split.

---

# DRIVER TELEMETRY

Expose counters at minimum:

`starts`
`stops`
`resets`
`generation`
`submitted`
`completed`
`reads`
`writes`
`flushes`
`bytes_read`
`bytes_written`
`max_outstanding`
`current_outstanding`
`irq_count`
`cqes`
`cqes_per_irq`
`timeouts`
`poll_recovered_completions`
`invalid_cqe`
`invalid_tag`
`broker_rejections`
`prp_translation_failures`
`tcb_inval_failures`

m1n1 exposes corresponding broker-side counters.

Hardware reports must compare both sides.

---

# OFFLINE TEST MATRIX

Before every hardware candidate:

m1n1 tests:

* tag lifecycle;
* queue wrap;
* CQ phase wrap;
* shared admin/IO tag namespace;
* PRP translation;
* malformed PRP;
* protected memory rejection;
* stale generation;
* reset with outstanding request;
* double completion;
* double submit;
* broker ABI mismatch;
* read-only mode rejecting writes;
* second controller lifetime.

Windows common-code tests:

* READ10/16 parsing;
* WRITE10/16 parsing;
* capacity;
* overflow;
* namespace boundary;
* SCSI sense mapping;
* SG→PRP;
* CQ status→SRB status;
* tag allocator;
* reset cleanup.

Build:

* ARM64 release;
* WDK 26100;
* code analysis;
* InfVerif;
* signing verification.

No hardware run until all relevant deterministic gates are GREEN.

---

# HARDWARE EXPERIMENT RULE

Every experiment changes one independently observable variable.

Every experiment must contain before execution:

`HYPOTHESIS`
`ONE VARIABLE`
`EXPECTED PASS`
`EXPECTED FAIL`
`COMMITS`
`HASHES`
`ROLLBACK`
`EVIDENCE PATH`

After execution:

`OBSERVED`
`VERDICT`
`FIRST UNKNOWN`
`NEXT CAUSAL STEP`

No blind retry.

Wrong launch / sandbox denial before test boundary is not a hardware rejection.

---

# RECOVERY CONTRACT

At all times preserve:

`VNVME_RECOVERY`

This must remain able to boot the existing Windows installation independently from AppleAns.sys.

Native driver experiments may not delete or fundamentally alter the working virtual storage path.

Before native becomes default require repeated proof.

---

# FUNCTIONAL DONE CRITERIA

Call Native ANS V1 functional only when all are true:

1. Windows boots from the internal SSD using AppleAns.sys.
2. No virtual NVMe device is involved after ExitBootServices.
3. READ and WRITE payloads DMA directly between ANS and Windows-owned memory.
4. Multiple commands can be simultaneously outstanding.
5. FLUSH is proven.
6. GPT/filesystem data survives checksum validation.
7. Reboot and clean shutdown work.
8. No Event129/reset during sustained storage stress.
9. Virtual recovery profile still boots.

---

# PRODUCTION DONE CRITERIA

Do not call it production-ready until additionally:

* repeated cold boot;
* repeated warm restart;
* repeated shutdown;
* Fast Startup;
* hibernate/resume if supported;
* crash-dump path;
* controller timeout recovery;
* Windows Update workload;
* large sequential workload;
* QD random workload;
* filesystem metadata workload;
* zero checksum corruption;
* no unexplained Event129;
* no tag leaks;
* no stale generation reuse.

Use a long-run soak test.

---

# IMPLEMENTATION ORDER — DO NOT REORDER WITHOUT EVIDENCE

Execute exactly in this dependency order:

1. freeze recovery baseline
2. canonical ANS config
3. broker ABI
4. m1n1 common ANS refactor
5. async tag engine
6. PRP IPA→PA translation
7. native interrupt path
8. Mu ANS0 ACPI
9. ExitBootServices handoff
10. AppleAns StorPort skeleton
11. admin queue
12. Identify
13. single READ in WinPE
14. disk enumeration
15. concurrent READ
16. controlled WRITE
17. filesystem test partition
18. native boot
19. reset/recovery
20. shutdown/restart
21. Fast Startup/hibernate
22. crash dump
23. performance optimization
24. optional full Windows ownership.

Do not jump from enumeration directly to booting the production Windows volume.

Do not optimize virtual NVMe as part of this project unless it is required to preserve recovery.

Do not continue GPU experiments while an ANS hardware candidate has exclusive platform ownership.

---

# FIRST IMMEDIATE TASK

Do not start by coding AppleAns.sys.

First produce one short architecture audit based on current local HEAD, not stale GitHub:

1. current `src/nvme.c`;
2. current `src/hv_nvme*.c`;
3. current `src/hv_pci.c`;
4. current ANS/SART/RTKit implementation;
5. current J313 ADT;
6. current Mu DSDT/MCFG;
7. pinned Linux `drivers/nvme/host/apple.c`;
8. pinned Linux T8103 device tree;
9. current Microsoft StorPort documentation.

Output:

`CURRENT OWNERSHIP`
`TARGET OWNERSHIP`
`RESOURCE MAP`
`IRQ MAP`
`DMA ADDRESS MODEL`
`UEFI → WINDOWS HANDOFF`
`BROKER ABI`
`FILES TO CHANGE`

The audit may correct filenames/numbers but must not change the architecture without concrete source evidence.

After the audit, implement Phase 0 immediately.

Do not stop merely to report the audit.

Continue autonomously through offline phases.

Hardware testing begins only at ANS001 with an exact recovery path.
