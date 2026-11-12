#ifndef APPLE_AGX_UMD_PRIMARY_BACKBUFFER_H
#define APPLE_AGX_UMD_PRIMARY_BACKBUFFER_H

/* BP-1 (investigation/analysis/BP-1-d3d12-primary-back-buffers.md): what a
 * presentation resource's creation puts into D3DDDI_ALLOCATIONINFO,
 * D3DDDICB_ALLOCATE and the driver's own resource data.
 *
 * Two runtime marks make the kernel allocation a primary
 * (D3DDDI_ALLOCATIONINFO.Flags.Primary):
 *  - D3D10/11: D3D11DDIARG_CREATERESOURCE.pPrimaryDesc != NULL;
 *  - D3D12: the heap of a committed resource carries
 *    D3D12DDI_HEAP_FLAG_PRIMARY (d3d12umddi.h:313).  The D3D12 owner forwards
 *    it as ADMISSION_UMD_CREATE_RUNTIME_PRIMARY; it is a UMD-local request and
 *    never written into the resource data the KMD validates.
 *
 * VidPnSourceId is 0: the D3D12 DDI gives no source at resource creation
 * (D3D12DDIARG_CREATEHEAP_0001 / D3D12DDIARG_CREATERESOURCE_* have none) and
 * the adapter reports exactly one source (render-admission/src/lifecycle.c,
 * *NumberOfVideoPresentSources = 1).  The D3D10/11 primary keeps the 0 it
 * always had.
 *
 * WRITTEN_PRIMARY (the driver's "scan-out primary" mark, which the KMD
 * admits only for a scan-out format: allocation_windows.c) is requested only
 * for a Displayable resource, i.e. A8R8G8B8 storage.  RGBA8 stays
 * non-displayable in user mode: whether the broker scans RGBA8888 out is a
 * kernel-side capability (AdmissionScanoutFormatSupported), and the KMD
 * derives ScanoutCapable for the allocation itself.
 *
 * Only a pPrimaryDesc primary is a mode-setting primary: it alone is
 * allocated device-associated (PL-1, umd_primary_retention.h) and recorded
 * as a primary retirement.  A D3D12 PRIMARY back buffer keeps the runtime
 * resource allocation and the release path it had before BP-1.
 *
 * Neutral types only, so a host test compiles it
 * (tests/test_umd_primary_backbuffer.py). */

/* AdmissionUmdCreateResourceWithMetadata CreateFlags (UMD-local). */
#define ADMISSION_UMD_CREATE_RUNTIME_PRIMARY 1u

/* D3DDDIFMT_A8R8G8B8 (d3dukmdt.h); umd.c asserts the equality. */
#define ADMISSION_UMD_STORAGE_A8R8G8B8 21u

typedef struct _ADMISSION_UMD_PRIMARY_MAPPING {
  unsigned int AllocationPrimary; /* D3DDDI_ALLOCATIONINFO.Flags.Primary */
  unsigned int VidPnSourceId;     /* D3DDDI_ALLOCATIONINFO.VidPnSourceId */
  unsigned int WrittenPrimary;    /* ADMISSION_PRESENT_RESOURCE_WRITTEN_PRIMARY */
  unsigned int DeviceAssociated;  /* D3DDDICB_ALLOCATE.hResource NULL (PL-1) */
  unsigned int RetirementPrimary; /* the retirement record's Primary */
} ADMISSION_UMD_PRIMARY_MAPPING;

/* Displayable of a presentation allocation, from its storage format.  Create
 * (AdmissionUmdDescribePrimary: BGRA8 formats -> A8R8G8B8 storage) and open
 * (AdmissionUmdOpenResource) give the same answer through this rule. */
static inline unsigned int AdmissionUmdStorageDisplayable(
    unsigned int StorageFormat) {
  return StorageFormat == ADMISSION_UMD_STORAGE_A8R8G8B8 ? 1u : 0u;
}

static inline ADMISSION_UMD_PRIMARY_MAPPING AdmissionUmdPrimaryMap(
    int HasPrimaryDesc, int Shared, int DisplayableMisc,
    unsigned int Displayable, unsigned int CreateFlags) {
  ADMISSION_UMD_PRIMARY_MAPPING mapping;
  const int runtimePrimary =
      (CreateFlags & ADMISSION_UMD_CREATE_RUNTIME_PRIMARY) != 0u;
  mapping.AllocationPrimary = HasPrimaryDesc || runtimePrimary ? 1u : 0u;
  mapping.VidPnSourceId = 0u;
  mapping.WrittenPrimary =
      Displayable != 0u && (HasPrimaryDesc || DisplayableMisc || runtimePrimary)
          ? 1u : 0u;
  mapping.DeviceAssociated = HasPrimaryDesc && !Shared ? 1u : 0u;
  mapping.RetirementPrimary = HasPrimaryDesc ? 1u : 0u;
  return mapping;
}

#endif /* APPLE_AGX_UMD_PRIMARY_BACKBUFFER_H */
