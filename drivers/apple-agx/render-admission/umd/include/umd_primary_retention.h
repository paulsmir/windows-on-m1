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
 * device-owned when its resource is destroyed.  Only device-associated
 * allocations (created with D3DDDICB_ALLOCATE.hResource NULL and released by
 * HandleList, so they never touch the runtime's resource object after
 * DestroyResource: EXP975) can be retained.  A failed SetDisplayMode, or one
 * naming allocation 0 or the current mode allocation again, changes nothing.
 *
 * PL-1b (EXP2401/2402): releasing the retained primary right after a later
 * SetDisplayMode(new) returned was followed, every time, by a hide of the
 * source and a new SetDisplayMode from DWM (a mode-change loop).  The
 * retained object is therefore not released there: it moves to a bounded
 * "superseded" list (oldest first) and the current slot is freed, so the new
 * mode-setting primary is retained at its destroy.  Only when the list is
 * full is its OLDEST entry (at least ADMISSION_UMD_RETENTION_SUPERSEDED_MAX
 * mode sets old) handed back for release.  Device destroy hands back the
 * current object and every superseded one.
 *
 * Neutral types only, so a host test compiles it (tests/
 * test_umd_primary_retention.py); the retained object is opaque. */

#define ADMISSION_UMD_RETENTION_SUPERSEDED_MAX 4u

typedef struct _ADMISSION_UMD_PRIMARY_RETENTION {
  unsigned int ModeAllocation;     /* last successful SetDisplayMode, 0 none */
  void *Retained;                  /* current slot: retained retirement */
  unsigned int RetainedAllocation; /* its kernel allocation */
  unsigned int SupersededCount;    /* valid entries below, oldest first */
  void *Superseded[ADMISSION_UMD_RETENTION_SUPERSEDED_MAX];
  unsigned int SupersededAllocation[ADMISSION_UMD_RETENTION_SUPERSEDED_MAX];
} ADMISSION_UMD_PRIMARY_RETENTION;

/* After pfnSetDisplayModeCb returned.  A retained object of another
 * allocation is superseded, not released.  Returns the object the caller
 * must release now (the oldest superseded one, only when the list was
 * full), or NULL.  The caller sees a supersede as Retained going from
 * non-NULL to NULL across the call. */
static inline void *AdmissionUmdRetentionSetDisplayMode(
    ADMISSION_UMD_PRIMARY_RETENTION *State, unsigned int Allocation,
    int Succeeded) {
  void *release = 0;
  unsigned int i;
  if (State == 0 || !Succeeded || Allocation == 0u ||
      Allocation == State->ModeAllocation)
    return 0;
  State->ModeAllocation = Allocation;
  if (State->Retained == 0 || State->RetainedAllocation == Allocation)
    return 0;
  if (State->SupersededCount >= ADMISSION_UMD_RETENTION_SUPERSEDED_MAX) {
    release = State->Superseded[0];
    for (i = 1u; i < ADMISSION_UMD_RETENTION_SUPERSEDED_MAX; ++i) {
      State->Superseded[i - 1u] = State->Superseded[i];
      State->SupersededAllocation[i - 1u] = State->SupersededAllocation[i];
    }
    State->SupersededCount = ADMISSION_UMD_RETENTION_SUPERSEDED_MAX - 1u;
  }
  State->Superseded[State->SupersededCount] = State->Retained;
  State->SupersededAllocation[State->SupersededCount] =
      State->RetainedAllocation;
  ++State->SupersededCount;
  State->Retained = 0;
  State->RetainedAllocation = 0u;
  return release;
}

/* At the destroy of a resource.  Returns 1 when the caller must NOT
 * deallocate: the object is now retained as device-owned in the current
 * slot.  Keyed on the current slot only; superseded entries do not block
 * it. */
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

/* At device destroy: one held object per call (the current slot first,
 * then the superseded ones oldest first) or NULL when none is left; the
 * caller loops until NULL.  The mode allocation is cleared on every call. */
static inline void *AdmissionUmdRetentionDeviceDestroy(
    ADMISSION_UMD_PRIMARY_RETENTION *State) {
  void *release;
  unsigned int i;
  if (State == 0)
    return 0;
  State->ModeAllocation = 0u;
  if (State->Retained != 0) {
    release = State->Retained;
    State->Retained = 0;
    State->RetainedAllocation = 0u;
    return release;
  }
  if (State->SupersededCount == 0u)
    return 0;
  release = State->Superseded[0];
  for (i = 1u; i < State->SupersededCount; ++i) {
    State->Superseded[i - 1u] = State->Superseded[i];
    State->SupersededAllocation[i - 1u] = State->SupersededAllocation[i];
  }
  --State->SupersededCount;
  State->Superseded[State->SupersededCount] = 0;
  State->SupersededAllocation[State->SupersededCount] = 0u;
  return release;
}

#endif /* APPLE_AGX_UMD_PRIMARY_RETENTION_H */
