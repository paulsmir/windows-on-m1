#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using UINT = unsigned; using UINT64 = uint64_t; using FLOAT = float;
using LONG = int; using VOID = void; using HRESULT = int32_t;
using D3D12DDI_GPU_VIRTUAL_ADDRESS = UINT64;
#define APIENTRY
static constexpr HRESULT S_OK = 0;
struct D3D12DDI_HCOMMANDLIST { void *pDrvPrivate; };
struct D3D12DDI_HROOTSIGNATURE { void *pDrvPrivate; };
struct D3D12DDI_HRTCOMMANDLIST { void *handle; };
struct D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 {
    void (*preceding[ CLEAR_ROOT_SLOT ])();
    void (*pfnClearRootArguments)();
};
struct AgxD3d12Device;
struct AgxD3d12PipelineState;
struct AgxD3d12Heap;
struct AgxD3d12Command;
/* Only identity is consumed. These opaque identities and unused value-type
 * stand-ins do not establish native root/list layout or shader binding ABI. */
struct AgxD3d12RootSignature { UINT opaque_identity; };
struct AgxD3d12Descriptor { UINT unused; };
struct AgxD3d12VertexBuffer { UINT unused; };
struct D3D12DDI_VIEWPORT { FLOAT unused; };
struct RECT { LONG unused; };
static unsigned repeated[2];
static unsigned bound_values[2][2];
static void AdmissionUmdDiagnostic(const char *name, HRESULT hr, const UINT *values, UINT count) {
    if (std::strcmp(name, "measure-d3d12-root-redundant")) return;
    if (hr != S_OK || count != 3 || !values || values[0] > 1) std::abort();
    ++repeated[values[0]];
    bound_values[values[0]][0] = values[1];
    bound_values[values[0]][1] = values[2];
}
/* CURRENT_FUNCTIONS */
static void check(bool value, const char *why) {
    if (!value) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
template<class T, size_t N> static bool equal(const T (&a)[N], const T (&b)[N]) {
    return std::memcmp(a, b, sizeof(a)) == 0;
}
template<class T, size_t N> static bool zero(const T (&a)[N]) {
    for (auto value : a) if (value != 0) return false;
    return true;
}
static void seed(AgxD3d12CommandList &list) {
    for (UINT i = 0; i != kRootArguments; ++i) {
        list.compute_root_va[i] = UINT64(0x100000) + i;
        list.compute_root_dwords[i] = 0x200 + i;
        list.graphics_root_va[i] = UINT64(0x300000) + i;
        list.graphics_root_dwords[i] = 0x400 + i;
    }
}
int main() {
    AgxD3d12CommandList list{};
    AgxD3d12RootSignature roots[2]{{1}, {2}};
    D3D12DDI_HCOMMANDLIST h{&list};
    D3D12DDI_HROOTSIGNATURE a{&roots[0]}, b{&roots[1]};
    SetComputeRootSignature(h, a);
    SetGraphicsRootSignature(h, a);
    seed(list);
    auto saved = list;
    SetComputeRootSignature(h, a);
    check(equal(list.compute_root_va, saved.compute_root_va) &&
          equal(list.compute_root_dwords, saved.compute_root_dwords),
          "same compute root identity discarded bound arguments");
    check(equal(list.graphics_root_va, saved.graphics_root_va) &&
          equal(list.graphics_root_dwords, saved.graphics_root_dwords), "compute setter changed graphics bank");
    SetGraphicsRootSignature(h, a);
    check(equal(list.graphics_root_va, saved.graphics_root_va) &&
          equal(list.graphics_root_dwords, saved.graphics_root_dwords),
          "same graphics root identity discarded bound arguments");
    check(equal(list.compute_root_va, saved.compute_root_va) &&
          equal(list.compute_root_dwords, saved.compute_root_dwords), "graphics setter changed compute bank");
    check(repeated[0] == 1 && repeated[1] == 1, "redundant DDI reachability receipt missing");
    check(bound_values[0][0] == kRootArguments && bound_values[0][1] == kRootArguments &&
          bound_values[1][0] == kRootArguments && bound_values[1][1] == kRootArguments,
          "reachability receipt cannot distinguish bound state from empty initialization");
    SetComputeRootSignature(h, b);
    check(list.compute_root == &roots[1] && zero(list.compute_root_va) && zero(list.compute_root_dwords),
          "changed compute identity did not invalidate its arguments");
    check(equal(list.graphics_root_va, saved.graphics_root_va), "compute identity change invalidated graphics");
    seed(list);
    saved = list;
    SetGraphicsRootSignature(h, b);
    check(list.graphics_root == &roots[1] && zero(list.graphics_root_va) && zero(list.graphics_root_dwords),
          "changed graphics identity did not invalidate its arguments");
    check(equal(list.compute_root_va, saved.compute_root_va), "graphics identity change invalidated compute");
    seed(list);
    ClearRootArguments(h);
    check(list.compute_root == &roots[1] && list.graphics_root == &roots[1] &&
          zero(list.compute_root_va) && zero(list.compute_root_dwords) &&
          zero(list.graphics_root_va) && zero(list.graphics_root_dwords),
          "explicit root clear lost its separate semantics");
    seed(list);
    SetComputeRootSignature(h, {});
    check(!list.compute_root && zero(list.compute_root_va) && zero(list.compute_root_dwords),
          "null selection no longer clears the compute bank");
    std::puts("ROOT_SIGNATURE_IDENTITY_PASS");
}
