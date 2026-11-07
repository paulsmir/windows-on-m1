# C15: unlinked descriptor adaptation for owned compute NIR

## WINDOWS CONTRACT:

Keep the existing C17 immutable `(kind, space, register) -> slot` map and its
independent CBV/SRV/UAV namespaces. This CPU pass adds no Windows callback,
shader-model advertisement or root-resolution policy. Its input must already
belong to the admitted compute 6.0 singleton CBV/raw-SRV/raw-UAV profile.
Root visibility, live resources and readable input ownership remain separate.

## AGX/ASAHI CONTRACT:

The inspected pinned Mesa frontend emits Vulkan descriptor intrinsics; their
set numbers alone are insufficient for Gallium. `vtn_variables.c`,
`nir_lower_explicit_io.c`, `nir.h`, `agx_nir_lower_bindings.c` and `agx_state.c`
establish the frontend address representation and subsequent owner boundary.
The source manifest in `.local/c15-owned-conversion-boundary-source-plan`
has SHA-256 `68923cdc7d5f83902cd6c515f8bd600dfdcd4878bd15462e7f6756bcd07e9ab8`.

Both UBO and SSBO frontend address formats must explicitly be
`nir_address_format_32bit_index_offset`: two 32-bit components containing
buffer index and byte offset. Component count alone cannot prove that format.
Raw SRVs remain unsigned buffer textures; raw UAVs remain SSBOs. Normal Gallium
binding lowering and Asahi preprocessing are still downstream work.

## TRANSLATION:

The original project pass first checks the complete map, descriptor variables,
resource intrinsics and texture dereferences. It accepts only known dense
singleton slots within the existing 14/128/8 limits. All resource reindexing,
nonzero descriptor-array selection, unknown namespaces, mismatched descriptor
types and unsupported texture forms refuse before mutation. Map bytes remain
unchanged and refusal publishes zero statistics.

After preflight, a resource index becomes `(slot, 0)`, a directly matching
descriptor load forwards that address pair, and ordinary address arithmetic
retains byte offsets. Validate raw-SRV set/type/slot before calling the actual
`nir_lower_samplers`, which discards set identity. The fixture then performs
explicit buffer IO lowering, system-value/SSA cleanup and NIR validation.
This does not erase loads, stores or unsupported instructions to force success.

No external implementation was copied. The pass and fixture are original; the
fixture reuses this project's existing actual-NIR semantic oracle. Legacy DXBC
translation, the native owner, KMD, m1n1, Mu and recovery are unchanged.

## WHAT IS STILL UNKNOWN:

The pass does not establish general shader-operation admission, typed resources,
samplers, arrays, bindless access, graphics IO, recoverable allocation failure or
Windows runtime behavior. The admitted raw-resource profile is a precondition;
erased SPIR-V buffer layout cannot by itself distinguish raw and structured DXIL.

Windows libvtn compilation/linkage remains unproved. The private source inventory
`windows-libvtn-source-inventory.json` under the durable adoption preparation
records 14 configured C translation units, generated inputs and license markers;
SHA-256 `79b20c2bfc3f9cd0dc2eec2bc852b23ba9dde7e349f45e05e78650ffeb981b1c`.
It is a source inventory, not a Windows build result. The host diagnostic
SPIRV-Tools dependency is not assumed to be a required shipping dependency.

## Checkpoint and recovery

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Namespace aliasing, descriptor
type loss, unresolved Vulkan descriptors, mutation on refusal and dropped or
misdirected writes. EXP1442 uses actual frozen mixed SPIR-V and the actual Mesa
frontend. Seven refusal clones preserve printed NIR/map and zero statistics.
The existing independent oracle checks 192 lanes, 173 active lanes, 19 tail lanes,
384 words and 346 stores; both result vectors match. Swapped SRVs, lost seed and
wrong UAV slot each produce 346 mismatches; unsupported operations/bounds refuse.

The first compilation stopped on two stale producer-instruction member accesses;
its receipt remains intact. Replacing those two accesses with the pinned
`nir_def_instr` API yielded four zero-exit commands with empty stderr, checked
MMD dependencies, 44 direct and 6958 header pins unchanged. Result manifest:
`563e7784a691e323e46f98e11d25f47f54f5451646963f864e41a8a9647a4b06`.
The durable CURRENT ROOT opt-in passed in 5.142 s using the same pass and oracle,
four commands and actual dependency closure. Default discovery skips before
external tool access. No translator, AGX backend, Asahi preprocess or GPU ran.
No hardware experiment is justified for this deterministic CPU checkpoint.

## Combined source-suite checkpoint

The one full-suite run covered both independent unlinked NIR-map and provider
phase changes: 1611 tests in 221.143 s, exactly the prior 119 failures/errors
(68 failures, 51 errors), no added or removed failures, 15 skips, all 12 source
pins unchanged. Log SHA-256:
`2d9fa19eb299282741d0608a8d210f217271234839c6172dfaa4859fb46084ec`.
This is an unchanged-baseline comparison, not whole-suite GREEN. The two
implementations are committed separately; the suite refers to their combined
source tree rather than asserting two separate full-suite executions.
