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
 * source and a new SetDisplayMode from DWM (a mode-change loop), so the
 * retained object is superseded instead (the current slot is freed and the
 * new mode-setting primary is retained at its destroy).  PL-1c (EXP2403):
 * releasing the oldest superseded one beyond a bound of 4 restarted the same
 * loop, so the superseded set is unbounded and SetDisplayMode never returns
 * an object to release.  Device destroy hands back the current object and
 * every superseded one.
 *
 * Neutral types only, so a host test compiles it (tests/
 * test_umd_primary_retention.py); the retained object is opaque except that
 * its first field is the pointer-sized chain link (ADMISSION_UMD_RETENTION_LINK). */

/* PL-1c (EXP2403): any live release of a retained primary - also the
 * oldest superseded one, four mode sets back - was followed by a hide of the
 * source and a new SetDisplayMode (the loop returned at the first bound
 * release).  The superseded set is therefore unbounded and nothing is
 * released before device destroy.  Superseded objects are chained through
 * their FIRST pointer-sized field (ADMISSION_UMD_RETIREMENT::Next, checked
 * at compile time in umd.c), oldest first; no allocation is needed. */
#define ADMISSION_UMD_RETENTION_LINK(Object) (*(void **)(Object))

typedef struct _ADMISSION_UMD_PRIMARY_RETENTION {
  unsigned int ModeAllocation;     /* last successful SetDisplayMode, 0 none */
  void *Retained;                  /* current slot: retained retirement */
  unsigned int RetainedAllocation; /* its kernel allocation */
  unsigned int SupersededCount;    /* entries in the chain below */
  void *SupersededHead;            /* oldest superseded, chained by LINK */
  void *SupersededTail;            /* newest superseded */
} ADMISSION_UMD_PRIMARY_RETENTION;

/* After pfnSetDisplayModeCb returned.  A retained object of another
 * allocation is superseded (appended to the chain), never released: PL-1c
 * returns NULL always.  The caller sees a supersede as Retained going from
 * non-NULL to NULL across the call. */
static inline void *AdmissionUmdRetentionSetDisplayMode(
    ADMISSION_UMD_PRIMARY_RETENTION *State, unsigned int Allocation,
    int Succeeded) {
  if (State == 0 || !Succeeded || Allocation == 0u ||
      Allocation == State->ModeAllocation)
    return 0;
  State->ModeAllocation = Allocation;
  if (State->Retained == 0 || State->RetainedAllocation == Allocation)
    return 0;
  ADMISSION_UMD_RETENTION_LINK(State->Retained) = 0;
  if (State->SupersededTail != 0)
    ADMISSION_UMD_RETENTION_LINK(State->SupersededTail) = State->Retained;
  else
    State->SupersededHead = State->Retained;
  State->SupersededTail = State->Retained;
  ++State->SupersededCount;
  State->Retained = 0;
  State->RetainedAllocation = 0u;
  return 0;
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
  if (State == 0)
    return 0;
  State->ModeAllocation = 0u;
  if (State->Retained != 0) {
    release = State->Retained;
    State->Retained = 0;
    State->RetainedAllocation = 0u;
    return release;
  }
  release = State->SupersededHead;
  if (release == 0)
    return 0;
  State->SupersededHead = ADMISSION_UMD_RETENTION_LINK(release);
  ADMISSION_UMD_RETENTION_LINK(release) = 0;
  if (State->SupersededHead == 0)
    State->SupersededTail = 0;
  --State->SupersededCount;
  return release;
}

#endif /* APPLE_AGX_UMD_PRIMARY_RETENTION_H */
