#ifndef RENDER_OV5_H
#define RENDER_OV5_H
/* OV5 drill (investigation/analysis/OV1-render-compute-overlap.md, "OV5 KMD
 * drill"; REG_DWORD Wom1Ov5FragWaitDrill = 1, default off): the KMD files
 * include this header to wire the 3D-queue compute-stamp Barrier; host tests
 * that extract single function bodies do not, so those bodies compile
 * without the OV5 runtime fields (the pure decision is tested in
 * render_job_window.c). */
#define ADMISSION_OV5_ON 1
/* OV5 step 2 (REG_DWORD Wom1Ov5TaWaiver = 1): the render parser admits
 * CdmBarrier NONE only while the device reports
 * AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT. apple_agx_g4_submit.h
 * defines the 0u fallback when included first (C4005). */
#ifdef ADMISSION_G4_RENDER_PARSE_FLAGS
#undef ADMISSION_G4_RENDER_PARSE_FLAGS
#endif
#define ADMISSION_G4_RENDER_PARSE_FLAGS(adapter) AdmissionG4RenderParseFlags(adapter)
#endif
