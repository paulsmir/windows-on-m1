// Metal twin of the D3D12 OPTWIN probe steps (drivers/apple-agx/windows/icd/agx_d3d12_probe.c optwin_run,
// agx_d3d12_probe_cs_optwin.h; architect 2026-11-12T14:03Z, .local/night-20261110/optwin/REPORT.md section 3), batch 1:
// S1 structure, G1 gathers, T1 sampling, B1 barriers, C1 constant-buffer rows. Everything between OPTWIN-SHARED-BEGIN
// and OPTWIN-SHARED-END is byte-identical to the D3D12 header (tests/test_optwin_probe.py): one kernel text for HLSL
// and MSL (only the macros below differ), the same constants, shapes, R, inputs, CPU oracles and output lines.
// Timing as CLKTWIN: per case one command buffer with na and with nb dispatches (buffer memory barrier between),
// per-dispatch = (Tb - Ta) / (nb - na) from GPUEndTime - GPUStartTime, best of 3; oracles first; P6 hold: 2 s of W56
// filler command buffers (the CLKTWIN W56 kernel, R 10000), then a W56 filler committed behind every timed buffer.
// Fast math is off (the D3D12 side compiles with D3DCOMPILE_ENABLE_STRICTNESS).
// Build: clang -fobjc-arc -framework Metal -framework Foundation optwin_metal.m -o optwin_metal
// Usage: optwin_metal [--archive PATH.metallib] [--archive-each DIR] [--compile-only] [S1] [G1] [T1] [B1] [C1]
//   (no probe = all, in order). --archive writes one MTLBinaryArchive of every pipeline for the code-equality gate
//   (.local/metal-oracle/dis.py); --archive-each writes DIR/optwin_<kernel>_<NTX>x<NTY>_alt<ALT>.metallib, one
//   pipeline per file, for tools/macos/optwin_metal_regs.py (highest register -> threads per core per kernel).
// Validity: run sudo powermetrics --samplers gpu_power -i 250 alongside (>= 99 % at 1278 MHz); OPTWIN_P6 prints
// p6_pct=-1 here. Expected GPU time: about 30-50 s for all five probes; the program ends by itself.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OPTWIN-SHARED-BEGIN */
#define OPTWIN_OUT_BYTES (4u << 20)          /* >= 921600 threads x 4 bytes + the S1n alternate words */
#define OPTWIN_DATA_BYTES (85u << 20)        /* G1 regions of 1, 4, 16 and 64 MB at 0, 1, 5 and 21 MB */
#define OPTWIN_REGIONS 4u
#define OPTWIN_TEX 2048u                     /* T1: 2048 x 2048 x 6 2D arrays (R32 depth, RGBA8) */
#define OPTWIN_LAYERS 6u
#define OPTWIN_TEX_LAYER_BYTES (OPTWIN_TEX * OPTWIN_TEX * 4u)
#define OPTWIN_ROWS 912u                     /* C1: cbuffer float4 rows[912] = 16 views x 57 rows */
#define OPTWIN_ROWS_PER_VIEW 57u
#define OPTWIN_VIEW 13u                      /* the runtime view index */
#define OPTWIN_S1O_NV 23u                    /* S1o live float4 (NV in the kernel text): 92 + temporaries
                                              * = 93..104 32-bit registers = 186..208 half registers, which
                                              * Asahi agx_performance.c occupancies[] maps to 512 threads/core */
#define OPTWIN_G1C_STEP 4096u                /* G1c coalesced stride in float4 elements */
#define OPTWIN_T1_REF 0.5f                   /* T1 compare reference (0.5f in the kernel text) */
#define OPTWIN_FILL_R 10000u                 /* P6-hold filler: the CLKTWIN W56 list (S1 at 1024, R 10000) */
#define OPTWIN_FILL_GROUPS 56u
#define OPTWIN_REPS 3u
#define OPTWIN_ARGS_THREADS 32u              /* S1i argument writer: one 32-thread group */
#define OPTWIN_MAX_CASES 6u

enum { OPTWIN_K_S1, OPTWIN_K_S1O, OPTWIN_K_ARGS, OPTWIN_K_G1, OPTWIN_K_G1D, OPTWIN_K_G1C, OPTWIN_K_T1, OPTWIN_K_T1L,
       OPTWIN_K_B1, OPTWIN_K_B1B, OPTWIN_K_C1, OPTWIN_KERNELS };
#define OPTWIN_RES_SRC 1u      /* StructuredBuffer<float4> src / device const float4 *src (a G1 region) */
#define OPTWIN_RES_ROWS 2u     /* cbuffer rows[912] / constant float4 *rows */
#define OPTWIN_RES_SHADOW 4u   /* Texture2DArray<float> + comparison sampler / depth2d_array + compare sampler */
#define OPTWIN_RES_COLOR 8u    /* Texture2DArray<float4> + linear sampler / texture2d_array + linear sampler */
#define OPTWIN_RES_ARGS 16u    /* RWByteAddressBuffer args / device uint *args (indirect arguments) */
#define OPTWIN_RES_SHARED 32u  /* groupshared / threadgroup uint sh[16] */

/* Root constants b0 (8 dwords) == Metal setBytes buffer(1), same order. */
typedef struct { unsigned R, mask, n, view, w; float a, b, c; } optwin_params;
typedef struct { const char *name; unsigned res, ops_per_iter, bytes_per_op; const char *op; const char *body; } optwin_kernel;
typedef struct {
    const char *probe, *variant;
    unsigned kernel, ntx, nty, region, indirect, alt;
    unsigned r_lat, r_w56, r_w224, r_gm, gm_gx, gm_gy;
    float a, b, c;
} optwin_variant;
typedef struct { const char *shape; unsigned ntx, nty, gx, gy, R, na, nb; } optwin_case;

/* Kernel bodies. Names visible to the text: gid (uint3 thread position in grid), lidx (index in group), the
 * resources output / src / rows / args / shadow / color / sh, and the macros P FMA FRAC LOOP UNROLL STORE
 * STOREU SRC ROWS ARGS3 BARRIER SAMPLE_CMP SAMPLE_LIN, NV, NTX, NTY and ALT. */
static const char optwin_body_s1[] = /* the CLKTWIN body, thread index = row-major linear index */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " float t=(float)lin;\n"
    " float x0=1.0f+t*1e-7f, x1=1.0f+t*2e-7f, x2=1.0f+t*3e-7f, x3=1.0f+t*4e-7f;\n"
    " float x4=1.0f+t*5e-7f, x5=1.0f+t*6e-7f, x6=1.0f+t*7e-7f, x7=1.0f+t*8e-7f;\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  UNROLL for(uint u=0u;u<4u;++u) {\n"
    "   x0=FMA(x0,P(pA),P(pB)); x1=FMA(x1,P(pA),P(pB)); x2=FMA(x2,P(pA),P(pB)); x3=FMA(x3,P(pA),P(pB));\n"
    "   x4=FMA(x4,P(pA),P(pB)); x5=FMA(x5,P(pA),P(pB)); x6=FMA(x6,P(pA),P(pB)); x7=FMA(x7,P(pA),P(pB)); } }\n"
    " STORE(lin+ALT,x0+x1+x2+x3+x4+x5+x6+x7);\n";
