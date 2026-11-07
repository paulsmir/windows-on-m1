# Failed FillDDITable must not publish a runtime-list table

The CPU defect is confined to publication order. FillDDITable assigned the first
process-global list_table before validating the destination through FillTable.
A NULL or undersized request therefore returned E_INVALIDARG but retained tokenBad;
a subsequent valid Fill could not publish H0 because the global was already set.

The minimal correction publishes that existing global only after SUCCEEDED(hr).
It preserves first-valid-token behavior. It changes no adapter registry, callback
routing, table selection, device lifetime, CORE/R1/SM6 capability or GPU semantics.
Existing global ownership limitations remain separate gates; this is not their fix.

## Primary contract and invariant

Pinned WDK26100 d3d12umddi.h SHA256
 d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28
provides FillDDITable, list0003/queue0001 records and actual callback types.
D3D12-C15-r1-command-tables.md contains the exact-runtime positive family proof:
list408bytes/51 pointers, queue56bytes/7 pointers, list tableNum0/1 are instance
indices, not version selectors. No extra binary archaeology or new runtime premise
is needed for this CPU side-effect invariant.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A refused Fill publishing a token
that later prevents successful table publication, buffer/canary corruption, or a
change to the existing first-valid behavior. No public runtime malformed-input
reachability or successful R1 device admission is inferred.

## Actual RED/GREEN evidence

Original attempt BEFORE80a8c7c9 stopped after RED compile0 at the dependency gate:
three actual SDK headers were not registered. No linking or functional RED ran.
STOP verdict SHA cb8f9a9b71943dde5b5a91a7316575e0240539fe7cbdf060079d4700563905c2;
artifact manifest99af18c012ebaad27839e9ec81967508c9bc1e1ff1f74406b268d1b2e59c3d63.
The complete actual closure was collated once:124 unique inputs/150 dependency
rows including packing-header repeats. Retry added only dxgicommon.h,dxgitype.h,
dxgiformat.h pins plus a fresh root/argv. The STOP is preserved, not relabelled RED.

Retry1 BEFOREd95eb2d8 approved proposal
 bacd52924e9729a3effa02a5654f07d05330e0d77b6138f7ce7bf138579dc529.
One six-command x64 gate returned0,0,1,0,0,0: actual RED137 checks/6 failures;
actual GREEN137 checks/0 failures. All267 native pins and four staged files were
independently rehashed unchanged, all32 remote artifacts matched downloads.
Both x64 PE executables are25088bytes and import only Kernel32/VCRUNTIME/CRT.
RED SHA9ab6c401f0a6bc3100cbb59bf41cae0c8457cb85f68659001eea2ddbea1194ae;
GREEN147dd975f2a525b12fb6845cb1bee08da7bb5c4c36d0f4c1924580e62d3b5607.
Receipts `.local/c15-failed-table-publication-prep/retry1-prep/gate-receipts/`:
verdictcc2ba3c753bdccb8bbf9d4fe6355bccd837940b3369453ade41f72d6b2dc8ddf;
58-file artifact manifest54407ffed7dd4942f2c4d4e4321b68fdbaa3275ff70f2da062d5c59e256b8738.

## Durable regression boundary

The durable renderer reads the CURRENT repository DDI source with dynamic ROOT,
not an archived candidate. It extracts actual FillTable, final FillDDITable and
list/queue typed filler bodies unchanged and freezes each slice/full-source hash.
The C++ oracle observes the actual global. It runs NULL and one-byte-short requests
with tokenBad, valid list0/H0, list1/H1 and queue0 with external canaries. It checks
all56 nonreserved callback slots using real SDK types and both reservedNULL slots.
Only noninvoked callback link bodies are original signature-faithful shims; they
implement no Fill/selection/publication logic. No smaller private object model or
CreateCommandList surrogate is used. Cross-device fix explicitly remainsNO.

Default unittest discovery skips its one opt-in method before tool/file work.
Explicit Windows x64 execution requires its own BEFORE and approved source/tool/
actual-SDK/generated-input/three-argv manifest. Actual compiler dependencies and
x64 PE are verified before executing the CPU fixture. Compile/link failure is not
functional RED, and earlier receipt directories may not be reused.

The external confirmed RED/GREEN above is not execution evidence for the newly
adopted renderer/runner. That durable gate and one full suite must be separately
run after source freeze, before committing. Production application waits for the
C23 package snapshot so it cannot become an unregistered hardware variable.
No hardware validation, GPU call, DDI capability or R1/SM6 admission is claimed.

## Observed durable current-source verification

The original before-fix actual fixture produced137 checks/6 failures; moving
publication after successful Fill produced137/0. The durable extractor subsequently
read the CURRENT project DDI (including C23 and canonical allocation alignment),
with no old-candidate fallback, and produced an actual SDK x64 fixture.

First outer launcher returned1 because PowerShell treated ordinary unittest stderr
progress as NativeCommandError. Its inner actual compile/link/run all returned0,
all137 checks passed and the durable post-input manifest completed. That outer
FAIL remains preserved; it is not relabelled as a passing launcher.

A separate V2 capture repair uses a bounded Python subprocess to preserve raw
stdout/stderr and explicit returncode, leaving PowerShell only a status line.
Fresh source bundle uses an explicit allowlist and excludes Python caches. Under
BEFORE48f17a5c the V2 outer launcher returned0, Python unittest returned0/one test
in0.755s, actual native compile/link/run returned0/0/0 and137 checks passed. All277
approved inputs and nine staged source/generated/launcher files matched afterwards.
Artifact manifest944c04ed2ab4b6362e4264c3464f6eafd02d871636223607f6f6d512313b9434
is preserved in .local/c15-publication-current-native-v2-prep/native-execution.

Default host discovery skips this opt-in actual-Windows-SDK test and starts no
compiler. Full final branch1590 tests220.503s retains exactly119 baseline failures/
errors, no additions/removals,9 skips. No full-suite GREEN claim. The narrow fix
keeps first-valid-token policy; it does not repair cross-device registration or
token lifetime and does not enable R1/SM6. It is excluded from frozen hardware
1426/1427 sources, which continue their independent registered contracts.
