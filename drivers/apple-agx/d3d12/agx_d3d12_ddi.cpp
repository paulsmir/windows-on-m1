/* D3D12 ABI gate. Deliberately no GPU access and no feature-level admission. */
#include <windows.h>
/* d3dkmddi.h expects NTSTATUS, which user-mode windows.h does not define. */
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#include <d3d12umddi.h>
#include <type_traits>

/* the UMD trace logger (umd_runtime_device.c; C linkage through umd_internal.h) */
extern "C" VOID AdmissionUmdDiagnostic(PCSTR Stage, HRESULT Status, const UINT *Values, UINT Count);

namespace {
static const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks;
static D3D12DDI_HRTDEVICE runtime_device;

template<class T> struct Rejected;
template<class R, class... A> struct Rejected<R (APIENTRY *)(A...)> {
    static R APIENTRY call(A...) {
        OutputDebugStringA("AGX D3D12 ABI gate: unimplemented DDI\n");
        if (callbacks && callbacks->pfnSetErrorCb)
            callbacks->pfnSetErrorCb(runtime_device, E_NOTIMPL);
        /* Command-list errors need the runtime list handle; no list is created here. */
        (void)(callbacks && callbacks->pfnSetCommandListErrorCb);
        if constexpr (std::is_same_v<R, HRESULT>) return E_NOTIMPL;
        else if constexpr (!std::is_void_v<R>) return R{};
    }
};
#define AGX_ASSIGN(T, M, F) (f).M = &F

static HRESULT APIENTRY GetVersions(D3D12DDI_HADAPTER, UINT32 *count, UINT64 *versions) {
    const UINT receipt[] = {count ? *count : 0, versions != nullptr};
    AdmissionUmdDiagnostic("d3d12-versions", count ? S_OK : E_INVALIDARG, receipt, 2);
    if (!count) return E_INVALIDARG;
    if (!versions) { *count = 1; return S_OK; }
    if (*count < 1) { *count = 1; return E_INVALIDARG; }
    versions[0] = D3D12DDI_SUPPORTED_0003;
    *count = 1;
    return S_OK;
}
static HRESULT APIENTRY GetCaps(D3D12DDI_HADAPTER, const D3D12DDIARG_GETCAPS *arg) {
    const UINT receipt[] = {arg ? static_cast<UINT>(arg->Type) : 0, arg ? arg->DataSize : 0};
    AdmissionUmdDiagnostic("d3d12-caps", E_NOTIMPL, receipt, 2);
    return E_NOTIMPL;
}
static HRESULT APIENTRY CreateDevice(D3D12DDI_HADAPTER,
                                     const D3D12DDIARG_CREATEDEVICE_0003 *arg) {
    if (arg) { callbacks = arg->p12UMCallbacks; runtime_device = arg->hRTDevice; }
    const UINT receipt[] = {arg ? arg->Interface : 0, arg ? arg->Version : 0};
    AdmissionUmdDiagnostic("d3d12-create-device", E_NOTIMPL, receipt, 2);
    if (callbacks && callbacks->pfnSetErrorCb)
        callbacks->pfnSetErrorCb(runtime_device, E_NOTIMPL);
    return E_NOTIMPL;
}
static HRESULT APIENTRY CloseAdapter(D3D12DDI_HADAPTER) {
    AdmissionUmdDiagnostic("d3d12-close-adapter", S_OK, nullptr, 0);
    return S_OK;
}
static void FillD3D12DDI_ADAPTERFUNCS(D3D12DDI_ADAPTERFUNCS &f) {
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnCalcPrivateDeviceSize, Rejected<decltype(((D3D12DDI_ADAPTERFUNCS *)0)->pfnCalcPrivateDeviceSize)>::call);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnCreateDevice, CreateDevice);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnCloseAdapter, CloseAdapter);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnGetSupportedVersions, GetVersions);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnGetCaps, GetCaps);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnGetOptionalDDITables, Rejected<decltype(((D3D12DDI_ADAPTERFUNCS *)0)->pfnGetOptionalDDITables)>::call);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnFillDDITable, Rejected<decltype(((D3D12DDI_ADAPTERFUNCS *)0)->pfnFillDDITable)>::call);
    AGX_ASSIGN(D3D12DDI_ADAPTERFUNCS, pfnDestroyDevice, Rejected<decltype(((D3D12DDI_ADAPTERFUNCS *)0)->pfnDestroyDevice)>::call);
}
[[maybe_unused]] static void FillD3D12DDI_DEVICE_FUNCS_CORE_0003(D3D12DDI_DEVICE_FUNCS_CORE_0003 &f) {
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckFormatSupport, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCheckFormatSupport)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckMultisampleQualityLevels, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCheckMultisampleQualityLevels)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetMipPacking, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnGetMipPacking)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateElementLayoutSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateElementLayoutSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateElementLayout, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateElementLayout)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyElementLayout, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyElementLayout)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateBlendStateSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateBlendStateSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateBlendState, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateBlendState)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyBlendState, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyBlendState)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDepthStencilStateSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateDepthStencilStateSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilState, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateDepthStencilState)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDepthStencilState, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyDepthStencilState)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRasterizerStateSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateRasterizerStateSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRasterizerState, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateRasterizerState)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRasterizerState, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyRasterizerState)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateShaderSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateShaderSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateVertexShader, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateVertexShader)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePixelShader, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreatePixelShader)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShader, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateGeometryShader)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateComputeShader, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateComputeShader)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateGeometryShaderWithStreamOutput, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateGeometryShaderWithStreamOutput)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateGeometryShaderWithStreamOutput, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateGeometryShaderWithStreamOutput)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateTessellationShaderSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateTessellationShaderSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHullShader, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateHullShader)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDomainShader, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateDomainShader)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyShader, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyShader)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandQueueSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateCommandQueueSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandQueue, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateCommandQueue)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandQueue, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyCommandQueue)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandAllocatorSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateCommandAllocatorSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandAllocator, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateCommandAllocator)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandAllocator, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyCommandAllocator)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnResetCommandAllocator, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnResetCommandAllocator)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivatePipelineStateSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivatePipelineStateSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreatePipelineState, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreatePipelineState)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyPipelineState, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyPipelineState)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandListSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateCommandListSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandList, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateCommandList)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandList, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyCommandList)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateFenceSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateFenceSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateFence, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateFence)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyFence, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyFence)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDescriptorHeapSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateDescriptorHeapSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDescriptorHeap, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateDescriptorHeap)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyDescriptorHeap, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyDescriptorHeap)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetDescriptorSizeInBytes, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnGetDescriptorSizeInBytes)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetCPUDescriptorHandleForHeapStart, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnGetCPUDescriptorHandleForHeapStart)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetGPUDescriptorHandleForHeapStart, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnGetGPUDescriptorHandleForHeapStart)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateShaderResourceView, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateShaderResourceView)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateConstantBufferView, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateConstantBufferView)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateSampler, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateSampler)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateUnorderedAccessView, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateUnorderedAccessView)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRenderTargetView, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateRenderTargetView)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDepthStencilView, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateDepthStencilView)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateRootSignatureSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateRootSignatureSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateRootSignature, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateRootSignature)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyRootSignature, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyRootSignature)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnSerializeObject, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnSerializeObject)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyObjectSerialization, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyObjectSerialization)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateDeserializedObjectSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateDeserializedObjectSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateDeserializedObject, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateDeserializedObject)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMapHeap, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnMapHeap)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnUnmapHeap, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnUnmapHeap)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateHeapAndResourceSizes, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateHeapAndResourceSizes)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateHeapAndResource, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateHeapAndResource)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyHeapAndResource, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyHeapAndResource)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMakeResident, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnMakeResident)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnEvict, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnEvict)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateOpenedHeapAndResourceSizes, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateOpenedHeapAndResourceSizes)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOpenHeapAndResource, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnOpenHeapAndResource)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCopyDescriptors, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCopyDescriptors)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCopyDescriptorsSimple, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCopyDescriptorsSimple)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateQueryHeapSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateQueryHeapSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateQueryHeap, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateQueryHeap)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyQueryHeap, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyQueryHeap)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCalcPrivateCommandSignatureSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCalcPrivateCommandSignatureSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCreateCommandSignature, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCreateCommandSignature)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnDestroyCommandSignature, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnDestroyCommandSignature)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceVirtualAddress, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCheckResourceVirtualAddress)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationInfo, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCheckResourceAllocationInfo)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckSubresourceInfo, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCheckSubresourceInfo)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckExistingResourceAllocationInfo, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCheckExistingResourceAllocationInfo)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnOfferResources, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnOfferResources)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnReclaimResources, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnReclaimResources)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnGetImplicitPhysicalAdapterMask)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetPresentPrivateDriverDataSize, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnGetPresentPrivateDriverDataSize)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnQueryNodeMap, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnQueryNodeMap)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnRetrieveShaderComment, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnRetrieveShaderComment)>::call);
    AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnCheckResourceAllocationHandle, Rejected<decltype(((D3D12DDI_DEVICE_FUNCS_CORE_0003 *)0)->pfnCheckResourceAllocationHandle)>::call);
}
[[maybe_unused]] static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0003(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 &f) {
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCloseCommandList, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnCloseCommandList)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResetCommandList, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnResetCommandList)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawInstanced, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnDrawInstanced)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDrawIndexedInstanced, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnDrawIndexedInstanced)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDispatch, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnDispatch)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewUint, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnClearUnorderedAccessViewUint)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearUnorderedAccessViewFloat, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnClearUnorderedAccessViewFloat)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRenderTargetView, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnClearRenderTargetView)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearDepthStencilView, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnClearDepthStencilView)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnDiscardResource, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnDiscardResource)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTextureRegion, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnCopyTextureRegion)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceCopy, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnResourceCopy)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyTiles, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnCopyTiles)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnCopyBufferRegion, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnCopyBufferRegion)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceResolveSubresource, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnResourceResolveSubresource)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteBundle, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnExecuteBundle)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnExecuteIndirect, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnExecuteIndirect)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResourceBarrier, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnResourceBarrier)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBlt, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnBlt)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnPresent, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnPresent)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnBeginQuery, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnBeginQuery)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnEndQuery, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnEndQuery)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnResolveQueryData, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnResolveQueryData)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPredication, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetPredication)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIaSetTopology, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnIaSetTopology)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetViewports, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnRsSetViewports)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnRsSetScissorRects, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnRsSetScissorRects)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetBlendFactor, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnOmSetBlendFactor)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOmSetStencilRef, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnOmSetStencilRef)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetPipelineState, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetPipelineState)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetDescriptorHeaps, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetDescriptorHeaps)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootSignature, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetComputeRootSignature)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootSignature, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetGraphicsRootSignature)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootDescriptorTable, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetComputeRootDescriptorTable)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootDescriptorTable, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetGraphicsRootDescriptorTable)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstant, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetComputeRoot32BitConstant)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstant, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetGraphicsRoot32BitConstant)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRoot32BitConstants, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetComputeRoot32BitConstants)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRoot32BitConstants, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetGraphicsRoot32BitConstants)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootConstantBufferView, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetComputeRootConstantBufferView)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootConstantBufferView, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetGraphicsRootConstantBufferView)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootShaderResourceView, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetComputeRootShaderResourceView)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootShaderResourceView, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetGraphicsRootShaderResourceView)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetComputeRootUnorderedAccessView, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetComputeRootUnorderedAccessView)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetGraphicsRootUnorderedAccessView, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetGraphicsRootUnorderedAccessView)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetIndexBuffer, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnIASetIndexBuffer)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnIASetVertexBuffers, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnIASetVertexBuffers)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSOSetTargets, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSOSetTargets)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnOMSetRenderTargets, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnOMSetRenderTargets)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnSetMarker, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnSetMarker)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003, pfnClearRootArguments, Rejected<decltype(((D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *)0)->pfnClearRootArguments)>::call);
}
[[maybe_unused]] static void FillD3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 &f) {
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnExecuteCommandLists, Rejected<decltype(((D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *)0)->pfnExecuteCommandLists)>::call);
    f.pfnUnused = nullptr;
    f.pfnUnused2 = nullptr;
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnUpdateTileMappings, Rejected<decltype(((D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *)0)->pfnUpdateTileMappings)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnCopyTileMappings, Rejected<decltype(((D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *)0)->pfnCopyTileMappings)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnSignalFence, Rejected<decltype(((D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *)0)->pfnSignalFence)>::call);
    AGX_ASSIGN(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001, pfnWaitForFence, Rejected<decltype(((D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *)0)->pfnWaitForFence)>::call);
}
} // namespace
extern "C" HRESULT APIENTRY OpenAdapter12(D3D12DDIARG_OPENADAPTER *arg) {
    char enabled[2] = {};
    if (GetEnvironmentVariableA("APPLE_AGX_D3D12", enabled, sizeof(enabled)) != 1 || enabled[0] != '1')
        return E_NOTIMPL;
    const UINT receipt[] = {arg != nullptr, arg && arg->pAdapterFuncs != nullptr,
                            arg && arg->pAdapterCallbacks != nullptr};
    AdmissionUmdDiagnostic("d3d12-open-adapter", arg && arg->pAdapterFuncs ? S_OK : E_INVALIDARG, receipt, 3);
    if (!arg || !arg->pAdapterFuncs) return E_INVALIDARG;
    arg->hAdapter.pDrvPrivate = nullptr;
    FillD3D12DDI_ADAPTERFUNCS(*arg->pAdapterFuncs);
    return S_OK;
}
