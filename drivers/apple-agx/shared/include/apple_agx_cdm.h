#ifndef APPLE_AGX_CDM_H
#define APPLE_AGX_CDM_H

/* G2: CDM control-stream words and USC words for a compute dispatch that
 * the KMD builds itself (present blt; later KMD-owned helper programs).
 * Word layouts are those of Mesa's src/asahi/genxml/cmdbuf.xml (CDM Launch
 * Word 0/1, Global/Local size, Barrier, Stream Terminate; USC Uniform,
 * Shared, Shader, Registers, Preshader, No Preshader); the sequences follow
 * libagx_dgc.h agx_cdm_launch/agx_cdm_barrier (G13, direct grid) and the
 * precompiled-kernel USC bake of agx_bg_eot.c. Project-owned encoder. */

#define APPLE_AGX_CDM_LAUNCH_WORDS 8u   /* word 0, word 1, global xyz, local xyz */
#define APPLE_AGX_CDM_BARRIER_WORDS 1u
#define APPLE_AGX_CDM_TERMINATE_WORDS 2u
#define APPLE_AGX_USC_UNIFORM_BYTES 8u
#define APPLE_AGX_USC_SHARED_BYTES 4u
#define APPLE_AGX_USC_SHADER_BYTES 6u
#define APPLE_AGX_USC_REGISTERS_BYTES 4u
#define APPLE_AGX_USC_PRESHADER_BYTES 8u
#define APPLE_AGX_USC_NO_PRESHADER_BYTES 2u
/* Pipeline (USC word block) and shader code addresses are offsets from the
 * USC execution base; the pipeline is 64-byte aligned (shr(6)). */
#define APPLE_AGX_USC_PIPELINE_ALIGNMENT 64u
#define APPLE_AGX_USC_MAX_UNIFORM_HALFS 64u

typedef struct {
  /* Launch word 0, in Mesa's units (agx_bg_eot.c): uniform registers are
   * 16-bit halfs (push_count), preshader registers are GPRs. */
  unsigned int UniformHalfs, PreshaderGprs;
  unsigned int TextureStates;  /* groups of 8; 0 and 8 encode alike */
  unsigned int SamplerStates;  /* "Sampler states" enum: 0, 1 = 4 compact, ... */
  unsigned int PipelineOffset; /* USC words, from the USC execution base */
  unsigned int Global[3], Local[3];
} APPLE_AGX_CDM_DISPATCH;

/* Each writer returns the number of 32-bit words written, 0 when the input
 * is not encodable or the output is too small. */
unsigned int AppleAgxCdmLaunch(unsigned int *Out, unsigned int CapacityWords,
                               const APPLE_AGX_CDM_DISPATCH *Dispatch);
/* The G13G barrier Mesa emits after every launch (agx_cdm_barrier). */
unsigned int AppleAgxCdmBarrier(unsigned int *Out, unsigned int CapacityWords);
unsigned int AppleAgxCdmTerminate(unsigned int *Out, unsigned int CapacityWords);

typedef struct {
  unsigned int CodeOffset;      /* main code, from the USC execution base */
  unsigned int PreambleOffset;  /* 0: no preshader */
  unsigned int Gprs;            /* nr_gprs */
  unsigned int SharedBytes;     /* threadgroup memory; 0: none */
} APPLE_AGX_USC_PROGRAM;

/* USC words are byte-packed records; each writer returns bytes written. */
unsigned int AppleAgxUscUniform(unsigned char *Out, unsigned int Capacity,
                                unsigned int StartHalf, unsigned int Halfs,
                                unsigned long long Buffer);
unsigned int AppleAgxUscProgram(unsigned char *Out, unsigned int Capacity,
                                const APPLE_AGX_USC_PROGRAM *Program);

#endif
