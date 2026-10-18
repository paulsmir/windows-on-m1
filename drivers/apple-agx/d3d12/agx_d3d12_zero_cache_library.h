/* Original ZERO_NATIVE_CACHE foundation. SPDX-License-Identifier: MIT
 * Include the actual Windows d3d12umddi.h before this header.
 * Unregistered and unlinked: no CORE table or capability change.
 */
#ifndef AGX_D3D12_ZERO_CACHE_LIBRARY_H
#define AGX_D3D12_ZERO_CACHE_LIBRARY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Integration hooks, deliberately separate from any private PSO layout.
 * Return a stable, nonnull live device identity, or NULL for an unknown/dead
 * handle. ResolvePsoOwner must check live PSO ownership without requiring a
 * compiled native object. Neither hook transfers/retains an object reference.
 * Identity must remain valid during callbacks and while its libraries live.
 * The module calls hooks outside its registry lock. An eventual DDI adapter
 * must supply its own real device/PSO ownership registry and lifetime policy.
 */
const void *AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE device);
const void *AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE pipeline);

/* Internal CPU adapter helper, NOT a new WDK callback or capability.
 * reference==NULL is E_INVALIDARG (not an omitted/ordinary reference).
 * A readable record with hLibrary.pDrvPrivate==NULL is the ordinary no-library
 * path and succeeds only for a live valid device. PipelineIndex is unused.
 * A nonnull library must be live and owned by that same device. Unknown library
 * addresses are compared with registered addresses before any dereference.
 * Nonnull reference must point to a readable actual WDK record. No retention,
 * native work or lifetime extension is performed; existing runtime object
 * lifetime and resolver identity stability preconditions still apply.
 */
HRESULT APIENTRY AgxD3d12ZeroCacheValidateLibraryReference(
    D3D12DDI_HDEVICE, const D3D12DDI_LIBRARY_REFERENCE_0010 *reference);

SIZE_T APIENTRY AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(
    D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 *);
/* Create requires runtime-owned, writable, suitably aligned private storage of
 * at least the returned CalcPrivate size. Unknown-handle checks on live-object
 * operations do not validate arbitrary Create destination memory. */
HRESULT APIENTRY AgxD3d12ZeroCacheCreatePipelineLibrary(
    D3D12DDI_HDEVICE, const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 *,
    D3D12DDI_HPIPELINELIBRARY);
VOID APIENTRY AgxD3d12ZeroCacheDestroyPipelineLibrary(
    D3D12DDI_HDEVICE, D3D12DDI_HPIPELINELIBRARY);
HRESULT APIENTRY AgxD3d12ZeroCacheAddPipelineStateToLibrary(
    D3D12DDI_HDEVICE, D3D12DDI_HPIPELINELIBRARY,
    D3D12DDI_HPIPELINESTATE, UINT);
SIZE_T APIENTRY AgxD3d12ZeroCacheCalcSerializedLibrarySize(
    D3D12DDI_HDEVICE, D3D12DDI_HPIPELINELIBRARY);
HRESULT APIENTRY AgxD3d12ZeroCacheSerializeLibrary(
    D3D12DDI_HDEVICE, D3D12DDI_HPIPELINELIBRARY, VOID *);

#ifdef __cplusplus
}
#endif
#endif
