# EXP944: report GPU writes to displayable surfaces

WHY THIS HYPOTHESIS:
1. On 941 and 943 DWM's UMD Present callback returned S_OK, yet no KMD Present
   or VirtualPresent followed. The physical 941 image contained repeated
   wallpaper fragments and scanlines. Native DWM render submits and completes.
2. `submit` in `umd_gpuva_windows.c` sends `D3DDDICB_SUBMITCOMMAND` with
   `NumPrimaries=0` for every batch. The native presentation resource imports a
   GPU-local render target directly. Its screen-buffer slot already has a
   `WrittenPrimary` marker, but submission ignores it.
3. Microsoft says a GPUVA UMD must place every written allocation created with
   `D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE` in WrittenPrimaries; the
   scheduler uses the list to synchronize writes with display flips. The
   existing fixture even marks an imported buffer WrittenPrimary while expecting
   zero. A CPU staging copy is not a GPU write to the displayable allocation.

WINDOWS CONTRACT: FULL GRAPHICS, WDDM 3.0 on pinned WDK26100. The runtime's
CreateResource DISPLAYABLE_SURFACE flag or primary description identifies a
displayable allocation. `D3DDDICB_SUBMITCOMMAND::NumPrimaries` and
`WrittenPrimaries` describe displayable allocations written by that command;
at most `D3DDDI_MAX_WRITTEN_PRIMARIES` (16) are allowed. MakeResident and its
paging fence must already protect each submitted resource. A private per-resource
metadata blob passed through Allocate is returned in OpenResource and preserves
the displayable marker across processes. Sources:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_submitcommand
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/residency-overview
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_allocate
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ns-d3d10umddi-d3d10ddiarg_openresource

AGX/ASAHI CONTRACT: the existing native batch passes its actual written BO
tokens into `AgxWin32GpuvaSubmit`. A direct imported GPU-local BGRA presentation
BO is both the render target and runtime allocation; a staging-backed canonical
BO is not a write to the runtime's displayed allocation. The existing MakeResident
path covers submitted tokens and waits for its paging fence. Asahi/m1n1 queue,
UAT, RTKit, GPU submission, interrupt, and Mu/ACPI contracts remain unchanged.

TRANSLATION: preserve the runtime's displayable flag in a small versioned
resource-private blob at Allocate and validate it on KMD Create/Open. Propagate
it into the UMD presentation buffer slot on Create/Open. For each submitted
written BO, append its kernel allocation handle only if the slot is direct and
marked WrittenPrimary. Deduplicate; fail closed on overflow or uncertain
ownership. Keep non-displayable shared surfaces and staging canonical BOs out.

ATOMIC CONTRACT: CreateResource flag -> persistent resource metadata -> direct
slot marker -> `NumPrimaries`/`WrittenPrimaries` is one indivisible WDDM
invariant. A list entry without the runtime's flag is untruthful; a flagged GPU
write omitted from the list violates the scheduler contract. No other capability
or DDI version changes are grouped into this experiment.

WHAT IS STILL UNKNOWN: whether Windows advances DWM's kernel presentation path
and the physical panel once truthful primary writes are reported. One short
same-ce544 windowed test, followed by KMD source address/present counters and a
physical observation, distinguishes this. Present S_OK alone is not success.

Current hardware baseline: 943 Code0/CPU8, windowed SDK6796 Present S_OK/exit0,
DWM1228 at least 8 UMD Present calls S_OK and 116 render submits, but KMD
Present/VirtualPresent zero; selected primary source address unchanged.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The current real GPUVA submit
returns `NumPrimaries=0` even when a direct written presentation slot is marked
WrittenPrimary. The test must prove one exact handle is reported, a staging BO
and ordinary shared BGRA surface remain absent, duplicate tokens report once,
and opening a shared displayable allocation retains the marker. It must verify
all existing residency/copy/fence and first-failure behavior remains valid.

Steps: implement the atomic metadata/list contract; run the focused real UMD
callback and KMD allocation/open replays plus decoder/ledger; commit and index
CHANGES; build/sign/hash/PE-PDB gates; freeze and clean 943 through377/392;
stage only the exact new package from a durable Code28 boot; run one bounded
hardware discriminator. Preserve rollback artifacts and record both ledger phases.
