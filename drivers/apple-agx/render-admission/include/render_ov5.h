#ifndef RENDER_OV5_H
#define RENDER_OV5_H
/* OV5 drill (investigation/analysis/OV1-render-compute-overlap.md, "OV5 KMD
 * drill"; REG_DWORD Wom1Ov5FragWaitDrill = 1, default off): the KMD files
 * include this header to wire the 3D-queue compute-stamp Barrier; host tests
 * that extract single function bodies do not, so those bodies compile
 * without the OV5 runtime fields (the pure decision is tested in
 * render_job_window.c). */
#define ADMISSION_OV5_ON 1
#endif
