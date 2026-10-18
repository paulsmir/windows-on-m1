# C17: static SM5.1 register-space mapping

Goal: compile and bind static CBV/SRV/UAV/sampler references by the complete
(type, register space, register) key. Keep existing SM4/5.0 identity behavior,
current caps and opt-in profile; reject arrays/dynamic resource indexing until
that separate mapping is implemented. No external source import.

## Sources inspected

- Pinned WDK26100 d3d12umddi.h root parameters/ranges/static samplers and
  CREATE_SHADER/CREATE_PIPELINE_STATE root-signature contracts.
- Microsoft Bytecode changes in SM5.1 and Resource Binding specification,
  linked from D3D12-C16-binding-map-plan.md: declarations encode range ID,
  lower/upper register bounds, then space; resource uses encode ID plus
  absolute register index, CBVs add the offset within the buffer.
- Current dxbc_parse.{c,h} owned instruction/index representation;
  dxbc_to_nir.c declaration and direct-resource/CBV lowering, translate_tokens.
- Current owner CreateCompute/create_shader/CreateGraphics and shader lifetime;
  UMD CompileCompute/CompileGraphics, ResolveRoot, BindTable and OwnerBindings.
- Asahi per-stage sampler views, shader buffers/images and constant buffers;
  EXP1379 proves the128-view owner extent; C13/C14 prove graphics UAVs.
- Self-authored pinned-FXC cs_5_1 example in
  .local/d3d12-sm51-space-reference: U0/u0-space1, U1/u0-space2,
  CB0/b0-space3, with distinct stores.

## WINDOWS CONTRACT

A range ID is a shader-local name; it is not the HLSL register. Equal register
numbers in different spaces name distinct bindings. Root arguments still use
root-parameter indices, and table descriptors are read at execution. Per-stage
visibility must be respected. Static samplers participate in sampler namespaces.

## AGX/ASAHI CONTRACT

NIR and Gallium use physical per-stage CBV/SRV/UAV/sampler slots, with owner
limits14/128/8/16. The current translator consumes SM5.0-shaped direct bindings.
Shader objects/compiled metadata belong to the owner/PSO; no root-signature
pointer is needed after compilation to describe resource identity. KMD owns
residency, scheduling, interrupts and recovery; no change to those contracts
is required for namespaces.

## TRANSLATION and ownership decision

Ruling: derive the immutable map from shader declarations, return it with the
compiled shader, and consume that exact map during root resolution. Root-layout
flattening is unnecessary and would add root-lifetime and unused-range issues.
The shader contains the semantic identity and attributes; the root signature
supplies the resource value for that identity.

- Add a plain-C bounded map (kind, range ID, space, logical register, physical
  slot). SM5.1 singleton static declarations get dense slots per kind. Refuse
  duplicate/ambiguous IDs/keys, malformed indices, capacity overflow, arrays,
  unbounded declarations and relative resource indices before NIR/GPU work.
- Normalize the owned parsed instructions/operands into existing SM5.0 shapes;
  never mutate caller tokens. CBV buffer-element indexing may keep its existing
  relative offset; selecting the resource itself must be static for C17.
- A new D3D12-only translate entry returns the map. Existing D3D11/OpenGL
  entry points remain unchanged, including their existing SM5.1 acceptance/refusal limitations. SM4/5.0 on the new
  entry returns an inactive map and the exact old translation.
- Owner compute/VS/PS shader objects hold their maps. Compile before resolving
  roots. A mapped root resolver matches kind/space/register, respects stage
  visibility, finds root VA/constants or table/static-sampler records and writes
  the physical slot. Unused root parameters are ignored; missing or ambiguous
  used bindings fail closed. Legacy inactive-map resolution stays unchanged.
- Use unsigned range subtraction and checked descriptor spans; bound physical
  slots, not arbitrary SM5.1 logical register/space numbers.

## Implementation plan (inline parent lane)

- [x] Add deterministic static-normalization tests: two u0 in different spaces
      map to distinct slots; CBV normalization; input tokens unchanged; invalid
      IDs, duplicate keys, array/dynamic references and capacity overflow refuse.