static const char optwin_body_s1o[] = /* NV live float4 chains: register pressure, ~512 threads per core (OPTWIN_SHADER) */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " float t=(float)lin;\n"
    " float4 A=float4(P(pA),P(pA),P(pA),P(pA)), B=float4(P(pB),P(pB),P(pB),P(pB));\n"
    " float4 x[NV];\n"
    " UNROLL for(uint j0=0u;j0<NV;++j0)\n"
    "  x[j0]=FMA(float4(t,t,t,t),float4((float)(4u*j0+1u),(float)(4u*j0+2u),(float)(4u*j0+3u),(float)(4u*j0+4u))*1e-7f,\n"
    "            float4(1.0f,1.0f,1.0f,1.0f));\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  UNROLL for(uint j1=0u;j1<NV;++j1) x[j1]=FMA(x[j1],A,B); }\n"
    " float s=0.0f;\n"
    " UNROLL for(uint j2=0u;j2<NV;++j2) { s=s+x[j2].x; s=s+x[j2].y; s=s+x[j2].z; s=s+x[j2].w; }\n"
    " STORE(lin,s);\n";
static const char optwin_body_args[] = /* S1i: the GPU writes the dispatch arguments */
    " if(gid.x==0u) ARGS3(P(pN),P(pView),1u);\n";
static const char optwin_body_g1[] = /* independent gathers: idx = lcg(idx + tid) & mask */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " uint idx=lin;\n"
    " float4 acc=float4(0.0f,0.0f,0.0f,0.0f);\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  UNROLL for(uint u=0u;u<4u;++u) {\n"
    "   idx=(idx+lin)*1664525u+1013904223u;\n"
    "   acc+=SRC(idx&P(pMask)); } }\n"
    " STORE(lin,acc.x+acc.y+acc.z+acc.w);\n";
static const char optwin_body_g1d[] = /* dependent chain: idx = perm[idx] (the element's w holds the next index) */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " uint idx=lin&P(pMask);\n"
    " float4 acc=float4(0.0f,0.0f,0.0f,0.0f);\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  UNROLL for(uint u=0u;u<4u;++u) {\n"
    "   float4 v=SRC(idx);\n"
    "   acc+=v; idx=(uint)v.w; } }\n"
    " STORE(lin,acc.x+acc.y+acc.z+acc.w);\n";
static const char optwin_body_g1c[] = /* coalesced control: consecutive lanes, consecutive elements */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " uint idx=lin&P(pMask);\n"
    " float4 acc=float4(0.0f,0.0f,0.0f,0.0f);\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  UNROLL for(uint u=0u;u<4u;++u) {\n"
    "   idx=(idx+P(pN))&P(pMask);\n"
    "   acc+=SRC(idx); } }\n"
    " STORE(lin,acc.x+acc.y+acc.z+acc.w);\n";
static const char optwin_body_t1[] = /* depth compare: uv = frac(uv*a + b), SampleCmpLevelZero, layer = i % 6 */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " float2 uv=float2((float)gid.x,(float)gid.y)*0.00048828125f;\n"
    " float2 A=float2(P(pA),P(pA)), st=float2(P(pB),P(pC));\n"
    " float acc=0.0f;\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  uint layer=i%6u;\n"
    "  UNROLL for(uint u=0u;u<4u;++u) {\n"
    "   uv=FRAC(FMA(uv,A,st));\n"
    "   acc+=SAMPLE_CMP(uv,layer,0.5f); } }\n"
    " STORE(lin,acc);\n";
static const char optwin_body_t1l[] = /* filtered RGBA8: the same walk, SampleLevel 0 with a linear sampler */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " float2 uv=float2((float)gid.x,(float)gid.y)*0.00048828125f;\n"
    " float2 A=float2(P(pA),P(pA)), st=float2(P(pB),P(pC));\n"
    " float4 acc=float4(0.0f,0.0f,0.0f,0.0f);\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  uint layer=i%6u;\n"
    "  UNROLL for(uint u=0u;u<4u;++u) {\n"
    "   uv=FRAC(FMA(uv,A,st));\n"
    "   acc+=SAMPLE_LIN(uv,layer); } }\n"
    " STORE(lin,acc.x+acc.y+acc.z+acc.w);\n";
static const char optwin_body_b1[] = /* thread 0 writes 16 words; barrier; all threads read them; barrier */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " uint acc=lin;\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  if(lidx==0u) { UNROLL for(uint k0=0u;k0<16u;++k0) sh[k0]=acc+k0+i; }\n"
    "  BARRIER;\n"
    "  UNROLL for(uint k1=0u;k1<16u;++k1) acc=acc*1664525u+sh[k1];\n"
    "  BARRIER; }\n"
    " STOREU(lin,acc);\n";
static const char optwin_body_b1b[] = /* barrier pair only */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " uint acc=lin;\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  BARRIER;\n"
    "  acc=acc*1664525u+i;\n"
    "  BARRIER; }\n"
    " STOREU(lin,acc);\n";
static const char optwin_body_c1[] = /* 16 x dot(rows[view*57 + j], p), runtime view, row index varies per iteration */
    " uint lin=gid.y*P(pW)+gid.x;\n"
    " float t=(float)lin;\n"
    " float4 pp=float4(t,t+1.0f,t+2.0f,t+3.0f)*1e-6f;\n"
    " float4 A=float4(P(pA),P(pA),P(pA),P(pA)), B=float4(P(pB),P(pB),P(pB),P(pB));\n"
    " uint base=P(pView)*57u;\n"
    " float acc=0.0f;\n"
    " LOOP for(uint i=0u;i<P(pR);++i) {\n"
    "  UNROLL for(uint j=0u;j<16u;++j) acc+=dot(ROWS(base+((i+j)&31u)),pp);\n"
    "  pp=FMA(pp,A,B); }\n"
    " STORE(lin,acc);\n";
/* Language-neutral defines prepended to every kernel on both sides. */
static const char optwin_common_src[] = "#define NV 23u\n";

static const optwin_kernel optwin_kernels[OPTWIN_KERNELS] = {
    {"S1", 0u, 32u, 0u, "ffma", optwin_body_s1},
    {"S1o", 0u, 92u, 0u, "ffma", optwin_body_s1o},
    {"ARGS", OPTWIN_RES_ARGS, 0u, 0u, "none", optwin_body_args},
    {"G1", OPTWIN_RES_SRC, 4u, 16u, "load", optwin_body_g1},
    {"G1d", OPTWIN_RES_SRC, 4u, 16u, "load", optwin_body_g1d},
    {"G1c", OPTWIN_RES_SRC, 4u, 16u, "load", optwin_body_g1c},
    {"T1", OPTWIN_RES_SHADOW, 4u, 0u, "sample", optwin_body_t1},
    {"T1l", OPTWIN_RES_COLOR, 4u, 0u, "sample", optwin_body_t1l},
    {"B1", OPTWIN_RES_SHARED, 1u, 0u, "barrier_pair", optwin_body_b1},
    {"B1b", 0u, 1u, 0u, "barrier_pair", optwin_body_b1b},
    {"C1", OPTWIN_RES_ROWS, 16u, 16u, "row_load", optwin_body_c1},
};

/* Batch 1, in run order. Shapes per variant: N32 (one 32x1 group, the variant's R_lat), L1 / L8 / W56 / W224 =
 * 1024 / 8192 / 57344 / 229376 threads in the variant's group size (8x8 groups: 4x4 / 16x8 / 32x28 / 64x56),
 * plus GM, the GravityMark shape of the class. R per shape: N32/L1/L8 r_lat, W56 r_w56, W224 r_w224, GM r_gm
 * (r_lat chosen so L1 is about 1 ms where the CLKTWIN rate allows an estimate; wide shapes cut so a variant
 * stays near 1.5 s). S1n: two cases N32x9 and N32x33 (1 vs 9 / 33 dispatches, two pipelines alternating).
 * CALIBRATION (architect 2026-11-12): after the first ours run, a variant whose L1 per_dispatch_ms is < 0.3 or
 * > 5 gets its R columns changed HERE ONLY (this table is shared by both sides); every OPTWIN_CASE line prints R. */
