#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <tuple>
#include <type_traits>
using UINT = unsigned; using UINT64 = uint64_t; using ULONG = unsigned;
using LONG = int; using BOOL = int; using SIZE_T = size_t;
using VOID = void;
using HRESULT = int32_t; using D3DKMT_HANDLE = unsigned;
#define APIENTRY
#define SUCCEEDED(hr) ((hr) >= 0)
static constexpr HRESULT S_OK = 0;
static constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057u);
static constexpr HRESULT E_NOTIMPL = static_cast<HRESULT>(0x80004001u);
static constexpr UINT D3D12DDI_TABLE_TYPE_DEVICE_CORE = 0;
static constexpr UINT D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D = 1;
static constexpr UINT D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D = 2;
struct D3D12DDI_HADAPTER {};
struct D3D12DDI_HRTDEVICE { void *handle; };
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HSHADER { void *pDrvPrivate; };
struct D3D12DDI_HROOTSIGNATURE {};
struct D3D12DDIARG_STAGE_IO_SIGNATURES {};
using D3D12DDI_CREATE_SHADER_FLAGS = UINT;
struct D3D12DDI_HBLENDSTATE { void *pDrvPrivate; };
struct D3D12DDI_HELEMENTLAYOUT { void *pDrvPrivate; };
struct D3D12DDI_BLEND_DESC { UINT value; };
struct D3D12DDI_DEPTH_STENCIL_DESC { UINT value; };
struct D3D12DDI_RASTERIZER_DESC { UINT value; };
struct D3D12DDIARG_INPUT_ELEMENT_DESC { UINT value; };
struct D3D12DDIARG_CREATEELEMENTLAYOUT {
    const D3D12DDIARG_INPUT_ELEMENT_DESC *pVertexElements;
    UINT NumElements;
};
struct D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 {};
struct D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 {};
struct D3D12DDI_DEVICE_FUNCS_CORE_0003 {
    void *preceding_wdk_pointer_slots[84];
    UINT (APIENTRY *pfnGetImplicitPhysicalAdapterMask)(D3D12DDI_HDEVICE);
};
static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0003,
                       pfnGetImplicitPhysicalAdapterMask) == 84 * sizeof(void *));
