# EXP860 diagnostic v2 — offline implementation

Authority: user approval following R146. Base
`2f737526de48142272f95bfc6a3265a1b7bdd400`. Scope is diagnostic only: no package,
Air operation, firmware, capability, signer or recovery change. EXP859 ordinary
Code28 remains the last accepted hardware state. This is not an EXP860 hardware
verdict or proof of the QUERY53 cause.

## Contract and implementation

The approved checkpoint is a first-failure snapshot that separates bootstrap
root selection from missing VA/ancestry and unpublished native coverage. R146's
current-source inventory and saved EXP859 evidence are the source-first basis
(`R146-query53.md`): Asahi native16K alignment; m1n1 root/grant ownership; Mu
reserved1GiB/64-bit ACPI contract; Microsoft SetRoot/UpdatePageTable/GpuMmu DDIs;
current G3 paging, graph, escape, receipt writer and R145 replay. No new machine
state, register, interrupt or DMA assumption was introduced. Exact R143 full-owner
and EXP859 recovery contracts remain unchanged.

VidMm owns mappings/root notification, UMD owns the QUERY and paging wait, KMD
owns the snapshot and authorization, m1n1 owns native translation/grants, and Mu
owns reservation/ACPI. IRQ, DMA, power and recovery ownership are unchanged.
The observed mismatch remains that the R145 fixture binds a root before QUERY,
while the live first-failure receipt did not identify the root. No root is bound
by this diagnostic and no missing mapping is admitted.

`AppleAgxGpuvaG3GraphInspectRangeAccess` is the existing read/write walk with an
optional failure output. The original API wraps it with a null output. QUERY's
guard53 calls the same walk with bounded stack evidence; every admission
condition, status and exit target remains unchanged. The first failed native
component is captured during that walk, avoiding a second traversal. Only the
winning first claim samples process/context history and, if needed, four existing
ResidentPtes to distinguish an unpublished logical group from an unmapped group.

Capture uses embedded adapter storage and the existing G3 mutex. Claim0→1→2
publishes an immutable144-byte record before unlocking. The winning caller alone
persists it after unlocking and releasing its allocation reference. No diagnostic
pool allocation, backing publication, mapping change or retry was added. The
existing request-envelope allocation is unchanged. Registry operations retain
the prior failure/first-attempt policy; if persistence fails, the RAM snapshot
remains and later failures do not overwrite it.

Failures before QUERY lock acquisition retain predicate/status but explicitly
lack the locked/process/context availability flags; invalid/truncated buffers
are not dereferenced to invent graph evidence. Successful QUERY and all
non-QUERY operations emit no failure receipt, as before.

Per-process and per-context SetRoot counts saturate at UINT32_MAX. History is
updated under the process mutex for owned-context callbacks at PASSIVE_LEVEL;
malformed/foreign or illegal-IRQL callbacks are not counted. A zero last IPA
means address resolution was not reached or failed. Otherwise it is the last
resolved IPA, translated to the broker shadow when that stage was reached.
ContextRootIpa separately records the existing current successful binding.
For predicate53, the earlier nonpoisoned/context checks have passed. Counts are
observations at the serialized update point, not evidence of GPU execution.

## Registry ABI and collector

Keep the **same REG_BINARY value `Wom1G3CopyQueryFailure`**, opened using
`IoOpenDeviceRegistryKey(..., PLUGPLAY_REGKEY_DEVICE, ...)`. Existing collectors
use the observed key
`HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters`.
Collectors
that save all device registry values in `state.json` and `devnode.reg` capture
v2 automatically; remove any hard-coded16-byte decode assumption.
Recommended raw export filename: **`Wom1G3CopyQueryFailure.bin`**. Do not truncate
to the old header. Associate the value with exact package/boot and remove stale
experiment diagnostics during ordinary cleanup, as before.

Host decoder:

```sh
python3 tools/decode_g3_copy_query_failure.py Wom1G3CopyQueryFailure.bin
python3 tools/decode_g3_copy_query_failure.py state.json
python3 tools/decode_g3_copy_query_failure.py devnode.reg
```

The tool outputs JSON, accepts legacy v1/16-byte receipts, and rejects unsupported
versions, wrong lengths, unknown flag bits/reasons, and ambiguous registry exports.
It never connects to a guest or reads the live registry.

