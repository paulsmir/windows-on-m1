# C15 root private extent preflight in current 0003 Calc/Create

WINDOWS CONTRACT: pinned WDK26100 `d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`
defines paired `CalcPrivateRootSignatureSize_0001` and
`CreateRootSignature_0001`: the runtime allocates the driver's private bytes
from the size result and passes storage to Create. The [Microsoft DDI index](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/)
lists both callbacks; [root-signature limits](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits)
limit root argument cost to64 DWORDs, and [hardware limits](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-support)
state the 2032 static-sampler envelope. No per-table descriptor-range count
limit is inferred from those documents. A valid descriptor supplied at Calc
must yield the same private extent required by Create.

AGX/ASAHI CONTRACT: current private root stores parameters, resolved
descriptor ranges and static samplers in one CPU allocation. Existing DXBC
root lowering reads that layout at command replay; a size/layout regression
would affect current GPU work. Asahi native state does not own the Calc/Create
arithmetic. m1n1, Mu, KMD, ACPI, UAT, interrupt and power layers cannot fix
an undersized UMD private root allocation.

TRANSLATION: use the already host/WDK-checked
`AgxD3d12MeasureR1RootPrivate` as one preflight in both old callbacks.
Set a current-contract policy of at most64 parameters, UINT_MAX total ranges,
2032 static samplers and64 DWORDs. The parameter bound follows the 64-DWORD
root cost for valid nonempty parameters; the range policy adds no new cap.
Calc returns0 for invalid records or failed checked arithmetic. Create
rejects the same records before writing private storage and retains the old
field-copy/offset behavior for valid descriptors. The helper does not read
range-array contents, only counts and required nonnull pointers; current
Create remains responsible for those semantic values. It does not yet store
the extent or register provider roots.

ATOMIC CONTRACT: Calc and Create share the same preflight/policy because
accepting a layout in one but not the other would allocate an inconsistent
private extent. The WDK's size-then-create allocation contract makes those
two DDI checks one indivisible variable; no other state or caps change.

WHAT IS STILL UNKNOWN: actual driver package/GPU behavior after this old
0003 root path change, and provider registration/lifetime under R1. These are
separate. Deterministic CPU test first: actual-source Calc returns the same
size as Create's trailing-array requirement for a valid root; NULL descriptor,
unknown parameter type, >64-DWORD cost or missing arrays are refused before
destination write. The hardware checkpoint then installs one exact signed
package on normal GPU-visible J313, requires all previously accepted 63
non-E D3D12 steps, known C21E SKIP, smoke73+knownMSAA, KMD pool/FL11/corpus/
DWM and usual GL/CS references, collects receipts, reboots and runs exact
FactoryV2 visible recovery to Code28/PackageAbsent/noService/8CPU. Revert
with the immutable recovery artifact on hang; repeat only under a new EXP ID.
No R1/SM6 capability is published by this root preflight experiment.

The current DDI now calls one checked helper in both 0003 Calc and Create
under the recorded policy. Actual-source host extraction first produced six
failed assertions and an ASan/UBSan NULL-array read in old Calc; after wiring,
the fixture returned `R1_ROOT_PREFLIGHT_PASS checks=13 failures=0`. A first
GREEN compile found a local name collision between the measured range count
and the existing Create range pointer; only that identifier and the fixture's
extracted policy declaration were corrected before the passing run. EXP1655
staged seven exact DDI/header/script hashes; pinned WDK26100 Windows ARM64
native-owner/NONE/winsys syntax returned rc=0 for all three. Raw builder log
SHA256 `ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
This is not a J313 or signed-package verdict. The hardware checkpoint still
must isolate only this DDI/header change on the accepted EXP1594 source,
build/sign an exact packet and run normal visible recovery afterward.
