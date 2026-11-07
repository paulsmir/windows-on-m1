# Exact runtime early-R1 command-table selection

One bounded read-only pass, exact D3D12Core10.0.26100.9549
SHA256052241d7d9fdcf32dc2a86491c56e524f80e78990e6a3ddd787f75767bd41a82.
Matched1412 PDB association is reused, not revalidated by a new tool invocation.
No binary/module execution, new disassembler/PDB tool invocation, fetch, compiler,
Git, builder or guest command. Input/tool hashes and exact existing disassembly
ranges are preserved alongside this original analysis. No external implementation
code is imported into production. Python only reads existing files and computes
hashes/record counts.

## Positive selection chain

The preexisting immutable constructor receipt identifies selected interface/build
rows: ordinal2 is0010 (0x000c000a/build0), ordinal3 is0011 (same interface/build1).
Constructor stores selected ordinal at adapter+0x130 (RVA6b20). The newly inspected
initialization at7db8 loads this ordinal. Its explicit ordinal2 branch7dcc..7ddc
stores UINT2 at adapter+0x780; ordinal3 branch7de0..7df0 stores UINT3 at the same
address. Thus early-R1 Core FeatureVersions are positively2/3, not inferred from
header age or a missing declaration.

Matched named NDXGI::CDevice::GetTableInfo native RVA291228 takes a table type.
It obtains the FeatureVersions address from [device+0x18]+0x780. The table-to-feature
helper2863e8/286398 maps tabletype1 AND2 to feature1 (Core). Named
GetFeatureVersionImpl at22ac40 returns firstUINT of that record for feature1.
IsTableTypeCommandList2864f8/286498 returns true for type1, false for type2.
For command-list tables GetTableInfo can compute a separately returned driver-size
using current feature version, then obtains an internal latest-size buffer using
version78. This INTERNAL allocation is not the size passed to the driver.
For queue, it obtains current-version size directly. Preserve this distinction:
large internal/latest tables do not imply a newer driver callback ABI.

The explicit size selector285c20 is positive code evidence: tabletype1 and version
1/2/3 all take the SAME408-byte allocation/return branch285c44..285c64. Type2
continues to285c98: version1/2/3 all take the SAME56-byte branch285cbc..285cdc.
Existing pinned WDK definitions contain exclusively51 pointers (408bytes) in
D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 and7 pointers (56bytes) in
D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001. Pointer inventories, names and calculated
sizes are in wdk-record-analysis.json. This is an observed ordinal-dependent
size-selector branch combined with the matching primary ABI records, rather than
an argument from the absence of an0010 command-table declaration.

Control crosscheck: same selector tree285e48 handles deviceCore tabletype0;
version1 returns712 and version2 returns728, matching the known89-pointer0003
and91-pointer0010 core tables. The early list/queue equal-size branches are
explicit despite the changed core table, so using CORE0010 does not imply copying
that core revision number into list/queue/tableNum.

| Negotiated ordinal | Interface/build | Core feature version | driver list type1 | driver queue type2 |
| --- | --- | --- | --- | --- |
|2 |0x000c000a /0 |2 |0003,408bytes |0001,56bytes |
|3 |0x000c000a /1 |3 |0003,408bytes |0001,56bytes |

This closes the exact saved-runtime family/extent gate for those two ordinals.
It is not a claim about arbitrary future runtime/table revisions, unimplemented
callback semantics, multidevice safety or successful new-profile admission.

## Actual FillDDITable dispatch and tableNum

Matched named NDXGI::CDevice::UpdateGlobalDDITable native RVA1ef6b0 processes table
requests. At1ef7e8 it calls the named GetTableInfo helper above with tabletype and
separate buffer/driver-size output pointers. The normal driver callback path
1ef934..1ef994 loads FillDDITable target from [device+0x28]+0x140 and hAdapter
from that record+0x150. Immediately before call1ef994:

- x0 hAdapter;
- w1 current tabletype;
- x2 allocated buffer;
- x3 separately reported driver table size from [frame+0x40];
- w4 for tabletype1: ([device+0x258]-[device+0x250])/8, the existing list table
  vector length; for every other type in this branch, including queue2, zero;
- x5 runtime table tracking handle/token when applicable, otherwiseNULL.

At1ef9ac..1ef9bc, AFTER Fill returns, type1 appends its new entry to the list
vector. Thus index0 is sent for its first table; later list tables receive the
current count before append. It is not0010/0011, not the interface and not an
ABI revision. The pinned PFND3D12DDI_FILLDDITABLE definition has precisely
(hAdapter,tableType,pTable,SIZE_T,UINT tableNum,HRTTABLE). The source gate must
preserve table-instance/HRTTABLE association instead of retaining one global
first-list pointer. Queue's observedzero index does not license ignoring
unexpected future indices or arbitrary table requests.

## Remaining gates and smallest further offline proof

No new binary uncertainty remains for this exact ordinal2/3 table family and
extent mapping. Implementation still needs actual typed assignment tests and
canaries for408/56bytes; mismatched size/type refusal; two distinct list table
indices/HRTTABLE identities without cross-device leakage; field-by-field new
CORE0010 population; existing six successful zero-native-cache callbacks;
ordinary object ownership/lifetime and error routing; negotiated-version storage;
shared-context/reentry and complete translator OOM policy. No capability switch
or hardware experiment follows from this static table-selection finding alone.
The smallest next offline adapter test uses actual pinned WDK callback types and
real FillDDITable implementation with two different list table buffers/canaries,
indices0/1 and independent runtime handles, plus queue index0. It must not select
ABI from tableNum. That test/implementation needs its own approved source/command
freeze; none was compiled or implemented during this pass.

The current 0003 implementation names the fifth parameter `table_num` in
source and logs it unchanged; this is a terminology correction only. It retains
the first successful list HRTTABLE for the accepted 0003 path. The official
[FillDDITable DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_fillddittable)
identifies that UINT as the table index. A later 0011 source gate must provide
per-instance HRTTABLE association and an actual list-selection contract before
publishing R1; this name change does not pretend to implement it.
