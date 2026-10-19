#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <tuple>
#include <type_traits>
// Host stand-in for unused Windows private-state lock; native ABI gate is separate.
struct SRWLOCK { uintptr_t value; };
static void InitializeSRWLock(SRWLOCK *p) { p->value = 0; }
using UINT = unsigned; using UINT64 = uint64_t; using ULONG = unsigned;
using LONG = int; using BOOL = int; using SIZE_T = size_t;
using VOID = void;
using HANDLE = void *;
using HRESULT = int32_t; using D3DKMT_HANDLE = unsigned;
#define APIENTRY
#define SUCCEEDED(hr) ((hr) >= 0)
static constexpr HRESULT S_OK = 0;
static constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057u);
static constexpr HRESULT E_NOTIMPL = static_cast<HRESULT>(0x80004001u);
static constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
static constexpr UINT D3D12DDI_TABLE_TYPE_DEVICE_CORE = 0;
static constexpr UINT D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D = 1;
static constexpr UINT D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D = 2;
struct D3D12DDI_HADAPTER {};
struct D3D12DDI_HRTDEVICE { void *handle; };
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HCOMMANDLIST { void *pDrvPrivate; };
struct D3D12DDI_HRTCOMMANDLIST { void *handle; };
struct D3D12DDI_HCOMMANDQUEUE { void *pDrvPrivate; };
struct D3D12DDI_HRTCOMMANDQUEUE { void *handle; };
struct D3D12DDIARG_FENCE_OPERATION { UINT unused; };
struct D3DDDICB_CREATECONTEXTVIRTUAL {
    UINT NodeOrdinal, EngineAffinity;
    void *pPrivateDriverData;
    UINT PrivateDriverDataSize;
    HANDLE hContext;
};
struct ADMISSION_WIN32_CONTEXT_CREATE {
    uint32_t Magic;
    uint16_t Version, Bytes;
    uint32_t Generation, Reserved;
};
static constexpr UINT ADMISSION_WIN32_CONTEXT_MAGIC = 0x43574157u;
static constexpr UINT ADMISSION_WIN32_CONTEXT_VERSION = 1;
struct D3D12DDIARG_CREATECOMMANDQUEUE_0001 {
    D3D12DDI_HCOMMANDQUEUE hDrvCommandQueue;
    D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue;
    UINT QueueFlags, NodeMask;
};
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
struct D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 {
    void *preceding_wdk_pointer_slots[15];
    VOID (APIENTRY *pfnExecuteBundle)(D3D12DDI_HCOMMANDLIST, D3D12DDI_HCOMMANDLIST);
};
struct D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 {
    void *preceding_wdk_pointer_slots[5];
    VOID (APIENTRY *pfnSignalFence)(D3D12DDI_HCOMMANDQUEUE, D3D12DDIARG_FENCE_OPERATION *);
};
struct D3D12DDI_DEVICE_FUNCS_CORE_0003 {
    void *preceding_wdk_pointer_slots[84];
    UINT (APIENTRY *pfnGetImplicitPhysicalAdapterMask)(D3D12DDI_HDEVICE);
};
static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0003,
                       pfnGetImplicitPhysicalAdapterMask) == 84 * sizeof(void *));