struct D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 {
    void (APIENTRY *pfnSetErrorCb)(D3D12DDI_HRTDEVICE, HRESULT);
    void (APIENTRY *pfnSetCommandListErrorCb)();
};
struct D3DDDI_DEVICECALLBACKS {};
struct D3D12DDIARG_CREATEDEVICE_0003 {
    UINT Interface, Version, Flags;
    D3D12DDI_HDEVICE hDrvDevice;
    D3D12DDI_HRTDEVICE hRTDevice;
    const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *p12UMCallbacks;
    const D3DDDI_DEVICECALLBACKS *pKTCallbacks;
};
struct AgxD3d12Heap;
static volatile LONG queue_generation;
static const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks;
static D3D12DDI_HRTDEVICE runtime_device;
static unsigned diagnostics;
static void OutputDebugStringA(const char *) {}
static UINT GetCurrentProcessId() { return 17; }
static LONG InterlockedIncrement(volatile LONG *value) { return ++*value; }
static void AdmissionUmdDiagnostic(const char *name, HRESULT status,
                                   const UINT *values, UINT count) {
    if (std::strcmp(name, "measure-d3d12-rejected")) return;
    if (status != E_NOTIMPL ||
        count != 2 || !values || values[0] != D3D12DDI_TABLE_TYPE_DEVICE_CORE ||
        values[1] != 84 * sizeof(void *)) std::abort();
    ++diagnostics;
}
/* ACTUAL_REJECTED */
/* ACTUAL_DEVICE */
static void Fill(D3D12DDI_DEVICE_FUNCS_CORE_0003 &f) {
    /* ACTUAL_TABLE_ASSIGNMENT */
}
static unsigned calls[2], wrong_handle;
static HRESULT expected_status = E_NOTIMPL;
static int runtime_tokens[2];
static void callback_a(D3D12DDI_HRTDEVICE runtime, HRESULT hr) {
    ++calls[0];
    if (runtime.handle != &runtime_tokens[0] || hr != expected_status) ++wrong_handle;
}
static void callback_b(D3D12DDI_HRTDEVICE runtime, HRESULT hr) {
    ++calls[1];
    if (runtime.handle != &runtime_tokens[1] || hr != expected_status) ++wrong_handle;
}
int main() {
    D3D12DDI_DEVICE_FUNCS_CORE_0003 table{};
    Fill(table);
    AgxD3d12Device devices[2]{};
    D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 callback_tables[2]{};
    callback_tables[0].pfnSetErrorCb = callback_a;
    callback_tables[1].pfnSetErrorCb = callback_b;
    D3D12DDIARG_CREATEDEVICE_0003 args[2]{};
    for (unsigned i = 0; i != 2; ++i) {
        args[i].hDrvDevice.pDrvPrivate = &devices[i];
        args[i].hRTDevice.handle = &runtime_tokens[i];
        args[i].p12UMCallbacks = &callback_tables[i];
    }
    for (unsigned reverse = 0; reverse != 2; ++reverse) {
        calls[0] = calls[1] = wrong_handle = diagnostics = 0;
        for (unsigned pass = 0; pass != 2; ++pass) {
            const unsigned i = reverse ? 1 - pass : pass;
            if (CreateDevice({}, &args[i]) != S_OK) return 2;
        }
        for (unsigned i : {0u, 1u, 0u}) {
            if (table.pfnGetImplicitPhysicalAdapterMask(args[i].hDrvDevice) != 0) return 3;
        }
        if (calls[0] != 2 || calls[1] != 1 || wrong_handle || diagnostics != 3) {
            std::fprintf(stderr, "reverse=%u A=%u B=%u wrong=%u receipts=%u\n",
                         reverse, calls[0], calls[1], wrong_handle, diagnostics);
            return 1;
        }
    }
    callback_tables[1].pfnSetErrorCb = nullptr;
    calls[0] = calls[1] = diagnostics = 0;
    if (CreateDevice({}, &args[0]) != S_OK || CreateDevice({}, &args[1]) != S_OK) return 4;
    table.pfnGetImplicitPhysicalAdapterMask(args[0].hDrvDevice);
    table.pfnGetImplicitPhysicalAdapterMask(args[1].hDrvDevice);
    if (calls[0] != 1 || calls[1] || diagnostics != 2) return 5;
    callback_tables[1].pfnSetErrorCb = callback_b;
    expected_status = E_INVALIDARG;
    calls[0] = calls[1] = wrong_handle = 0;
    CreateComputeShader(args[0].hDrvDevice, nullptr, {}, {}, 0);
    if (calls[0] != 1 || calls[1] != 0 || wrong_handle) {
        std::fprintf(stderr, "compute-invalid A=%u B=%u wrong=%u\n", calls[0], calls[1], wrong_handle);
        return 6;
    }
    calls[0] = calls[1] = wrong_handle = 0;
    if (CreateDevice({}, &args[1]) != S_OK || CreateDevice({}, &args[0]) != S_OK) return 7;
    CreateShader(args[1].hDrvDevice, nullptr, {}, {}, nullptr, 0);
    if (calls[0] != 0 || calls[1] != 1 || wrong_handle) {
        std::fprintf(stderr, "graphics-invalid A=%u B=%u wrong=%u\n", calls[0], calls[1], wrong_handle);
        return 8;
    }
    calls[0] = calls[1] = wrong_handle = 0;
    BlendState::create(args[1].hDrvDevice, nullptr, D3D12DDI_HBLENDSTATE{});
    if (calls[0] != 0 || calls[1] != 1 || wrong_handle) {
        std::fprintf(stderr, "state-invalid A=%u B=%u wrong=%u\n", calls[0], calls[1], wrong_handle);
        return 9;
    }
    calls[0] = calls[1] = wrong_handle = 0;
    if (CreateDevice({}, &args[0]) != S_OK || CreateDevice({}, &args[1]) != S_OK) return 10;
    CreateElementLayout(args[0].hDrvDevice, nullptr, {});
    if (calls[0] != 1 || calls[1] != 0 || wrong_handle) {
        std::fprintf(stderr, "element-invalid A=%u B=%u wrong=%u\n", calls[0], calls[1], wrong_handle);
        return 11;
    }
    std::puts("DEVICE_ERROR_ROUTE_PASS");
    return 0;
}