#define OPTWIN_A 0.999f
#define OPTWIN_B 0.001f
static const optwin_variant optwin_variants[] = {
    /* probe variant kernel ntx nty region indirect alt  r_lat r_w56 r_w224 r_gm gm_gx gm_gy  a b c */
    {"S1", "S1_64", OPTWIN_K_S1, 8u, 8u, 0u, 0u, 0u, 4096u, 4096u, 4096u, 1024u, 160u, 90u, OPTWIN_A, OPTWIN_B, 0.0f},
    {"S1", "S1_128", OPTWIN_K_S1, 128u, 1u, 0u, 0u, 0u, 4096u, 4096u, 4096u, 1024u, 7200u, 1u, OPTWIN_A, OPTWIN_B, 0.0f},
    {"S1", "S1_256", OPTWIN_K_S1, 256u, 1u, 0u, 0u, 0u, 4096u, 4096u, 4096u, 1024u, 3600u, 1u, OPTWIN_A, OPTWIN_B, 0.0f},
    {"S1", "S1_1024", OPTWIN_K_S1, 1024u, 1u, 0u, 0u, 0u, 4096u, 4096u, 4096u, 1024u, 900u, 1u, OPTWIN_A, OPTWIN_B, 0.0f},
    {"S1", "S1o", OPTWIN_K_S1O, 8u, 8u, 0u, 0u, 0u, 1400u, 1400u, 1400u, 350u, 160u, 90u, OPTWIN_A, OPTWIN_B, 0.0f},
    {"S1", "S1i", OPTWIN_K_S1, 1024u, 1u, 0u, 1u, 0u, 4096u, 4096u, 4096u, 1024u, 900u, 1u, OPTWIN_A, OPTWIN_B, 0.0f},
    {"S1", "S1n", OPTWIN_K_S1, 32u, 1u, 0u, 0u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, OPTWIN_A, OPTWIN_B, 0.0f},
    {"G1", "G1_1M", OPTWIN_K_G1, 256u, 1u, 0u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1_4M", OPTWIN_K_G1, 256u, 1u, 1u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1_16M", OPTWIN_K_G1, 256u, 1u, 2u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1_64M", OPTWIN_K_G1, 256u, 1u, 3u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1d_1M", OPTWIN_K_G1D, 256u, 1u, 0u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1d_4M", OPTWIN_K_G1D, 256u, 1u, 1u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1d_16M", OPTWIN_K_G1D, 256u, 1u, 2u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1d_64M", OPTWIN_K_G1D, 256u, 1u, 3u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1c_1M", OPTWIN_K_G1C, 256u, 1u, 0u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"G1", "G1c_64M", OPTWIN_K_G1C, 256u, 1u, 3u, 0u, 0u, 128u, 32u, 8u, 16u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"T1", "T1", OPTWIN_K_T1, 8u, 8u, 0u, 0u, 0u, 256u, 64u, 16u, 16u, 160u, 90u, 1.0f, 0.1234567f, 0.0765432f},
    {"T1", "T1l", OPTWIN_K_T1L, 8u, 8u, 0u, 0u, 0u, 256u, 64u, 16u, 16u, 160u, 90u, 1.0f, 0.1234567f, 0.0765432f},
    {"B1", "B1_128", OPTWIN_K_B1, 128u, 1u, 0u, 0u, 0u, 4096u, 2048u, 512u, 512u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"B1", "B1_256", OPTWIN_K_B1, 256u, 1u, 0u, 0u, 0u, 4096u, 2048u, 512u, 512u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"B1", "B1b_128", OPTWIN_K_B1B, 128u, 1u, 0u, 0u, 0u, 4096u, 2048u, 512u, 512u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"B1", "B1b_256", OPTWIN_K_B1B, 256u, 1u, 0u, 0u, 0u, 4096u, 2048u, 512u, 512u, 1840u, 1u, 0.0f, 0.0f, 0.0f},
    {"C1", "C1", OPTWIN_K_C1, 128u, 1u, 0u, 0u, 0u, 1024u, 256u, 64u, 64u, 1840u, 1u, OPTWIN_A, OPTWIN_B, 0.0f},
};
#define OPTWIN_VARIANTS (sizeof(optwin_variants) / sizeof(optwin_variants[0]))

static unsigned optwin_cases(const optwin_variant *v, optwin_case *out) {
    static const char *const names[4] = {"L1", "L8", "W56", "W224"};
    static const unsigned threads[4] = {1024u, 8192u, 57344u, 229376u};
    static const unsigned g2x[4] = {4u, 16u, 32u, 64u}, g2y[4] = {4u, 8u, 28u, 56u};
    unsigned s, n = 0u;
    if (v->alt) {
        optwin_case c9 = {"N32x9", 32u, 1u, 1u, 1u, v->r_lat, 1u, 9u}, c33 = {"N32x33", 32u, 1u, 1u, 1u, v->r_lat, 1u, 33u};
        out[n++] = c9; out[n++] = c33;
        return n;
    }
    {
        optwin_case c = {"N32", 32u, 1u, 1u, 1u, v->r_lat, 1u, 5u};
        out[n++] = c;
    }
    for (s = 0u; s < 4u; ++s) {
        optwin_case c = {names[s], v->ntx, v->nty, 0u, 0u, 0u, 1u, 5u};
        c.gx = v->nty > 1u ? g2x[s] : threads[s] / v->ntx;
        c.gy = v->nty > 1u ? g2y[s] : 1u;
        c.R = s < 2u ? v->r_lat : s == 2u ? v->r_w56 : v->r_w224;
        out[n++] = c;
    }
    {
        optwin_case c = {"GM", v->ntx, v->nty, v->gm_gx, v->gm_gy, v->r_gm, 1u, 5u};
        out[n++] = c;
    }
    return n;
}
static unsigned optwin_threads(const optwin_case *c) { return c->gx * c->gy * c->ntx * c->nty; }
static unsigned optwin_region_elems(unsigned r) { return 65536u << (2u * r); }
static unsigned optwin_region_offset(unsigned r) { return (((1u << (2u * r)) - 1u) / 3u) << 20; }
static optwin_params optwin_case_params(const optwin_variant *v, const optwin_case *c) {
    optwin_params p;
    p.R = c->R;
    p.mask = optwin_region_elems(v->region) - 1u;
    p.n = v->kernel == OPTWIN_K_G1C ? OPTWIN_G1C_STEP : 0u;
    p.view = v->kernel == OPTWIN_K_C1 ? OPTWIN_VIEW : 0u;
    p.w = c->gx * c->ntx;
    p.a = v->a; p.b = v->b; p.c = v->c;
    if (v->indirect) { p.n = c->gx; p.view = c->gy; } /* the argument writer's groups */
    return p;
}
static optwin_params optwin_filler_params(void) {
    optwin_params p = {OPTWIN_FILL_R, 0u, 0u, 0u, OPTWIN_FILL_GROUPS * 1024u, OPTWIN_A, OPTWIN_B, 0.0f};
    return p;
}

