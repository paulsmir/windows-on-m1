/* Original explicit-span legacy PROGRAM framing. SPDX-License-Identifier: MIT */
#ifndef AGX_LEGACY_PROGRAM_FRAME_H
#define AGX_LEGACY_PROGRAM_FRAME_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum agx_legacy_frame_status {AGX_LEGACY_FRAME_OK,AGX_LEGACY_FRAME_INVALID,AGX_LEGACY_FRAME_UNSUPPORTED};
enum agx_legacy_model_bit {AGX_LEGACY_SM40=1u,AGX_LEGACY_SM41=2u,AGX_LEGACY_SM50=4u,AGX_LEGACY_SM51=8u};
struct agx_legacy_frame_profile {
 uint32_t stage_mask; /* tokenized stages0..5. Mask is caller policy, not a cap. */
 uint32_t model_mask; /* exact SM4.0/4.1/5.0/5.1 bit set above. */
};
struct agx_legacy_program_frame {
 size_t program_bytes;
 uint32_t words,version,stage;
 uint8_t major,minor;
};
/* Caller supplies stable actual readable span; may be unaligned. PROGRAM means
 * SHDR/SHEX body, NOT DXBC container, DXIL PROGRAM or raw LLVM BC. Legacy tokens
 * have NO magic field: do not invent one. This routine reads only the two-word
 * header and checks declared extent, never instructions/semantics/checksum.
 * Length2 is valid FRAMING, not evidence that an empty shader is legal.
 * Trailing accessible bytes are allowed but excluded from program_bytes.
 * No allocation, retained pointer, DDI length inference or model advertisement.
 * Output must not alias input/profile. Output bytes zero on INVALID/UNSUPPORTED.
 */
enum agx_legacy_frame_status agx_legacy_frame_program(
 const void *input,size_t accessible_bytes,const struct agx_legacy_frame_profile *,
 struct agx_legacy_program_frame *output);
#ifdef __cplusplus
}
#endif
#endif
