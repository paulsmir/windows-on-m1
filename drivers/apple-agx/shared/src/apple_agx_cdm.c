#include "apple_agx_cdm.h"

/* genxml "groups(N)" over a LENGTH-bit field: zero clamps to one group, the
 * all-groups count encodes as 0, anything larger is not encodable. */
static int to_groups(unsigned int value, unsigned int group, unsigned int length,
                     unsigned int *encoded) {
  unsigned int groups = value == 0u ? 1u : (value + group - 1u) / group;
  if (groups > (1u << length)) return 0;
  *encoded = groups == (1u << length) ? 0u : groups;
  return 1;
}

static void put32(unsigned char *out, unsigned int value) {
  out[0] = (unsigned char)value; out[1] = (unsigned char)(value >> 8);
  out[2] = (unsigned char)(value >> 16); out[3] = (unsigned char)(value >> 24);
}

static void put64(unsigned char *out, unsigned long long value) {
  put32(out, (unsigned int)value);
  put32(out + 4, (unsigned int)(value >> 32));
}

unsigned int AppleAgxCdmLaunch(unsigned int *Out, unsigned int CapacityWords,
                               const APPLE_AGX_CDM_DISPATCH *Dispatch) {
  unsigned int uniforms, textures, preshader, i;
  if (!Out || !Dispatch || CapacityWords < APPLE_AGX_CDM_LAUNCH_WORDS ||
      (Dispatch->PipelineOffset % APPLE_AGX_USC_PIPELINE_ALIGNMENT) ||
      !Dispatch->PipelineOffset || Dispatch->SamplerStates > 7u ||
      Dispatch->SamplerStates == 5u ||
      !to_groups(Dispatch->UniformHalfs, 64u, 3u, &uniforms) ||
      !to_groups(Dispatch->TextureStates, 8u, 5u, &textures) ||
      !to_groups(Dispatch->PreshaderGprs, 16u, 4u, &preshader))
    return 0u;
  for (i = 0u; i < 3u; ++i)
    if (!Dispatch->Global[i] || !Dispatch->Local[i]) return 0u;
  /* Block type Launch (0), mode Direct (0). */
  Out[0] = (uniforms << 1) | (textures << 4) | (Dispatch->SamplerStates << 9) |
           (preshader << 12);
  Out[1] = Dispatch->PipelineOffset;
  for (i = 0u; i < 3u; ++i) {
    Out[2u + i] = Dispatch->Global[i];
    Out[5u + i] = Dispatch->Local[i];
  }
  return APPLE_AGX_CDM_LAUNCH_WORDS;
}

unsigned int AppleAgxCdmBarrier(unsigned int *Out, unsigned int CapacityWords) {
  if (!Out || CapacityWords < APPLE_AGX_CDM_BARRIER_WORDS) return 0u;
  /* Unk 0..19 (bit 3 is the USC cache invalidate), block type Barrier (3). */
  Out[0] = 0x000fffffu | (3u << 29);
  return APPLE_AGX_CDM_BARRIER_WORDS;
}

unsigned int AppleAgxCdmTerminate(unsigned int *Out, unsigned int CapacityWords) {
  if (!Out || CapacityWords < APPLE_AGX_CDM_TERMINATE_WORDS) return 0u;
  Out[0] = 2u << 29; /* block type Stream Terminate */
  Out[1] = 0u;
  return APPLE_AGX_CDM_TERMINATE_WORDS;
}

unsigned int AppleAgxUscUniform(unsigned char *Out, unsigned int Capacity,
                                unsigned int StartHalf, unsigned int Halfs,
                                unsigned long long Buffer) {
  unsigned int size;
  if (!Out || Capacity < APPLE_AGX_USC_UNIFORM_BYTES || StartHalf > 0xffu ||
      !Halfs || !to_groups(Halfs, 1u, 6u, &size) || (Buffer & 3ULL) ||
      Buffer >> 40)
    return 0u;
  put64(Out, 0x1dULL | ((unsigned long long)StartHalf << 8) |
             ((unsigned long long)size << 20) | ((Buffer >> 2) << 26));
  return APPLE_AGX_USC_UNIFORM_BYTES;
}

unsigned int AppleAgxUscProgram(unsigned char *Out, unsigned int Capacity,
                                const APPLE_AGX_USC_PROGRAM *Program) {
  unsigned int shared, registers, used = 0u;
  const unsigned int bytes = APPLE_AGX_USC_SHARED_BYTES +
      APPLE_AGX_USC_SHADER_BYTES + APPLE_AGX_USC_REGISTERS_BYTES +
      (Program && Program->PreambleOffset ? APPLE_AGX_USC_PRESHADER_BYTES :
                                            APPLE_AGX_USC_NO_PRESHADER_BYTES);
  if (!Out || !Program || Capacity < bytes || !Program->CodeOffset ||
      Program->SharedBytes > 65536u ||
      !to_groups(Program->SharedBytes ? Program->SharedBytes : 65536u, 256u, 8u,
                 &shared) ||
      !to_groups(Program->Gprs, 8u, 5u, &registers))
    return 0u;
  /* Shared: layout Vertex/compute (0x24); no threadgroup memory is encoded as
   * the full 64 KiB without "uses shared memory" (agx_usc_shared_none). */
  put32(Out + used, 0x4du | ((Program->SharedBytes ? 1u : 0u) << 8) |
                    (0x24u << 10) | (shared << 24));
  used += APPLE_AGX_USC_SHARED_BYTES;
  /* Shader: unk 2 = 3 as Mesa bakes it, code offset in bits 16..47. */
  Out[used] = 0x0du; Out[used + 1u] = (unsigned char)(3u << 2);
  put32(Out + used + 2u, Program->CodeOffset);
  used += APPLE_AGX_USC_SHADER_BYTES;
  put32(Out + used, 0x8du | (registers << 8));
  used += APPLE_AGX_USC_REGISTERS_BYTES;
  if (Program->PreambleOffset) {
    put32(Out + used, 0x38u | (0xc08000u << 8));
    put32(Out + used + 4u, Program->PreambleOffset);
    used += APPLE_AGX_USC_PRESHADER_BYTES;
  } else {
    Out[used] = 0x88u; Out[used + 1u] = 0u;
    used += APPLE_AGX_USC_NO_PRESHADER_BYTES;
  }
  return used;
}
