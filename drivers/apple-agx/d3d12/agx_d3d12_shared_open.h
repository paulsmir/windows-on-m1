#ifndef AGX_D3D12_SHARED_OPEN_H
#define AGX_D3D12_SHARED_OPEN_H
#include <string.h>
extern "C" {
#include "../render-admission/include/render_allocation.h"
}

/* C76: the existing D3D10/11 wire contract describes one linear, uncompressed
 * color texture. Never serialize process pointers, BOs, or GPU virtual addresses.
 * The consumer includes the WDK format enums before this header. */
struct AgxD3d12SharedDescription {
    unsigned Format, Width, Height, Pitch;
    unsigned long long Bytes;
};
static inline bool AgxD3d12DecodeShared(const void *resource_data, unsigned resource_bytes,
                                       const void *allocation_data, unsigned allocation_bytes,
                                       AgxD3d12SharedDescription *out) {
    if (!out || !resource_data || !allocation_data ||
        resource_bytes != sizeof(ADMISSION_PRESENT_RESOURCE_DATA) ||
        allocation_bytes != sizeof(ADMISSION_ALLOCATION_DESCRIPTION)) return false;
    /* Runtime-owned byte buffers need not have native struct alignment. */
    ADMISSION_PRESENT_RESOURCE_DATA r;
    ADMISSION_ALLOCATION_DESCRIPTION a;
    memcpy(&r, resource_data, sizeof(r));
    memcpy(&a, allocation_data, sizeof(a));
    if (r.Version != ADMISSION_PRESENT_RESOURCE_VERSION || !AdmissionPresentResourceDataValid(&r) ||
        (r.Flags & ADMISSION_PRESENT_RESOURCE_CONVERTED) || !AdmissionAllocationDescriptionValid(&a) ||
        a.Type != D3DKMDT_GDISURFACE_TEXTURE || a.BytesPerPixel != 4 || a.CpuVisible != 0 ||
        /* PITCH-2: the create path's size rule; the adapter's class limit is
         * applied by AdmissionUmdOpenResource, which this open calls next. */
        AdmissionPresentationSizeCheck(a.Width, a.Height, a.Size, AdmissionPresentationMaximumBytes()) !=
            ADMISSION_PRESENTATION_SIZE_OK ||
        (a.Size & 127ull) != 0) return false;
    const bool bgra = r.Format == DXGI_FORMAT_B8G8R8A8_UNORM || r.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if ((!bgra && r.Format != DXGI_FORMAT_R8G8B8A8_UNORM) ||
        a.Format != static_cast<unsigned>(bgra ? D3DDDIFMT_A8R8G8B8 : D3DDDIFMT_A8B8G8R8)) return false;
    *out = {r.Format, a.Width, a.Height, a.Pitch, a.Size};
    return true;
}
#endif
