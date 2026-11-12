#ifndef APPLE_AGX_UMD_PRIMARY_RETENTION_H
#define APPLE_AGX_UMD_PRIMARY_RETENTION_H

/* PL-1 (EXP2360, architect REPORT-primary-lifetime.md): dxgkrnl records the
 * allocation of the last successful pfnSetDisplayModeCb as the device's
 * displayed primary for the source; destroying exactly that allocation runs
 * DisablePrimaryAllocation -> UnpinPrimaryAllocations and clears the
 * device's "primary pinned" / "mode set" state, so the next MPO present
 * fails with DXGI_ERROR_SETDISPLAYMODE_REQUIRED.  ddisplay (the normal DWM
 * output path) destroys its mode-setting primary right after SetDisplayMode
 * and scans out from its own shared buffers (EXP2360: 87 of 87 cycles).
 *
 * The UMD therefore keeps, per device (one source, VidPnSource 0), the
 * current mode-setting allocation, and retains that kernel allocation as
 * device-owned when its resource is destroyed.  A retained allocation is
 * released after a later successful SetDisplayMode of another allocation,
 * or at device destroy.  Only device-associated allocations (created with
 * D3DDDICB_ALLOCATE.hResource NULL and released by HandleList, so they never
 * touch the runtime's resource object after DestroyResource: EXP975) can be
 * retained.  A failed SetDisplayMode changes nothing.
 *
 * Neutral types only, so a host test compiles it (tests/
 * test_umd_primary_retention.py); the retained object is opaque. */

typedef struct _ADMISSION_UMD_PRIMARY_RETENTION {
  unsigned int ModeAllocation;     /* last successful SetDisplayMode, 0 none */
  void *Retained;                  /* retained retirement, NULL none */
  unsigned int RetainedAllocation; /* its kernel allocation */
} ADMISSION_UMD_PRIMARY_RETENTION;

/* After pfnSetDisplayModeCb returned.  Returns the retained object the
 * caller must release now (it is no longer the displayed primary), or NULL. */
static inline void *AdmissionUmdRetentionSetDisplayMode(
    ADMISSION_UMD_PRIMARY_RETENTION *State, unsigned int Allocation,
    int Succeeded) {
  void *release = 0;
  if (State == 0 || !Succeeded || Allocation == 0u)
    return 0;
  State->ModeAllocation = Allocation;
  if (State->Retained != 0 && State->RetainedAllocation != Allocation) {
    release = State->Retained;
    State->Retained = 0;
    State->RetainedAllocation = 0u;
  }
  return release;
}

/* At the destroy of a resource.  Returns 1 when the caller must NOT
 * deallocate: the object is now retained as device-owned. */
static inline int AdmissionUmdRetentionDestroy(
    ADMISSION_UMD_PRIMARY_RETENTION *State, void *Object,
    unsigned int Allocation, int DeviceAssociated) {
  if (State == 0 || Object == 0 || !DeviceAssociated || Allocation == 0u ||
      Allocation != State->ModeAllocation || State->Retained != 0)
    return 0;
  State->Retained = Object;
  State->RetainedAllocation = Allocation;
  return 1;
}

/* At device destroy: the retained object to release (or NULL); the state is
 * cleared. */
static inline void *AdmissionUmdRetentionDeviceDestroy(
    ADMISSION_UMD_PRIMARY_RETENTION *State) {
  void *release;
  if (State == 0)
    return 0;
  release = State->Retained;
  State->Retained = 0;
  State->RetainedAllocation = 0u;
  State->ModeAllocation = 0u;
  return release;
}

#endif /* APPLE_AGX_UMD_PRIMARY_RETENTION_H */
