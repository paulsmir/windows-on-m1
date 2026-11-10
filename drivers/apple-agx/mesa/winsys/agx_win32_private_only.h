#ifndef AGX_WIN32_PRIVATE_ONLY_H
#define AGX_WIN32_PRIVATE_ONLY_H
/* C2a (investigation/analysis/DWM-COMPOSITION-LEVER.md): the private-only
 * verdict of one compute job. A job is private-only when every allocation it
 * references is a BO its own device allocated: the winsys allocates those
 * with no runtime resource (pfnAllocateCb, hResource NULL), so no kernel
 * resource handle exists and no other process can open them (Microsoft
 * D3DDDICB_ALLOCATE: shared resources need a non-NULL hResource). Everything
 * that came with a runtime resource (presentation textures, the display
 * primary) or through pfnOpenHeapAndResource is an imported BO and clears
 * the verdict, as does a BO of unknown origin and a job with no reference
 * at all. The flag is sent only while the KMD reports the device feature. */
/* apple_agx_g4_submit.h; repeated so this header stands alone */
#ifndef APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY
#define APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY 0x1u
#endif

typedef struct {
  unsigned Begun, Refs, Foreign;
} AGX_WIN32_PRIVATE_ONLY;

static inline void AgxWin32PrivateOnlyBegin(AGX_WIN32_PRIVATE_ONLY *v) {
  if (!v) return;
  v->Begun = 1u; v->Refs = 0u; v->Foreign = 0u;
}
/* One referenced allocation; Private: nonzero only when its origin is known
 * to be this device's own allocation path. */
static inline void AgxWin32PrivateOnlyAdd(AGX_WIN32_PRIVATE_ONLY *v, int Private) {
  if (!v) return;
  ++v->Refs;
  if (!Private) ++v->Foreign;
}
/* The header value: APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY or 0. */
static inline unsigned AgxWin32PrivateOnlyFlag(const AGX_WIN32_PRIVATE_ONLY *v,
                                               unsigned Feature) {
  return v && Feature && v->Begun && v->Refs != 0u && v->Foreign == 0u
      ? APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY : 0u;
}
#endif
