/* Original six-callback behavioral fixture. SPDX-License-Identifier: MIT */
#include <windows.h>
#include <d3d12umddi.h>
#include "agx_d3d12_zero_cache_library.h"
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace {
int device_a, device_b;
struct Pso { bool compiled; } pso_a{true}, uncompiled_a{false}, pso_b{false};
bool pso_a_live = true;
unsigned checks;
void require(bool value, const char *why) {
    ++checks;
    if (!value) { std::fprintf(stderr, "ZERO_CACHE_CONTRACT_FAIL %s\n", why); std::abort(); }
}
struct Storage {
    alignas(64) std::array<unsigned char, 512> bytes;
    Storage() { bytes.fill(0xa5); }
    D3D12DDI_HPIPELINELIBRARY handle() { return {bytes.data() + 64}; }
    bool outside_untouched(SIZE_T size) const {
        for (SIZE_T i = 0; i < bytes.size(); ++i)
            if ((i < 64 || i >= 64 + size) && bytes[i] != 0xa5) return false;
        return true;
    }
};
}

extern "C" const void *AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE h) {
    if (h.pDrvPrivate == &device_a) return &device_a;
    if (h.pDrvPrivate == &device_b) return &device_b;
    return nullptr;
}
extern "C" const void *AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE h) {
    if ((h.pDrvPrivate == &pso_a && pso_a_live) || h.pDrvPrivate == &uncompiled_a) return &device_a;
    if (h.pDrvPrivate == &pso_b) return &device_b;
    return nullptr;
}