/* Deterministic inputs. */
static unsigned optwin_hash(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static float optwin_unit16(unsigned h) { return (float)(h & 0xffffu) * (1.0f / 65536.0f); }
/* G1 data: per region a float4 array; x, y, z in [0, 1), w = the next index of one Sattolo cycle over the region
 * (a single cycle, exact in float since every region has < 2^24 elements). Returns 0 on allocation failure. */
static int optwin_fill_data(float *out) {
    unsigned r;
    for (r = 0u; r < OPTWIN_REGIONS; ++r) {
        const unsigned n = optwin_region_elems(r);
        float *base = out + optwin_region_offset(r) / 4u;
        unsigned *perm = (unsigned *)malloc((size_t)n * sizeof(unsigned));
        unsigned long long state = 0x9e3779b97f4a7c15ull ^ (unsigned long long)r;
        unsigned i;
        if (!perm) return 0;
        for (i = 0u; i < n; ++i) perm[i] = i;
        for (i = n - 1u; i > 0u; --i) {
            unsigned j, tmp;
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            j = (unsigned)((state >> 33) % (unsigned long long)i);
            tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
        }
        for (i = 0u; i < n; ++i) {
            base[4u * i + 0u] = optwin_unit16(optwin_hash(4u * i + 0u + (r << 28)));
            base[4u * i + 1u] = optwin_unit16(optwin_hash(4u * i + 1u + (r << 28)));
            base[4u * i + 2u] = optwin_unit16(optwin_hash(4u * i + 2u + (r << 28)));
            base[4u * i + 3u] = (float)perm[i];
        }
        free(perm);
    }
    return 1;
}
static float optwin_depth(unsigned x, unsigned y, unsigned layer) {
    return (float)(optwin_hash(x + OPTWIN_TEX * (y + OPTWIN_TEX * layer)) >> 8) * (1.0f / 16777216.0f);
}
static unsigned optwin_color(unsigned x, unsigned y, unsigned layer) { /* RGBA8 texel, R in the low byte */
    return optwin_hash((x + OPTWIN_TEX * (y + OPTWIN_TEX * layer)) ^ 0x9e3779b9u);
}
static float optwin_row(unsigned k, unsigned c) { return optwin_unit16(optwin_hash(4u * k + c + 0x51ed270bu)) - 0.5f; }

/* CPU oracles. */
static float optwin_mad(float x, float a, float b, int fused) {
    volatile float m;
    if (fused) return fmaf(x, a, b);
    m = x * a;
    return m + b;
}
static float optwin_expect_s1(unsigned lin, unsigned R, float a, float b, int init_fused, int loop_fused) {
    static const float k[8] = {1e-7f, 2e-7f, 3e-7f, 4e-7f, 5e-7f, 6e-7f, 7e-7f, 8e-7f};
    const float t = (float)lin;
    float x[8];
    volatile float s;
    unsigned i, j;
    for (j = 0u; j < 8u; ++j) x[j] = optwin_mad(t, k[j], 1.0f, init_fused);
    for (i = 0u; i < R * 4u; ++i)
        for (j = 0u; j < 8u; ++j) x[j] = optwin_mad(x[j], a, b, loop_fused);
    s = x[0];
    for (j = 1u; j < 8u; ++j) s = s + x[j];
    return s;
}
static float optwin_expect_s1o(unsigned lin, unsigned R, float a, float b, int init_fused, int loop_fused) {
    float x[OPTWIN_S1O_NV * 4u];
    const float t = (float)lin;
    volatile float s = 0.0f;
    unsigned i, j;
    for (j = 0u; j < OPTWIN_S1O_NV * 4u; ++j) x[j] = optwin_mad(t, (float)(j + 1u) * 1e-7f, 1.0f, init_fused);
    for (i = 0u; i < R; ++i)
        for (j = 0u; j < OPTWIN_S1O_NV * 4u; ++j) x[j] = optwin_mad(x[j], a, b, loop_fused);
    for (j = 0u; j < OPTWIN_S1O_NV * 4u; ++j) s = s + x[j];
    return s;
}
static float optwin_expect_g1(unsigned kernel, unsigned lin, const optwin_params *p, const float *region) {
    volatile float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    volatile float s;
    unsigned idx = kernel == OPTWIN_K_G1 ? lin : lin & p->mask, i, c;
    for (i = 0u; i < p->R * 4u; ++i) {
        const float *v;
        if (kernel == OPTWIN_K_G1) { idx = (idx + lin) * 1664525u + 1013904223u; v = region + 4u * (idx & p->mask); }
        else if (kernel == OPTWIN_K_G1C) { idx = (idx + p->n) & p->mask; v = region + 4u * idx; }
        else v = region + 4u * idx;
        for (c = 0u; c < 4u; ++c) acc[c] = acc[c] + v[c];
        if (kernel == OPTWIN_K_G1D) idx = (unsigned)v[3];
    }
    s = acc[0]; s = s + acc[1]; s = s + acc[2]; s = s + acc[3];
    return s;
}
static unsigned optwin_expect_b1(unsigned kernel, unsigned lin, unsigned group_base, unsigned R) {
    unsigned acc = lin, acc0 = group_base, i, k, sh[16];
    for (i = 0u; i < R; ++i) {
        if (kernel == OPTWIN_K_B1B) { acc = acc * 1664525u + i; continue; }
        for (k = 0u; k < 16u; ++k) sh[k] = acc0 + k + i;
        for (k = 0u; k < 16u; ++k) { acc = acc * 1664525u + sh[k]; acc0 = acc0 * 1664525u + sh[k]; }
    }
    return acc;
}
static int optwin_clampi(int v) { return v < 0 ? 0 : v > (int)OPTWIN_TEX - 1 ? (int)OPTWIN_TEX - 1 : v; }
/* Bilinear filter at (u, v) with clamp-to-edge: depth compare (ref <= texel) or the RGBA8 channel sum. */
static float optwin_bilinear(float u, float v, unsigned layer, int color) {
    const float x = u * (float)OPTWIN_TEX - 0.5f, y = v * (float)OPTWIN_TEX - 0.5f;
    const float fx = floorf(x), fy = floorf(y), wx = x - fx, wy = y - fy;
    float t[4];
    unsigned q;
    for (q = 0u; q < 4u; ++q) {
        const unsigned tx = (unsigned)optwin_clampi((int)fx + (int)(q & 1u)), ty = (unsigned)optwin_clampi((int)fy + (int)(q >> 1));
        if (color) {
            const unsigned c = optwin_color(tx, ty, layer);
            t[q] = (float)((c & 0xffu) + ((c >> 8) & 0xffu) + ((c >> 16) & 0xffu) + (c >> 24)) / 255.0f;
        } else t[q] = OPTWIN_T1_REF <= optwin_depth(tx, ty, layer) ? 1.0f : 0.0f;
    }
    return (1.0f - wx) * (1.0f - wy) * t[0] + wx * (1.0f - wy) * t[1] + (1.0f - wx) * wy * t[2] + wx * wy * t[3];
}
static double optwin_expect_t1(unsigned kernel, unsigned x, unsigned y, const optwin_params *p) {
    float u = (float)x * 0.00048828125f, v = (float)y * 0.00048828125f;
    double acc = 0.0;
    unsigned i, k;
    for (i = 0u; i < p->R; ++i)
        for (k = 0u; k < 4u; ++k) {
            volatile float su = u * p->a, sv = v * p->a; /* a = 1: fused and unfused agree bit for bit */
            su = su + p->b; sv = sv + p->c;
            u = su - floorf(su); v = sv - floorf(sv);
            acc += optwin_bilinear(u, v, i % 6u, kernel == OPTWIN_K_T1L);
        }
    return acc;
}
static double optwin_expect_c1(unsigned lin, const optwin_params *p) {
    const float t = (float)lin;
    float pp[4];
    double acc = 0.0;
    unsigned i, j, c;
    pp[0] = t * 1e-6f; pp[1] = (t + 1.0f) * 1e-6f; pp[2] = (t + 2.0f) * 1e-6f; pp[3] = (t + 3.0f) * 1e-6f;
    for (i = 0u; i < p->R; ++i) {
        for (j = 0u; j < 16u; ++j) {
            const unsigned row = p->view * OPTWIN_ROWS_PER_VIEW + ((i + j) & 31u);
            for (c = 0u; c < 4u; ++c) acc += (double)optwin_row(row, c) * (double)pp[c];
        }
        for (c = 0u; c < 4u; ++c) pp[c] = fmaf(pp[c], p->a, p->b);
    }
    return acc;
}
static unsigned optwin_f2u(float f) { unsigned u; memcpy(&u, &f, 4); return u; }
static float optwin_u2f(unsigned u) { float f; memcpy(&f, &u, 4); return f; }
/* One lane of the oracle dispatch (the variant's L1 case, or N32x9 for S1n, one dispatch, pipeline A). Exact
 * kernels compare bits (S1 / S1o in all four init/loop fused forms); T1, T1l and C1 compare within a tolerance
 * (texture filter weights, dot-product rounding). Returns 1 on a match and names the form. */
static int optwin_oracle_lane(const optwin_variant *v, const optwin_case *c, unsigned lin, unsigned gpu,
                              const float *data, char *form, size_t form_size) {
    const optwin_params p = optwin_case_params(v, c);
    const unsigned w = c->gx * c->ntx;
    unsigned f;
    if (v->kernel == OPTWIN_K_S1 || v->kernel == OPTWIN_K_S1O) {
        static const char *const forms[4] = {"init=U loop=U", "init=U loop=F", "init=F loop=U", "init=F loop=F"};
        for (f = 4u; f-- > 0u;) {
            const float e = v->kernel == OPTWIN_K_S1 ? optwin_expect_s1(lin, p.R, p.a, p.b, (int)(f >> 1), (int)(f & 1u))
                                                     : optwin_expect_s1o(lin, p.R, p.a, p.b, (int)(f >> 1), (int)(f & 1u));
            if (optwin_f2u(e) == gpu) { snprintf(form, form_size, "%s", forms[f]); return 1; }
        }
        snprintf(form, form_size, "NEITHER fused 0x%08x unfused 0x%08x",
                 optwin_f2u(v->kernel == OPTWIN_K_S1 ? optwin_expect_s1(lin, p.R, p.a, p.b, 1, 1) : optwin_expect_s1o(lin, p.R, p.a, p.b, 1, 1)),
                 optwin_f2u(v->kernel == OPTWIN_K_S1 ? optwin_expect_s1(lin, p.R, p.a, p.b, 0, 0) : optwin_expect_s1o(lin, p.R, p.a, p.b, 0, 0)));
        return 0;
    }
    if (v->kernel == OPTWIN_K_G1 || v->kernel == OPTWIN_K_G1D || v->kernel == OPTWIN_K_G1C) {
        const unsigned e = optwin_f2u(optwin_expect_g1(v->kernel, lin, &p, data + optwin_region_offset(v->region) / 4u));
        snprintf(form, form_size, "exact expect 0x%08x", e);
        return e == gpu;
    }
    if (v->kernel == OPTWIN_K_B1 || v->kernel == OPTWIN_K_B1B) {
        const unsigned e = optwin_expect_b1(v->kernel, lin, lin - lin % (c->ntx * c->nty), p.R);
        snprintf(form, form_size, "exact expect 0x%08x", e);
        return e == gpu;
    }
    {
        const double e = v->kernel == OPTWIN_K_C1 ? optwin_expect_c1(lin, &p) : optwin_expect_t1(v->kernel, lin % w, lin / w, &p);
        const double tol = v->kernel == OPTWIN_K_T1 ? (double)(4u * p.R) / 64.0
                         : v->kernel == OPTWIN_K_T1L ? (double)p.R / 8.0
                         : 1e-4 * (double)(16u * p.R) + 1e-3 * fabs(e);
        const double g = (double)optwin_u2f(gpu);
        snprintf(form, form_size, "tolerance expect %.4f gpu %.4f tol %.4f", e, g, tol);
        return fabs(g - e) <= tol;
    }
}

/* Output lines: identical keys on both sides (fw_ms = firmware compute Start/End on D3D12, GPUEnd - GPUStart on
 * Metal). p6_pct is the Wom1GpuPerf P6 share over the variant's block on D3D12, -1 on macOS (powermetrics). */
/* span_ms: first job start to last job end of the list (Metal: GPUEndTime - GPUStartTime); jobsum_ms: the sum of
 * the list's jobs' own start-to-end times (-1 where there is no per-job timing, i.e. Metal). */
static void optwin_print_rep(unsigned rep, const optwin_variant *v, const optwin_case *c, unsigned dispatches, int got,
                             double span_ms, double jobsum_ms, double wall_ms, unsigned jobs) {
    printf("OPTWIN_REP rep=%u variant=%s shape=%s dispatches=%u span_ms=%s%.4f jobsum_ms=%.4f wall_ms=%.3f jobs=%u\n", rep,
           v->variant, c->shape, dispatches, got ? "" : "missing", got ? span_ms : 0.0, got ? jobsum_ms : -1.0, wall_ms, jobs);
}
static void optwin_print_case(const optwin_variant *v, const optwin_case *c, const double best[2], const double wall[2],
                              double p6_pct) {
    const optwin_kernel *k = &optwin_kernels[v->kernel];
    const double threads = (double)optwin_threads(c), groups = (double)(c->gx * c->gy);
    const double per = best[0] < 1e29 && best[1] < 1e29 ? (best[1] - best[0]) / (double)(c->nb - c->na) : -1.0;
    const double ops = threads * (double)c->R * (double)k->ops_per_iter;
    printf("OPTWIN_CASE probe=%s variant=%s shape=%s kernel=%s nt=%ux%u groups=%ux%u threads=%u R=%u region_mb=%u "
           "indirect=%u alt=%u na=%u nb=%u ta_ms=%.4f tb_ms=%.4f per_dispatch_ms=%.4f per_group_ns=%.3f op=%s "
           "simd_op_ns=%.4f gbps=%.3f wall_a_ms=%.3f wall_b_ms=%.3f p6_pct=%.2f\n",
           v->probe, v->variant, c->shape, k->name, c->ntx, c->nty, c->gx, c->gy, optwin_threads(c), c->R,
           v->kernel == OPTWIN_K_G1 || v->kernel == OPTWIN_K_G1D || v->kernel == OPTWIN_K_G1C ? 1u << (2u * v->region) : 0u,
           v->indirect, v->alt, c->na, c->nb, best[0], best[1], per, per > 0.0 ? per * 1e6 / groups : -1.0, k->op,
           per > 0.0 && ops > 0.0 ? per * 1e6 / (ops / 32.0) : -1.0,
           per > 0.0 && k->bytes_per_op ? ops * (double)k->bytes_per_op / (per * 1e6) : 0.0, wall[0], wall[1], p6_pct);
}
/* busy_ms: the GPU time of the variant's own timed lists (sum of span_ms); block_ms: wall time of the block. The
 * P6 delta covers sampled_ms, which also holds the W56 fillers around the block. */
static void optwin_print_p6(const optwin_variant *v, double p6_pct, double sampled_ms, unsigned gaps, int valid,
                            double busy_ms, double block_ms) {
    printf("OPTWIN_P6 variant=%s p6_pct=%.2f sampled_ms=%.1f gaps=%u valid=%d busy_ms=%.1f block_ms=%.1f\n", v->variant,
           p6_pct, sampled_ms, gaps, valid, busy_ms, block_ms);
}
/* One line per pipeline. D3D12: hash = FNV-1a 64 of the DXBC program tokens, the key of the UMD's
 * measure-d3d12-shader-stats row (values 0-1), from which threads_per_core / gprs16 come; -1 when the trace row
 * is not available (source names where to join). Metal: threads_per_core -1 here; tools/macos/optwin_metal_regs.py
 * prints the highest register of each --archive-each file. */
static void optwin_print_shader(const char *kernel, unsigned ntx, unsigned nty, unsigned alt, unsigned long long hash,
                                int threads_per_core, int gprs16, int instrs, int spills, int fills, const char *source) {
    printf("OPTWIN_SHADER kernel=%s nt=%ux%u alt=%u hash=%016llx threads_per_core=%d gprs16=%d instrs=%d spills=%d "
           "fills=%d source=%s\n", kernel, ntx, nty, alt, hash, threads_per_core, gprs16, instrs, spills, fills, source);
}
/* OPTWIN-SHARED-END */

static NSString *const optwin_msl_preamble = @"#include <metal_stdlib>\nusing namespace metal;\n"
  "struct OptwinParams { uint pR; uint pMask; uint pN; uint pView; uint pW; float pA; float pB; float pC; };\n"
  "#define P(x) p.x\n"
  "#define FMA(x,y,z) fma(x,y,z)\n"
  "#define FRAC(x) fract(x)\n"
  "#define LOOP\n"
  "#define UNROLL _Pragma(\"unroll\")\n"
  "#define STORE(i,v) output[(i)]=as_type<uint>(v)\n"
  "#define STOREU(i,v) output[(i)]=(v)\n"
  "#define SRC(i) src[(i)]\n"
  "#define ROWS(i) rows[(i)]\n"
  "#define ARGS3(x,y,z) { args[0]=(x); args[1]=(y); args[2]=(z); }\n"
  "#define BARRIER threadgroup_barrier(mem_flags::mem_threadgroup)\n"
  "#define SAMPLE_CMP(uv,layer,ref) shadow.sample_compare(cmp,(uv),(layer),(ref),level(0))\n"
  "#define SAMPLE_LIN(uv,layer) color.sample(lsamp,(uv),(layer),level(0.0f))\n";

typedef struct {
  id<MTLDevice> dev; id<MTLCommandQueue> q; MTLCompileOptions *opt;
  NSMutableDictionary<NSNumber *, id<MTLComputePipelineState>> *pso;
  NSMutableArray<MTLComputePipelineDescriptor *> *descs;
  NSMutableArray<NSString *> *labels;
  id<MTLBuffer> out, data, rows, args, staging;
  id<MTLTexture> shadow, color;
  id<MTLSamplerState> cmp, lin;
  float *cpu_data;
  unsigned oracle_bad;
} optwin_ctx;

static id<MTLComputePipelineState> optwin_pso(optwin_ctx *x, unsigned kernel, unsigned ntx, unsigned nty, unsigned alt) {
  NSNumber *key = @(kernel + 16u * (ntx + 2048u * (nty + 16u * (alt ? 1u : 0u))));
  id<MTLComputePipelineState> ps = x->pso[key];
  if (ps) return ps;
  const optwin_kernel *k = &optwin_kernels[kernel];
  NSMutableString *s = [NSMutableString stringWithString:optwin_msl_preamble];
  [s appendFormat:@"%s#define NTX %u\n#define NTY %u\n#define ALT %uu\n", optwin_common_src, ntx, nty, alt];
  [s appendString:@"[[max_total_threads_per_threadgroup(NTX*NTY)]] kernel void k(device uint *output [[buffer(0)]],"
                   " constant OptwinParams &p [[buffer(1)]]"];
  if (k->res & OPTWIN_RES_SRC) [s appendString:@", device const float4 *src [[buffer(2)]]"];
  if (k->res & OPTWIN_RES_ROWS) [s appendString:@", constant float4 *rows [[buffer(3)]]"];
  if (k->res & OPTWIN_RES_ARGS) [s appendString:@", device uint *args [[buffer(4)]]"];
  if (k->res & OPTWIN_RES_SHADOW) [s appendString:@", depth2d_array<float> shadow [[texture(0)]], sampler cmp [[sampler(0)]]"];
  if (k->res & OPTWIN_RES_COLOR) [s appendString:@", texture2d_array<float> color [[texture(1)]], sampler lsamp [[sampler(1)]]"];
  [s appendString:@", uint3 gid [[thread_position_in_grid]], uint lidx [[thread_index_in_threadgroup]]) {\n"];
  if (k->res & OPTWIN_RES_SHARED) [s appendString:@" threadgroup uint sh[16];\n"];
  [s appendFormat:@"%s}\n", k->body];
  NSError *e = nil;
  id<MTLLibrary> l = [x->dev newLibraryWithSource:s options:x->opt error:&e];
  if (!l) { printf("OPTWIN compiler %s: %s\n", k->name, e.localizedDescription.UTF8String); return nil; }
  MTLComputePipelineDescriptor *pd = [MTLComputePipelineDescriptor new];
  pd.computeFunction = [l newFunctionWithName:@"k"];
  pd.label = [NSString stringWithFormat:@"optwin_%s_%ux%u_alt%u", k->name, ntx, nty, alt];
  optwin_print_shader(k->name, ntx, nty, alt, 0ull, -1, -1, -1, -1, -1, "metal-archive-each(optwin_metal_regs.py)");
  ps = [x->dev newComputePipelineStateWithDescriptor:pd options:0 reflection:nil error:&e];
  printf("OPTWIN pso kernel %s nt %ux%u alt %u max_threads %lu %s\n", k->name, ntx, nty, alt,
         ps ? (unsigned long)ps.maxTotalThreadsPerThreadgroup : 0ul, ps ? "ok" : e.localizedDescription.UTF8String);
  if (!ps) return nil;
  x->pso[key] = ps;
  [x->descs addObject:pd]; [x->labels addObject:pd.label];
  return ps;
}

static void optwin_blit_wait(optwin_ctx *x, void (^fill)(id<MTLBlitCommandEncoder>)) {
  id<MTLCommandBuffer> cb = [x->q commandBuffer];
  id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
  fill(b);
  [b endEncoding]; [cb commit]; [cb waitUntilCompleted];
}
static id<MTLBuffer> optwin_private_buffer(optwin_ctx *x, const void *bytes, NSUInteger size) {
  id<MTLBuffer> dst = [x->dev newBufferWithLength:size options:MTLResourceStorageModePrivate];
  memcpy(x->staging.contents, bytes, size);
  optwin_blit_wait(x, ^(id<MTLBlitCommandEncoder> b) { [b copyFromBuffer:x->staging sourceOffset:0 toBuffer:dst destinationOffset:0 size:size]; });
  return dst;
}
static id<MTLTexture> optwin_texture(optwin_ctx *x, int color) {
  MTLTextureDescriptor *td = [MTLTextureDescriptor new];
  td.textureType = MTLTextureType2DArray; td.pixelFormat = color ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatDepth32Float;
  td.width = OPTWIN_TEX; td.height = OPTWIN_TEX; td.arrayLength = OPTWIN_LAYERS; td.mipmapLevelCount = 1;
  td.storageMode = MTLStorageModePrivate; td.usage = MTLTextureUsageShaderRead;
  id<MTLTexture> t = [x->dev newTextureWithDescriptor:td];
  unsigned *texels = (unsigned *)x->staging.contents;
  for (unsigned l = 0; l < OPTWIN_LAYERS; ++l)
    for (unsigned y = 0; y < OPTWIN_TEX; ++y)
      for (unsigned i = 0; i < OPTWIN_TEX; ++i) {
        unsigned *texel = &texels[(l * OPTWIN_TEX + y) * OPTWIN_TEX + i];
        if (color) *texel = optwin_color(i, y, l);
        else { const float d = optwin_depth(i, y, l); memcpy(texel, &d, 4); } }
  optwin_blit_wait(x, ^(id<MTLBlitCommandEncoder> b) {
    for (unsigned l = 0; l < OPTWIN_LAYERS; ++l)
      [b copyFromBuffer:x->staging sourceOffset:(NSUInteger)l * OPTWIN_TEX_LAYER_BYTES sourceBytesPerRow:OPTWIN_TEX * 4u
          sourceBytesPerImage:OPTWIN_TEX_LAYER_BYTES sourceSize:MTLSizeMake(OPTWIN_TEX, OPTWIN_TEX, 1) toTexture:t
          destinationSlice:l destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)]; });
  printf("OPTWIN texture %s layers %u\n", color ? "RGBA8" : "Depth32Float", OPTWIN_LAYERS);
  return t;
}

