#ifndef RENDER_OV2_H
#define RENDER_OV2_H
/* OV2 (investigation/analysis/OV1-render-compute-overlap.md; REG_DWORD
 * Wom1Ov2NoHazardLead = 1 on Q2c, default off): the KMD files include this
 * header to wire the no-hazard lead Barrier; host tests that extract single
 * function bodies do not, so those bodies compile without the OV2 runtime
 * fields (the pure decision is tested in render_job_window.c). */
#define ADMISSION_OV2_ON 1
/* apple_agx_g4_submit.h defines the 0u fallback when included first (C4005) */
#ifdef ADMISSION_G4_COMPUTE_PARSE_FLAGS
#undef ADMISSION_G4_COMPUTE_PARSE_FLAGS
#endif
#define ADMISSION_G4_COMPUTE_PARSE_FLAGS(adapter) AdmissionG4ComputeParseFlags(adapter)
#endif