- [x] Implement shared map and parser normalization, then extended NIR entry.
      Validate/compile real SM5.1 fixture with NIR/Asahi; keep old corpus stable.
- [x] Integrate maps in compute/VS/PS owner objects and namespace-aware UMD
      resolution; retain legacy behavior and32 current targeted tests.
- [x] Probe C17A compute: two root UAVs u0-space1/2 and root CBV b0-space3;
      initialize both outputs to sentinels, then expect3*i+17 and5*i+29.
      C17B fragment UAV u1-space4: counter=image512 as C13. C17C texture
      SRV/static sampler in separate spaces: existing quadrant image oracle.
      Each fresh device has its own AGX_D3D12_<step> PASS/FAIL and receipts.
- [ ] Builder syntax/probe gates, affected tests and one full-suite comparison,
      stack<=16384, commits then CHANGES with exact hashes.
- [ ] Next package may also contain the independent KMD pool fix with its own
      checkpoint/receipts. BEFORE explicitly says ATOMIC CONTRACT: none --
      separate steps. KMD and compiler/replay changes must not depend on one
      another's outcome. Hardware only after offline mapping is designed/proven.

## WHAT IS STILL UNKNOWN

Before hardware: exact normalization/owner/replay implementation must pass all
static-key/range/lifetime invariants offline. Hardware then verifies the designed
namespace isolation and per-stage binding, not guessed capability bits. Dynamic
resource indexing/arrays and complete SM5.1 coverage are outside C17 and remain
fail-closed; DXIL follows later after its explicit license/compatibility review.

## Recovery and evidence

Reuse the hash-pinned full-owner launch, exact experiment-local package removal,
and known inert normal guest. EXP1379 has RECOVERY_DONE/PackageAbsent. New BEFORE
must pin package/probe/source hashes and separate C17/KMD outcomes. Preserve
preexisting mu changes and unrelated README/license commits.

## User-authorized throughput (2026-10-18)

Independent changes may share a package only with separate probe lines/receipts
and explicit BEFORE separation. While hardware runs, prepare the next source,
unit tests, builder compile and probe/package gates. Parent coordinates commits,
snapshots, ledgers and the single hardware control plane; KMD pool agent owns
only its KMD implementation/tests/analysis files.

Legacy source nuance found during actual graphics validation: the old translator
accepts a raw-UAV-only SM5.1 PS by interpreting its range ID as a legacy slot;
that is not blanket SM5.1 correctness. The new D3D12 path returns the complete
u1/space4->physical0 map and correct root resolution. The old CS/array/texture
refusals and old PS behavior are preserved without a new global guard. C17B/C
root resources use PIXEL visibility, so the unchanged legacy VS path ignores
those PS-only namespaces.

## Pre-package verification

45 affected tests PASS (namespace/parser/NIR/Asahi and private-pool invariants);
three pinned MSVC /Zs /W4 /WX variants rc0. Ordinary ARM64 probe SHA256
4f6e18cff1130d097a0513893ad5720ef8861750fcdc475e677bbd257c1075df;
separate pool probe55740036f90c78f8e6a96c33ab4ecebbc0b11360ec87f28aaac72e9d0d96f5d3.
Full-suite comparison and package stack/sign/hash gates recorded below when complete.

REVIEW R113 and historical G3/admission items: DEFER, preserved pinned full-owner
launch and exact normal rollback rather than reopening historical boundaries.
REVIEW R85/R47: ACCEPT pinned source/profile/hash/build receipts; R48: ACCEPT
single launcher, no concurrent USB owner. R97: ACCEPT no old-reference archaeology.
R14: ACCEPT targeted then full once, persistent builder.

Full suite1509 tests in166.532s:68failures/51errors/2skips; all119 failure/error signatures match pre-C17 baseline exactly. DXBC corpus439 translated/13refused/0crashes. Logs /tmp/agx-c17-pool-{targeted,full-tests}.log and /tmp/agx-c17-syntax.log.