static id<MTLCommandBuffer> optwin_encode(optwin_ctx *x, const optwin_variant *v, const optwin_case *c, unsigned n,
                                          id<MTLBuffer> out) {
  const optwin_params p = optwin_case_params(v, c);
  const unsigned res = optwin_kernels[v->kernel].res;
  id<MTLComputePipelineState> ps[2] = {optwin_pso(x, v->kernel, c->ntx, c->nty, 0),
                                       v->alt ? optwin_pso(x, v->kernel, c->ntx, c->nty, 32) : nil};
  const MTLSize tpg = MTLSizeMake(c->ntx, c->nty, 1);
  id<MTLCommandBuffer> cb = [x->q commandBuffer];
  id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
  [e setBuffer:out offset:0 atIndex:0]; [e setBytes:&p length:sizeof p atIndex:1];
  if (res & OPTWIN_RES_SRC) [e setBuffer:x->data offset:optwin_region_offset(v->region) atIndex:2];
  if (res & OPTWIN_RES_ROWS) [e setBuffer:x->rows offset:0 atIndex:3];
  if (res & OPTWIN_RES_SHADOW) { [e setTexture:x->shadow atIndex:0]; [e setSamplerState:x->cmp atIndex:0]; }
  if (res & OPTWIN_RES_COLOR) { [e setTexture:x->color atIndex:1]; [e setSamplerState:x->lin atIndex:1]; }
  if (v->indirect) {  // the GPU writes the arguments, then every dispatch of the buffer is indirect
    [e setComputePipelineState:optwin_pso(x, OPTWIN_K_ARGS, OPTWIN_ARGS_THREADS, 1, 0)];
    [e setBuffer:x->args offset:0 atIndex:4];
    [e dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(OPTWIN_ARGS_THREADS, 1, 1)];
    [e memoryBarrierWithScope:MTLBarrierScopeBuffers]; }
  [e setComputePipelineState:ps[0]];
  for (unsigned d = 0; d < n; ++d) {
    if (d) [e memoryBarrierWithScope:MTLBarrierScopeBuffers];
    if (d && v->alt) [e setComputePipelineState:ps[d & 1u]];
    if (v->indirect) [e dispatchThreadgroupsWithIndirectBuffer:x->args indirectBufferOffset:0 threadsPerThreadgroup:tpg];
    else [e dispatchThreadgroups:MTLSizeMake(c->gx, c->gy, 1) threadsPerThreadgroup:tpg]; }
  [e endEncoding];
  return cb;
}
static id<MTLCommandBuffer> optwin_filler(optwin_ctx *x) {
  const optwin_params p = optwin_filler_params();
  id<MTLCommandBuffer> cb = [x->q commandBuffer];
  id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
  [e setComputePipelineState:optwin_pso(x, OPTWIN_K_S1, 1024, 1, 0)];
  [e setBuffer:x->out offset:0 atIndex:0];  // same buffer as the timed one: Metal orders them (CLKTWIN)
  [e setBytes:&p length:sizeof p atIndex:1];
  [e dispatchThreadgroups:MTLSizeMake(OPTWIN_FILL_GROUPS, 1, 1) threadsPerThreadgroup:MTLSizeMake(1024, 1, 1)];
  [e endEncoding];
  return cb;
}
static void optwin_oracle(optwin_ctx *x, const optwin_variant *v) {
  optwin_case cases[OPTWIN_MAX_CASES];
  (void)optwin_cases(v, cases);
  const optwin_case *c = v->alt ? &cases[0] : &cases[1];
  static const unsigned lanes_l1[4] = {0, 1, 511, 1023}, lanes_n32[4] = {0, 1, 17, 31};
  const unsigned *lanes = v->alt ? lanes_n32 : lanes_l1;
  unsigned match = 0;
  id<MTLCommandBuffer> cb = optwin_encode(x, v, c, 1, x->out);
  [cb commit]; [cb waitUntilCompleted];
  for (unsigned i = 0; i < 4; ++i) {
    char form[160];
    const unsigned gv = ((const unsigned *)x->out.contents)[lanes[i]];
    const int ok = optwin_oracle_lane(v, c, lanes[i], gv, x->cpu_data, form, sizeof form);
    match += ok ? 1u : 0u;
    printf("OPTWIN oracle variant %s shape %s lane %u gpu 0x%08x %s %s\n", v->variant, c->shape, lanes[i], gv,
           ok ? "match" : "MISMATCH", form); }
  printf("OPTWIN_ORACLE variant=%s shape=%s match=%u/4 hr=0x%08lx\n", v->variant, c->shape, match, (unsigned long)cb.status);
  if (match != 4) ++x->oracle_bad;
}

