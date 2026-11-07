#ifndef AGX_D3D12_QUEUE_SYNC_H
#define AGX_D3D12_QUEUE_SYNC_H
/* C83: include after windows.h/d3dumddi.h. The KT callback takes the
 * device runtime handle; hContext selects the waiting application queue. */
/* V is the actual accepted render fence, including completion downloads.
 * Zero means there has been no accepted submission: use the CPU fallback. */
static inline HRESULT AgxD3d12QueueWaitForOwner(const D3DDDI_DEVICECALLBACKS *k,
                                              HANDLE device, HANDLE context,
                                              D3DKMT_HANDLE object, UINT64 value) {
    if (!k || !k->pfnWaitForSynchronizationObjectFromGpuCb || !device || !context || !object || !value)
        return S_FALSE;
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU wait = {};
    wait.hContext = context;
    wait.ObjectCount = 1;
    wait.ObjectHandleArray = &object;
    wait.MonitoredFenceValueArray = &value;
    return k->pfnWaitForSynchronizationObjectFromGpuCb(device, &wait);
}
/* Q11: a proven completed value can discharge the outgoing edge without
 * another VidSch packet. Never interpret a device-removal sentinel as success.
 * The caller obtains this value atomically from its own completion cache. */
static inline bool AgxD3d12OwnerFenceComplete(UINT64 completed, UINT64 value) {
    return value != 0 && completed != ~static_cast<UINT64>(0) && completed >= value;
}
#endif
