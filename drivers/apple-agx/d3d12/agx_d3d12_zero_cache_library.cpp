/* Original ZERO_NATIVE_CACHE foundation. SPDX-License-Identifier: MIT */
#include <windows.h>
#ifdef _WIN32
/* Same actual SDK prerequisite as the existing user-mode D3D12 translation TU. */
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#endif
#include <d3d12umddi.h>
#ifdef _WIN32
#pragma warning(pop)
#endif
#include <cstring>
#include <new>
#include <type_traits>
#include "agx_d3d12_zero_cache_library.h"

namespace {
constexpr unsigned long long kLibraryMagic = 0x4147585a434c4942ull;
struct Library {
    unsigned long long magic;
    const void *owner;
    Library *next;
};

/* Runtime supplies each private allocation. Intrusive registration needs no
 * allocator and lets every live-handle operation compare an unknown address
 * before dereferencing it. One process registry also rejects duplicate storage
 * and cross-device Create without touching the foreign private header.
 */
SRWLOCK registry_lock = SRWLOCK_INIT;
Library *libraries = nullptr;

class RegistryGuard {
public:
    RegistryGuard() { AcquireSRWLockExclusive(&registry_lock); }
    ~RegistryGuard() { ReleaseSRWLockExclusive(&registry_lock); }
    RegistryGuard(const RegistryGuard &) = delete;
    RegistryGuard &operator=(const RegistryGuard &) = delete;
};

Library **Find(void *address) {
    Library **entry = &libraries;
    while (*entry && static_cast<void *>(*entry) != address)
        entry = &(*entry)->next;
    return entry;
}
bool Owned(const Library *library, const void *owner) {
    return library && owner && library->magic == kLibraryMagic &&
           library->owner == owner;
}
}

HRESULT APIENTRY AgxD3d12ZeroCacheValidateLibraryReference(
    D3D12DDI_HDEVICE device, const D3D12DDI_LIBRARY_REFERENCE_0010 *reference) {
    const void *owner = AgxD3d12ZeroCacheResolveDevice(device);
    if (!owner || !reference) return E_INVALIDARG;
    if (!reference->hLibrary.pDrvPrivate) return S_OK;
    RegistryGuard guard;
    return Owned(*Find(reference->hLibrary.pDrvPrivate), owner) ? S_OK : E_INVALIDARG;
}

SIZE_T APIENTRY AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(
    D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 *args) {
    /* EXP1864: a CachedPSO creation sizes the runtime's temporary library
     * with the whole cached blob (InitialDataSize 0xe4e) and creates it with
     * InitialDataSize 0 in storage of this size; 0 here let Create write the
     * Library past it (D3D12 heap_failure_invalid_argument). The storage is
     * one Library whatever the initial data, which stays unread. */
    return AgxD3d12ZeroCacheResolveDevice(device) && args ? sizeof(Library) : 0;
}

HRESULT APIENTRY AgxD3d12ZeroCacheCreatePipelineLibrary(
    D3D12DDI_HDEVICE device,
    const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 *args,
    D3D12DDI_HPIPELINELIBRARY handle) {
    const void *owner = AgxD3d12ZeroCacheResolveDevice(device);
    if (!owner || !args || args->InitialDataSize || !handle.pDrvPrivate)
        return E_INVALIDARG;
    RegistryGuard guard;
    if (*Find(handle.pDrvPrivate)) return E_INVALIDARG;
    /* Create's destination is the runtime's writable CalcPrivate-sized storage,
     * as required by the DDI. Do not inspect its uninitialized contents.
     * In particular, zero-sized initial data is NEVER read, even when nonnull.
     */
    auto *library = ::new (handle.pDrvPrivate) Library{kLibraryMagic, owner, libraries};
    libraries = library;
    return S_OK;
}

VOID APIENTRY AgxD3d12ZeroCacheDestroyPipelineLibrary(
    D3D12DDI_HDEVICE device, D3D12DDI_HPIPELINELIBRARY handle) {
    const void *owner = AgxD3d12ZeroCacheResolveDevice(device);
    if (!owner) return;
    RegistryGuard guard;
    Library **entry = Find(handle.pDrvPrivate);
    if (!Owned(*entry, owner)) return;
    Library *library = *entry;
    *entry = library->next;
    std::memset(library, 0, sizeof(*library));
}

HRESULT APIENTRY AgxD3d12ZeroCacheAddPipelineStateToLibrary(
    D3D12DDI_HDEVICE device, D3D12DDI_HPIPELINELIBRARY handle,
    D3D12DDI_HPIPELINESTATE pipeline, UINT index) {
    (void)index; /* Opaque unused native-cache key, including UINT_MAX. */
    const void *owner = AgxD3d12ZeroCacheResolveDevice(device);
    if (!owner || AgxD3d12ZeroCacheResolvePsoOwner(pipeline) != owner)
        return E_INVALIDARG;
    RegistryGuard guard;
    return Owned(*Find(handle.pDrvPrivate), owner) ? S_OK : E_INVALIDARG;
}

SIZE_T APIENTRY AgxD3d12ZeroCacheCalcSerializedLibrarySize(
    D3D12DDI_HDEVICE device, D3D12DDI_HPIPELINELIBRARY handle) {
    const void *owner = AgxD3d12ZeroCacheResolveDevice(device);
    if (!owner) return 0;
    RegistryGuard guard;
    if (!Owned(*Find(handle.pDrvPrivate), owner)) return 0;
    return 0; /* No opaque driver bytes; runtime framing is separate. */
}

HRESULT APIENTRY AgxD3d12ZeroCacheSerializeLibrary(
    D3D12DDI_HDEVICE device, D3D12DDI_HPIPELINELIBRARY handle, VOID *output) {
    (void)output; /* Size is always zero: no pointer is dereferenced/written. */
    const void *owner = AgxD3d12ZeroCacheResolveDevice(device);
    if (!owner) return E_INVALIDARG;
    RegistryGuard guard;
    return Owned(*Find(handle.pDrvPrivate), owner) ? S_OK : E_INVALIDARG;
}

/* These use actual WDK callback typedefs in a Windows compilation. The host
 * fixture extracts those same declarations but does NOT prove Windows ABI.
 */
static_assert(std::is_same<decltype(&AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize),
    PFND3D12DDI_CALC_PRIVATE_PIPELINE_LIBRARY_SIZE_0010>::value, "private size callback");
static_assert(std::is_same<decltype(&AgxD3d12ZeroCacheCreatePipelineLibrary),
    PFND3D12DDI_CREATE_PIPELINE_LIBRARY_0010>::value, "create callback");
static_assert(std::is_same<decltype(&AgxD3d12ZeroCacheDestroyPipelineLibrary),
    PFND3D12DDI_DESTROY_PIPELINE_LIBRARY_0010>::value, "destroy callback");
static_assert(std::is_same<decltype(&AgxD3d12ZeroCacheAddPipelineStateToLibrary),
    PFND3D12DDI_ADD_PIPELINE_STATE_TO_LIBRARY_0010>::value, "add callback");
static_assert(std::is_same<decltype(&AgxD3d12ZeroCacheCalcSerializedLibrarySize),
    PFND3D12DDI_CALC_SERIALIZED_LIBRARY_SIZE_0010>::value, "serialized size callback");
static_assert(std::is_same<decltype(&AgxD3d12ZeroCacheSerializeLibrary),
    PFND3D12DDI_SERIALIZE_LIBRARY_0010>::value, "serialize callback");