static int optwin_selected(const optwin_variant *v, int argc, char **argv) {
  int any = 0;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--archive") || !strcmp(argv[i], "--archive-each")) { ++i; continue; }
    if (argv[i][0] == '-') continue;
    any = 1;
    if (!strcmp(argv[i], v->probe)) return 1; }
  return !any;
}

int main(int argc, char **argv) { @autoreleasepool {
  optwin_ctx ctx = {0}, *x = &ctx;
  const char *archive = NULL, *archive_each = NULL;
  int compile_only = 0;
  unsigned need = 0, variants = 0;
  NSDate *start = [NSDate date];
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--archive") && i + 1 < argc) archive = argv[++i];
    else if (!strcmp(argv[i], "--archive-each") && i + 1 < argc) archive_each = argv[++i];
    else if (!strcmp(argv[i], "--compile-only")) compile_only = 1; }
  x->dev = MTLCreateSystemDefaultDevice(); x->q = [x->dev newCommandQueue];
  x->opt = [MTLCompileOptions new];
  if (@available(macOS 15.0, *)) x->opt.mathMode = MTLMathModeSafe; else x->opt.fastMathEnabled = NO;
  x->pso = [NSMutableDictionary new]; x->descs = [NSMutableArray new]; x->labels = [NSMutableArray new];
  for (unsigned i = 0; i < OPTWIN_VARIANTS; ++i)
    if (optwin_selected(&optwin_variants[i], argc, argv)) { need |= optwin_kernels[optwin_variants[i].kernel].res; ++variants; }
  // every pipeline before the first timed buffer
  BOOL ok = optwin_pso(x, OPTWIN_K_S1, 1024, 1, 0) != nil;
  for (unsigned i = 0; i < OPTWIN_VARIANTS && ok; ++i) {
    const optwin_variant *v = &optwin_variants[i];
    optwin_case cases[OPTWIN_MAX_CASES];
    const unsigned n = optwin_cases(v, cases);
    if (!optwin_selected(v, argc, argv)) continue;
    for (unsigned k = 0; k < n && ok; ++k) {
      ok = optwin_pso(x, v->kernel, cases[k].ntx, cases[k].nty, 0) != nil;
      if (ok && v->alt) ok = optwin_pso(x, v->kernel, cases[k].ntx, cases[k].nty, 32) != nil; }
    if (ok && v->indirect) ok = optwin_pso(x, OPTWIN_K_ARGS, OPTWIN_ARGS_THREADS, 1, 0) != nil; }
  printf("OPTWIN setup device %s variants %u psos %lu resources 0x%x %s\n", x->dev.name.UTF8String, variants,
         (unsigned long)x->pso.count, need, ok ? "ok" : "FAIL");
  if (!ok) return 1;
  if (archive) {  // code-equality gate: binary archive of every pipeline
    NSError *e = nil;
    id<MTLBinaryArchive> ar = [x->dev newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:&e];
    BOOL wrote = ar != nil;
    for (MTLComputePipelineDescriptor *pd in x->descs) wrote = wrote && [ar addComputePipelineFunctionsWithDescriptor:pd error:&e];
    wrote = wrote && [ar serializeToURL:[NSURL fileURLWithPath:@(archive)] error:&e];
    printf("OPTWIN_ARCHIVE %s %s pipelines %lu\n", archive, wrote ? "written" : e.localizedDescription.UTF8String,
           (unsigned long)x->descs.count); }
  if (archive_each) {  // one pipeline per archive: the Mach-O slice of each file is that kernel's code
    unsigned written = 0;
    [[NSFileManager defaultManager] createDirectoryAtPath:@(archive_each) withIntermediateDirectories:YES attributes:nil error:nil];
    for (NSUInteger i = 0; i < x->descs.count; ++i) {
      NSError *e = nil;
      id<MTLBinaryArchive> ar = [x->dev newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:&e];
      NSString *path = [[@(archive_each) stringByAppendingPathComponent:x->labels[i]] stringByAppendingPathExtension:@"metallib"];
      if (ar && [ar addComputePipelineFunctionsWithDescriptor:x->descs[i] error:&e] &&
          [ar serializeToURL:[NSURL fileURLWithPath:path] error:&e]) ++written;
      else printf("OPTWIN_ARCHIVE_EACH %s failed %s\n", path.UTF8String, e.localizedDescription.UTF8String); }
    printf("OPTWIN_ARCHIVE_EACH %s written %u/%lu\n", archive_each, written, (unsigned long)x->descs.count); }
  if (compile_only) return 0;
  x->out = [x->dev newBufferWithLength:OPTWIN_OUT_BYTES options:MTLResourceStorageModeShared];
  x->args = [x->dev newBufferWithLength:256 options:MTLResourceStorageModePrivate];
  x->staging = [x->dev newBufferWithLength:(NSUInteger)OPTWIN_LAYERS * OPTWIN_TEX_LAYER_BYTES options:MTLResourceStorageModeShared];
  if (need & OPTWIN_RES_SRC) {
    x->cpu_data = (float *)malloc(OPTWIN_DATA_BYTES);
    if (!x->cpu_data || !optwin_fill_data(x->cpu_data)) { printf("OPTWIN data allocation failed\n"); return 1; }
    x->data = optwin_private_buffer(x, x->cpu_data, OPTWIN_DATA_BYTES);
    printf("OPTWIN data %u bytes\n", OPTWIN_DATA_BYTES); }
  if (need & OPTWIN_RES_ROWS) {
    static float rows[OPTWIN_ROWS * 4u];
    for (unsigned k = 0; k < OPTWIN_ROWS; ++k) for (unsigned c = 0; c < 4u; ++c) rows[4u * k + c] = optwin_row(k, c);
    x->rows = optwin_private_buffer(x, rows, sizeof rows); }
  if (need & (OPTWIN_RES_SHADOW | OPTWIN_RES_COLOR)) {
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear; sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = sd.tAddressMode = sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
    x->lin = [x->dev newSamplerStateWithDescriptor:sd];
    sd.compareFunction = MTLCompareFunctionLessEqual;
    x->cmp = [x->dev newSamplerStateWithDescriptor:sd];
    x->shadow = optwin_texture(x, 0); x->color = optwin_texture(x, 1);
    printf("OPTWIN_T1_DEPTH_FORM depth32float\n"); }
  for (unsigned i = 0; i < OPTWIN_VARIANTS; ++i)
    if (optwin_selected(&optwin_variants[i], argc, argv)) optwin_oracle(x, &optwin_variants[i]);
  id<MTLCommandBuffer> last = nil;
  { NSDate *t0 = [NSDate date]; unsigned lists = 0;  // 2 s warm-up, two fillers in flight
    id<MTLCommandBuffer> prev = nil;
    while ([[NSDate date] timeIntervalSinceDate:t0] < 2.0) {
      id<MTLCommandBuffer> cb = optwin_filler(x); [cb commit];
      if (prev) [prev waitUntilCompleted];
      prev = cb; ++lists; }
    last = prev; printf("OPTWIN warm-up lists %u\n", lists); }
  for (unsigned i = 0; i < OPTWIN_VARIANTS; ++i) {
    const optwin_variant *v = &optwin_variants[i];
    optwin_case cases[OPTWIN_MAX_CASES];
    const unsigned n = optwin_cases(v, cases);
    double best[OPTWIN_MAX_CASES][2], wall[OPTWIN_MAX_CASES][2], busy = 0.0;
    if (!optwin_selected(v, argc, argv)) continue;
    NSDate *block = [NSDate date];
    for (unsigned m = 0; m < n; ++m) best[m][0] = best[m][1] = wall[m][0] = wall[m][1] = 1e30;
    for (unsigned rep = 0; rep < OPTWIN_REPS; ++rep)
      for (unsigned m = 0; m < n; ++m)
        for (unsigned k = 0; k < 2u; ++k) {
          const unsigned dispatches = k ? cases[m].nb : cases[m].na;
          id<MTLCommandBuffer> cb = optwin_encode(x, v, &cases[m], dispatches, x->out), fill = optwin_filler(x);
          NSDate *t0 = [NSDate date];
          [cb commit]; [fill commit]; [cb waitUntilCompleted];
          const double wms = [[NSDate date] timeIntervalSinceDate:t0] * 1e3, ms = (cb.GPUEndTime - cb.GPUStartTime) * 1e3;
          const int got = cb.status == MTLCommandBufferStatusCompleted && ms > 0.0;
          if (got && ms < best[m][k]) best[m][k] = ms;
          if (got) busy += ms;
          if (wms < wall[m][k]) wall[m][k] = wms;
          last = fill;
          optwin_print_rep(rep, v, &cases[m], dispatches, got, ms, -1.0, wms, 1u); }
    optwin_print_p6(v, -1.0, 0.0, 0u, -1, busy, [[NSDate date] timeIntervalSinceDate:block] * 1e3);
    for (unsigned m = 0; m < n; ++m) optwin_print_case(v, &cases[m], best[m], wall[m], -1.0); }
  [last waitUntilCompleted];
  printf("OPTWIN_RECORDS missing 0 oracle_bad %u\n", x->oracle_bad);
  printf("OPTWIN_DONE seconds %.1f\n", [[NSDate date] timeIntervalSinceDate:start]);
  free(x->cpu_data);
  return 0; } }