int main() {
    const D3D12DDI_HDEVICE a{&device_a}, b{&device_b}, invalid{};
    const long page = sysconf(_SC_PAGESIZE);
    require(page > 0, "host page size");
    void *guard = mmap(nullptr, static_cast<SIZE_T>(page), PROT_NONE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
    require(guard != MAP_FAILED, "inaccessible initial/output/handle guard page");
    const D3D12DDI_HDEVICE unknown{guard};
    D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 empty{0, nullptr};
    D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 guarded{0, guard};
    D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 nonzero{1, guard};
    const SIZE_T size = AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(a, &empty);
    require(size > 0 && size <= 384, "fixed private header fits supplied storage");
    require(AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(a, &guarded) == size, "zero guarded initial data unread");
    require(AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(a, &nonzero) == 0, "nonzero native payload unsupported");
    require(AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(a, nullptr) == 0, "null create descriptor");
    require(AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(invalid, &empty) == 0, "invalid device private size");
    require(AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(unknown, &empty) == 0, "unknown device address unread");
    require(AgxD3d12ZeroCacheCreatePipelineLibrary(a, &empty, {}) == E_INVALIDARG, "null create destination");
    require(AgxD3d12ZeroCacheCreatePipelineLibrary(a, &nonzero, {guard}) == E_INVALIDARG, "reject payload before destination/data access");
    Storage first, second;
    const auto untouched = first.bytes;
    require(AgxD3d12ZeroCacheCreatePipelineLibrary(unknown, &empty, first.handle()) == E_INVALIDARG, "unknown Create device");
    require(first.bytes == untouched, "invalid Create does not initialize storage");
    require(AgxD3d12ZeroCacheCreatePipelineLibrary(a, &guarded, first.handle()) == S_OK, "create with inaccessible zero input");
    require(first.outside_untouched(size), "create writes private header only");
    auto snapshot = first.bytes;
    require(AgxD3d12ZeroCacheCreatePipelineLibrary(a, &empty, first.handle()) == E_INVALIDARG, "duplicate live storage rejected");
    require(first.bytes == snapshot, "duplicate create leaves live storage unchanged");
    require(AgxD3d12ZeroCacheCreatePipelineLibrary(b, &empty, first.handle()) == E_INVALIDARG, "cross-device create rejects live storage");
    require(first.bytes == snapshot, "cross-device create cannot corrupt foreign header");
    require(AgxD3d12ZeroCacheCreatePipelineLibrary(b, &empty, second.handle()) == S_OK, "second device independent library");

    require(!uncompiled_a.compiled, "PSO is deliberately uncompiled");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(a, first.handle(), {&uncompiled_a}, UINT_MAX) == S_OK, "owned uncompiled PSO and opaque maximum index");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(a, first.handle(), {&pso_a}, 0) == S_OK, "owned compiled PSO");
    pso_a_live = false;
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(a, first.handle(), {&pso_a}, 0) == E_INVALIDARG, "resolver rejects destroyed PSO");
    pso_a_live = true;
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(a, first.handle(), {&uncompiled_a}, UINT_MAX) == S_OK, "no per-index retention/replacement state");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(a, first.handle(), {&pso_b}, 0) == E_INVALIDARG, "foreign PSO rejected");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(b, first.handle(), {&pso_b}, 0) == E_INVALIDARG, "foreign library rejected");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(a, first.handle(), {guard}, 0) == E_INVALIDARG, "invalid PSO address never dereferenced");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(a, {guard}, {&pso_a}, 0) == E_INVALIDARG, "invalid library address never dereferenced");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(invalid, first.handle(), {&pso_a}, 0) == E_INVALIDARG, "invalid Add device");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(unknown, first.handle(), {&pso_a}, 0) == E_INVALIDARG, "unknown Add device");
    require(first.bytes == snapshot, "Add retains no PSO or per-index data");

    std::array<unsigned char, 64> output; output.fill(0x6b);
    const auto canary = output;
    require(AgxD3d12ZeroCacheCalcSerializedLibrarySize(a, first.handle()) == 0, "valid native serialized extent zero");
    require(AgxD3d12ZeroCacheSerializeLibrary(a, first.handle(), output.data()) == S_OK, "serialize success");
    require(output == canary, "zero output bytes written");
    require(AgxD3d12ZeroCacheSerializeLibrary(a, first.handle(), output.data() + output.size()) == S_OK, "nonnull end pointer untouched");
    require(output == canary, "end pointer cannot change canary");
    require(AgxD3d12ZeroCacheSerializeLibrary(a, first.handle(), guard) == S_OK, "inaccessible output untouched");
    require(AgxD3d12ZeroCacheSerializeLibrary(a, first.handle(), nullptr) == S_OK, "zero extent needs no output access");
    require(AgxD3d12ZeroCacheSerializeLibrary(b, first.handle(), output.data()) == E_INVALIDARG, "foreign serialize rejected");
    require(AgxD3d12ZeroCacheSerializeLibrary(a, {guard}, output.data()) == E_INVALIDARG, "invalid serialize address unread");
    require(AgxD3d12ZeroCacheSerializeLibrary(invalid, first.handle(), output.data()) == E_INVALIDARG, "invalid serialize device");
    require(AgxD3d12ZeroCacheSerializeLibrary(unknown, first.handle(), output.data()) == E_INVALIDARG, "unknown Serialize device");
    require(AgxD3d12ZeroCacheCalcSerializedLibrarySize(b, first.handle()) == 0, "foreign size has no error channel");
    require(AgxD3d12ZeroCacheCalcSerializedLibrarySize(a, {guard}) == 0, "invalid size address unread");
    require(AgxD3d12ZeroCacheCalcSerializedLibrarySize(invalid, first.handle()) == 0, "invalid size device");
    require(AgxD3d12ZeroCacheCalcSerializedLibrarySize(unknown, first.handle()) == 0, "unknown size device");
    require(output == canary && first.bytes == snapshot, "invalid operations do not mutate output/header");

    AgxD3d12ZeroCacheDestroyPipelineLibrary(b, first.handle());
    require(first.bytes == snapshot, "foreign Destroy cannot corrupt header");
    AgxD3d12ZeroCacheDestroyPipelineLibrary(a, {guard});
    AgxD3d12ZeroCacheDestroyPipelineLibrary(invalid, first.handle());
    AgxD3d12ZeroCacheDestroyPipelineLibrary(unknown, first.handle());
    require(AgxD3d12ZeroCacheSerializeLibrary(a, first.handle(), guard) == S_OK, "valid library survived invalid Destroy");
    AgxD3d12ZeroCacheDestroyPipelineLibrary(a, first.handle());
    require(first.outside_untouched(size), "Destroy touches owned header only");
    require(AgxD3d12ZeroCacheSerializeLibrary(a, first.handle(), output.data()) == E_INVALIDARG, "destroyed library rejected");
    require(AgxD3d12ZeroCacheAddPipelineStateToLibrary(a, first.handle(), {&pso_a}, 0) == E_INVALIDARG, "destroyed library Add rejected");
    AgxD3d12ZeroCacheDestroyPipelineLibrary(a, first.handle());
    require(AgxD3d12ZeroCacheSerializeLibrary(b, second.handle(), guard) == S_OK, "unrelated live library survives removal");
    AgxD3d12ZeroCacheDestroyPipelineLibrary(b, second.handle());
    require(AgxD3d12ZeroCacheSerializeLibrary(b, second.handle(), guard) == E_INVALIDARG, "second library destroyed");

    // Runtime-supplied private storage can be reused AFTER correct destruction.
    D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 end_input{0, output.data() + output.size()};
    require(AgxD3d12ZeroCacheCreatePipelineLibrary(b, &end_input, first.handle()) == S_OK, "zero-span import end pointer");
    require(AgxD3d12ZeroCacheSerializeLibrary(a, first.handle(), guard) == E_INVALIDARG, "reused storage belongs only to new device");
    require(AgxD3d12ZeroCacheSerializeLibrary(b, first.handle(), guard) == S_OK, "new device live header");
    AgxD3d12ZeroCacheDestroyPipelineLibrary(b, first.handle());
    require(output == canary, "all paths preserve output canaries");
    require(munmap(guard, static_cast<SIZE_T>(page)) == 0, "guard released");
    std::printf("ZERO_NATIVE_CACHE_PASS checks=%u callbacks=6 actual_WDK_declarations=YES host_Windows_ABI=NO no_registration=YES\n", checks);
}
