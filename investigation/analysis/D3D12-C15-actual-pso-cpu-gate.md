# Actual PSO and cached compute CPU gate

WINDOWS CONTRACT: keep existing CORE0003/FL11_0 DDIs, shader/PSO allocation and
root resolution. Exercise actual StoreShader, PSO Calc/Create, CompileCompute,
full EnsureOwner and PSO destruction with valid typed model storage. This is a
private CPU fixture, never an installed driver or runtime allocation guarantee.

AGX/ASAHI CONTRACT: prepared compute entry receives actual owned legacy NIR and
clones before the consuming Gallium callback. A fixture owner with typed CPU
screen/context callbacks proves ownership and metadata only; physical Asahi
initialization, BO allocation and dispatch remain outside this checkpoint.

TRANSLATION: use a private copy of the qualified full UMD project and two original
include-wrapper translation units. Replace exactly one DDI and one owner compile
item; each includes the complete actual staged implementation and appends fixture
entry/helpers. This retains real OwnerCreate, diagnostics and native Win32 support
in the existing project graph. The typed model device preloads its fixture owner,
so actual EnsureOwner takes its existing early return. No replacement EnsureOwner,
OwnerCreate, logger or resolver; no copied private owner layout or hand-selected
success stubs. Preserve each original compile item's metadata/profile.

The source basis is qualified assembly3d40 with baseline DDI15f637 plus only the
reviewed CompileCompute function delta, and committed factory5files75d861.
Exclude current unvalidated publisher666abec. Feature1 and fixture exports exist
only in the private project. Add the real no-core factory object bundle and exact
17 archives explicitly; the normal1506 props provide runtime plus five core
archives, so this additional closure remains a new link gate. Use normally
qualified1504 runtime/source provenance here, distinct from isolated1512's
explicit1500 BUILD_ONLY support. Never promote this DLL into a driver package.

Inspected primary files: actual AppleAgxRenderAdmissionUmd.vcxproj DDI/owner items
and link properties; umd.c DllMain537-565; actual DDI PSO storage/Create, EnsureOwner,
CompileCompute and Destroy; current prepared owner and factory. Detailed original
source plan `.local/c15-full-umd-ddi-cpu-source-plan/PLAN.md` SHA
adf405da9f80ef7fed4220066dcc4f1327720e59fb055931b708a61ad452f7b8.

WHAT IS STILL UNKNOWN: evaluated item identity and exact native link, actual
CRT/TLS initializer/import behavior, copied PSO program independence, success and
ordinary NULL callback failure caching, and matching destruction. Test CS5.0 and
static namespace CS5.1. Retire original shader/program input after actual PSO
creation; compare retained PSO words/map and callback count. Second compile must
return cached result without a new constructor/callback. Root UNSPECIFIED metadata
must not acquire compatibility meaning or change existing root resolution.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Borrowing original shader input
instead of the PSO copy, using an extent beyond recorded CS words, recompiling a
cached result, partial compute publication or deleting state more than once.

DllMain installs process VEH and SIGABRT handlers; explicit unload restores/removes
them and flushes diagnostics. Keep these behaviors. No fixture action runs under
loader lock; no nontrivial fixture static initializers. Build-only import/map and
CRT/TLS review precedes a separately preregistered, bounded disposable CPU host
that loads the exact DLL, calls only its fixture export, frees all fixture objects
and unloads. No fault triggering/handler modification, public device creation,
OpenAdapter, dispatch or submission. Ordinary test failures report status.

Checkpoint and recovery: first offline/source review and fresh builder compilation;
then separate CPU execution after active GPU experiment1510 rollback. CPU artifacts
never install a package or change firmware/service state. Current FactoryV2 visible
recovery remains the GPU rollback owner, with exact package removal and Code28.
