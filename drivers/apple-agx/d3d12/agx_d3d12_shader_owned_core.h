/* Original CPU-only R1 shader storage. SPDX-License-Identifier: MIT
 * Include actual Windows/d3d12umddi.h first. Unlinked; NOT a DDI callback. */
#pragma once
#include <cstddef>
#include <cstdint>
enum class AgxShaderProgramKind { Unknown, LegacyDxbc, Dxil };
enum class AgxShaderStorageStatus { Ok, Invalid, Unsupported, LibraryRejected, Overflow, StorageInvalid };
struct AgxShaderTrustedSpan { const void *data; SIZE_T bytes; };
/* Future verified provider guarantees lifetime/readability through Calc/Create,
 * stable args and counts, root compatibility bytes belonging to hRootSignature,
 * and no concurrent source mutation. Output pointers/records must not alias any
 * input or destination storage. These guarantees are NOT provided here.
 * Signature spans start at the actual deprecated (0010/0011) signature arrays.
 * program.data MUST be args.pShaderCode; explicit bytes bound parser reads.
 * root is canonical provider-owned byte representation, never a raw root object.
 */
struct AgxShaderTrustedInput {
 AgxShaderTrustedSpan program, input_signature, output_signature, root;
};
struct AgxShaderOwnedLayout {
 SIZE_T total, program_offset, program_bytes, input_offset, input_count;
 SIZE_T output_offset, output_count, root_offset, root_bytes;
};
struct AgxShaderOwnedSignature {
 uint32_t system_value, reg, component_type, min_precision;
 uint8_t mask;
};
struct AgxShaderOwned {
 uint64_t magic; /* Private object format/kind tag, NOT PROGRAM magic. */
 uint32_t stage, flags;
 AgxShaderOwnedLayout layout;
 /* Trailing bytes contain PROGRAM, canonical signatures and root snapshot.
  * All references are relative offsets. No input pointers/handles retained. */
};
AgxShaderStorageStatus AgxShaderOwnedCalculate(D3D12DDI_HDEVICE,
 const D3D12DDIARG_CREATE_SHADER_0010 *, uint32_t stage,
 const AgxShaderTrustedInput *, AgxShaderOwnedLayout *);
AgxShaderStorageStatus AgxShaderOwnedCreate(D3D12DDI_HDEVICE,
 const D3D12DDIARG_CREATE_SHADER_0010 *, uint32_t stage,
 const AgxShaderTrustedInput *, void *storage, SIZE_T capacity, AgxShaderOwned **);

/* Input must be a live owned CPU object (or NULL), not an arbitrary DDI handle.
 * The existing DXIL tag/layout is preserved; no pointer validation is supplied. */
AgxShaderProgramKind AgxShaderOwnedGetProgramKind(const AgxShaderOwned *);