All fields are little-endian. Layout is12×u32 followed by12×u64 (144 bytes):

| Offset | Fields |
|---:|---|
| 0 | u32 Version=2, Bytes=144, Predicate, Status |
| 16 | u32 Flags, Write, MissingLevel, MissingIndex |
| 32 | u32 MissingReason, ComponentReason, ProcessSetRootCount, ContextSetRootCount |
| 48 | u64 GraphRootIpa, BootstrapIpa, ProcessLastSetRootIpa, ContextLastSetRootIpa |
| 80 | u64 QueryVa, QueryBytes, MissingVa, ProcessGeneration |
| 112 | u64 MappingGeneration, ContextRootIpa, ProcessId, ContextToken |

Flag bits:1 captured under lock;2 process available;4 context available;
8 request envelope available;16 allocation range validated;32 RootIsBootstrap.
MissingLevel/Index=0xffffffff means not observed. QueryBytes is meaningful only
with bit16. Write remains0 because this QUERY already requests read access.
Native levels are root0/middle1/leaf2; MissingIndex is that level's table index.
MissingVa is the first uncovered byte in the requested interval.

Reasons:0 none/not observed;1 no-root (no root edge for that VA);2 no-table
(no middle edge);3 leaf-absent (no native leaf and no known valid logical page
in its group);4 leaf-not-published (logical group has valid entries but no
native leaf, or a retained leaf has no backing/access);5 tail-short (a covered
prefix precedes the first failed component). ComponentReason retains1–4 when
MissingReason is tail-short. No-root does **not** assert the root allocation is
absent; RootIsBootstrap distinguishes the selected bootstrap root. Missing
ResidentPtes cannot prove that logical mappings never existed.

## Verification and limits

Initial real-body16/64 replay is RED on missing v2. GREEN gives distinct decoded
bootstrap-root, absent-VA and short-tail records. It proves immutable first
receipt after later SetRoot/failures, capture claim under the QUERY mutex, only
the existing envelope allocation, separate process/context counts, saturation,
middle-link classification and registry writes after unlock/reference release.
The existing R145 guard/lifetime/provenance/partial-copy cases still run.

Independent review found that native unpublication removes the leaf node. A
real partial4K unmap leaves three valid logical pages and reproduces the mistaken
leaf-absent reason (RED); bounded existing-PTE refinement produces
leaf-not-published (GREEN) in both profiles. Review accepted the repair and found
no further substantive issue. Interlocked arbitration uses the Windows primitive;
the sequential host shim checks first preservation and lock placement, not a
concurrent Windows stress workload.

The three changed C TUs (`gpuva_g3_windows.c`, `receipts.c`, shared
`apple_agx_gpuva_g3_graph.c`) compile under pinned WDK26100/MSVC14.44 ARM64
`/W4 /WX /analyze`. Builder verifies535 input hashes and receives only7 changed
files. This is object compilation, without linking/signing/packaging.
Exact final suite comparison, compiler commands/object hashes, RED/GREEN logs,
source hashes and decoded receipts are in
`investigation/evidence/EXP860-query-v2/summary.json`.

Final suite:1156 tests in133.434s, exact baseline15 failures/38 errors/2 skips,
no new or removed failure/error identities. The initial full run exposed two
historical replay extraction failures because the new diagnostic helper did not
exist in the old sources. Using the current diagnostic helper while retaining
the historical functions under test restores all26 VidMm replays; the final full
rerun confirms the baseline. Production source remained identical to ARM64 inputs.

Tandem review SHA256 remains753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5;
all39 OPEN dispositions from `R146-review.md` carry forward. REVIEW R95: ACCEPT —
retain authorization guards and exercise real ordering/provenance.
REVIEW R97: ACCEPT — no historical archaeology; implement the approved bounded
diagnostic. REVIEW R113: ACCEPT — no armed ordinary boot or Air operation.
REVIEW R111: DEFER — cache-attribute changes remain outside QUERY diagnostics.

Smallest future hardware checkpoint: on separately authorized exact package/run,
the first QUERY53 v2 receipt names selected root and first missing component;
no receipt is inconclusive, never success. Evidence collection precedes exact
package cleanup and restoration of EXP859's accepted Code28 contract. No new
hardware experiment is preregistered or run in this task.