struct D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 {
    void (APIENTRY *pfnSetErrorCb)(D3D12DDI_HRTDEVICE, HRESULT);
    void (APIENTRY *pfnSetCommandListErrorCb)(D3D12DDI_HRTCOMMANDLIST, HRESULT);
    HRESULT (APIENTRY *pfnCreateContextVirtualCb)(D3D12DDI_HRTCOMMANDQUEUE, D3DDDICB_CREATECONTEXTVIRTUAL *);
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
static UINT expected_table = D3D12DDI_TABLE_TYPE_DEVICE_CORE;
static UINT expected_offset = 84 * sizeof(void *);
static void OutputDebugStringA(const char *) {}
static UINT GetCurrentProcessId() { return 17; }
static LONG InterlockedIncrement(volatile LONG *value) { return ++*value; }
static void AdmissionUmdDiagnostic(const char *name, HRESULT status,
                                   const UINT *values, UINT count) {
    if (std::strcmp(name, "measure-d3d12-rejected")) return;
    if (status != E_NOTIMPL ||
        count != 2 || !values || values[0] != expected_table ||
        values[1] != expected_offset) std::abort();
    ++diagnostics;
}
/* ACTUAL_REJECTED */
/* Only pointer fields and the runtime/device prefix are exercised below.
 * These unused value-type stand-ins do not establish native list layout. */
using D3D12DDI_GPU_VIRTUAL_ADDRESS = UINT64;
using FLOAT = float;
static constexpr UINT kRootArguments = 64, kVertexBuffers = 32;
struct AgxD3d12PipelineState;
struct AgxD3d12RootSignature;
struct AgxD3d12Command;
struct AgxD3d12Descriptor { UINT unused; };
struct AgxD3d12VertexBuffer { UINT unused; };
struct D3D12DDI_VIEWPORT { FLOAT unused; };
struct RECT { LONG unused; };
/* ACTUAL_DEVICE */
static void Fill(D3D12DDI_DEVICE_FUNCS_CORE_0003 &f) {
    /* ACTUAL_TABLE_ASSIGNMENT */
}
static void FillList(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 &f) {
    /* ACTUAL_LIST_ASSIGNMENT */
}
static void FillQueue(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 &f) {
    /* ACTUAL_QUEUE_ASSIGNMENT */
}
static unsigned calls[2], wrong_handle;
static unsigned list_calls[2];
static int list_tokens[2];
static HRESULT expected_status = E_NOTIMPL;
static int runtime_tokens[2];
static int queue_tokens[2];
static unsigned contexts_created;
static void callback_a(D3D12DDI_HRTDEVICE runtime, HRESULT hr) {
    ++calls[0];
    if (runtime.handle != &runtime_tokens[0] || hr != expected_status) ++wrong_handle;
}
static void callback_b(D3D12DDI_HRTDEVICE runtime, HRESULT hr) {
    ++calls[1];
    if (runtime.handle != &runtime_tokens[1] || hr != expected_status) ++wrong_handle;
}
static void list_callback_a(D3D12DDI_HRTCOMMANDLIST runtime, HRESULT hr) {
    ++list_calls[0];
    if (runtime.handle != &list_tokens[0] || hr != E_NOTIMPL) ++wrong_handle;
}
static void list_callback_b(D3D12DDI_HRTCOMMANDLIST runtime, HRESULT hr) {
    ++list_calls[1];
    if (runtime.handle != &list_tokens[1] || hr != E_NOTIMPL) ++wrong_handle;
}
static HRESULT create_context(D3D12DDI_HRTCOMMANDQUEUE runtime, D3DDDICB_CREATECONTEXTVIRTUAL *context) {
    if (!context || (runtime.handle != &queue_tokens[0] && runtime.handle != &queue_tokens[1]) ||
        context->NodeOrdinal != 0 || context->EngineAffinity != 1 ||
        context->PrivateDriverDataSize != sizeof(ADMISSION_WIN32_CONTEXT_CREATE)) return E_INVALIDARG;
    ++contexts_created;
    context->hContext = runtime.handle; /* Owned CPU token, no native context. */
    return S_OK;
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
    D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 lists{};
    FillList(lists);
    AgxD3d12CommandList owned_lists[2]{};
    D3D12DDI_HCOMMANDLIST handles[2]{};
    callback_tables[0].pfnSetCommandListErrorCb = list_callback_a;
    callback_tables[1].pfnSetCommandListErrorCb = list_callback_b;
    for (unsigned i = 0; i != 2; ++i) {
        owned_lists[i].device = &devices[i];
        owned_lists[i].runtime.handle = &list_tokens[i];
        handles[i].pDrvPrivate = &owned_lists[i];
    }
    expected_status = E_NOTIMPL;
    expected_table = D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D;
    expected_offset = 15 * sizeof(void *);
    calls[0] = calls[1] = wrong_handle = diagnostics = 0;
    for (unsigned i : {0u, 1u, 0u}) lists.pfnExecuteBundle(handles[i], handles[1-i]);
    if (calls[0] || calls[1] || list_calls[0] != 2 || list_calls[1] != 1 || wrong_handle || diagnostics != 3) {
        std::fprintf(stderr, "list-route deviceA=%u deviceB=%u listA=%u listB=%u wrong=%u receipts=%u\n",
                     calls[0], calls[1], list_calls[0], list_calls[1], wrong_handle, diagnostics);
        return 12;
    }
    callback_tables[1].pfnSetCommandListErrorCb = nullptr;
    list_calls[0] = list_calls[1] = diagnostics = 0;
    lists.pfnExecuteBundle(handles[0], handles[1]);
    lists.pfnExecuteBundle(handles[1], handles[0]);
    if (list_calls[0] != 1 || list_calls[1] || calls[0] || calls[1] || diagnostics != 2) return 13;
    D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 queue_table{};
    FillQueue(queue_table);
    const SIZE_T queue_bytes = CalcPrivateCommandQueueSize(args[0].hDrvDevice, nullptr);
    if (!queue_bytes) return 14;
    unsigned char *queue_storage[2]{};
    D3D12DDIARG_CREATECOMMANDQUEUE_0001 queue_args[2]{};
    for (unsigned i = 0; i != 2; ++i) {
        callback_tables[i].pfnCreateContextVirtualCb = create_context;
        queue_storage[i] = static_cast<unsigned char *>(std::malloc(queue_bytes + 16));
        if (!queue_storage[i]) return 14;
        std::memset(queue_storage[i], 0xa5, queue_bytes + 16);
        queue_args[i].hDrvCommandQueue.pDrvPrivate = queue_storage[i];
        queue_args[i].hRTCommandQueue.handle = &queue_tokens[i];
        if (CreateCommandQueue(args[i].hDrvDevice, &queue_args[i]) != S_OK) return 14;
        for (SIZE_T guard = 0; guard != 16; ++guard)
            if (queue_storage[i][queue_bytes + guard] != 0xa5) return 18;
    }
    if (contexts_created != 2) return 15;
    expected_table = D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D;
    expected_offset = 5 * sizeof(void *);
    calls[0] = calls[1] = wrong_handle = diagnostics = 0;
    D3D12DDIARG_FENCE_OPERATION operation{};
    for (unsigned i : {0u, 1u, 0u}) queue_table.pfnSignalFence(queue_args[i].hDrvCommandQueue, &operation);
    if (calls[0] != 2 || calls[1] != 1 || wrong_handle || diagnostics != 3) {
        std::fprintf(stderr, "queue-route A=%u B=%u wrong=%u receipts=%u\n", calls[0], calls[1], wrong_handle, diagnostics);
        return 16;
    }
    callback_tables[1].pfnSetErrorCb = nullptr;
    calls[0] = calls[1] = diagnostics = 0;
    queue_table.pfnSignalFence(queue_args[0].hDrvCommandQueue, &operation);
    queue_table.pfnSignalFence(queue_args[1].hDrvCommandQueue, &operation);
    if (calls[0] != 1 || calls[1] || diagnostics != 2) return 17;
    std::free(queue_storage[0]);
    std::free(queue_storage[1]);
    std::puts("DEVICE_LIST_QUEUE_ERROR_ROUTE_PASS");
    return 0;
}
