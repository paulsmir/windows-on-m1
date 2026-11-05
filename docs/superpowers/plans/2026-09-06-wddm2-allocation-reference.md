# WDDM2+ allocation-reference correction plan

**Goal:** close EXP498 version-gated runtime-handle resolution.
**Architecture:** WDDM3.0 and existing open/close object lifetime unchanged.
Use the supported acquire/release pair for the transient reference during Open.
**Spec:** EXP498 event494 code88 and current dxgkrnl GetHandleData version gate.
**Execution:** inline under existing authorization.

## Source/evidence and deterministic translation

EXP498 Code0/Start0/8CPU/SSH without crash; ETW shows WDDM2 driver calls WDDM1.x
DDI with88 thenINVALID_HANDLE. Current DxgGetHandleDataCB RVA130950 checks adapter
model at130a7c..a90, logs88 at130ab0..ae0 and returnsNULL130afc. The previous
Microsoft common-DDI example resolved token identity but omitted this version
restriction; it is superseded for this WDDM3.0 runtime, not a reason to change model.

Official DXGKCB_ACQUIREHANDLEDATA/RELEASEHANDLEDATA: Windows10 WDDM2.0+, <=APC_LEVEL;
Acquire returns KMD object and release handle; release takes structure by VALUE.
Pinned26100 d3dkmddi.h1392..1418 matches. Current AcquireCB RVA2d8600 accepts
queryType1 and produces a retained object reference, no legacy version gate.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_acquirehandledata
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_releasehandledata
Official CloseAllocation requires all bindings closed before DestroyAllocation:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_closeallocation

Reference is held through validation and creation of the device-specific binding,
then released before Open returns. Afterward the normal Windows Open/Close-before-
Destroy contract protects the binding, with existing KMD open-count guard. No
extra reference stored across DDIs, private handle table, circular ownership, or
new memory lifetime. On failure release current transient reference exactly once,
then rollback previously created bindings. NULL acquire means no object access.
Asahi/m1n1/Mu/AGX/HVC/scanout owners unchanged; no platform ABI change.

## Task

Files: allocation_windows.c; existing test_apple_agx_render_open_allocation.py.

- [x] RED actual production callback with legacyresolver returningNULL and valid
  modernacquire/release. Existing branch fails success assertion as hardware did.
- [ ] Require both modern callbacks. Initialize releaseHandleNULL, queryType1/
  Flags0; acquire. Failclosed if object/reference missing; validate sameobject.
  Release after binding fields/counts published. Every early failure releases
  active reference before existingrollback. Never call legacyGetHandleData.
- [ ] Test successful open/close, exactreference identity, one release per acquire,
  invalidhandle second element rollback, missing eithercallback, malformed
  description, and failed allocation cleanup. No outstandingrefs afterreturn.
- [ ] Run85+ relevant tests, diffreview and commit; append exactCHANGES row.
- [ ] Freeze498 plus changedfiles, pinnedFRYZZING499 build/sign/hash gates.
- [ ] Verifycleanordinary, stageexact499 and one naturalbind. Expected no88 gate,
  noINVALID_HANDLE fromresolver, nextactualcallback. Saveevidence thenexactcleanup
  andordinaryrestoration. No accelerationproof fromsuccessfulOpen alone.

Self-review: reference API pair is an indivisible lifetime invariant; it cannot
be tested as two unrelated callback changes. No changes outside the current
handle boundary. Knownrecovery377/392, emergency385 only when actuallyneeded.
