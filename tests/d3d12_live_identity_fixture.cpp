#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <sys/mman.h>

struct SRWLOCK { std::shared_mutex value; };
#define SRWLOCK_INIT {}
static void AcquireSRWLockExclusive(SRWLOCK *p) { p->value.lock(); }
static void ReleaseSRWLockExclusive(SRWLOCK *p) { p->value.unlock(); }
static void AcquireSRWLockShared(SRWLOCK *p) { p->value.lock_shared(); }
static void ReleaseSRWLockShared(SRWLOCK *p) { p->value.unlock_shared(); }

struct AgxD3d12Device {
    AgxD3d12Device *identity_next = nullptr;
};
struct AgxD3d12PipelineState {
    AgxD3d12Device *identity_owner = nullptr;
    AgxD3d12PipelineState *identity_next = nullptr;
};
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HPIPELINESTATE { void *pDrvPrivate; };
/* ACTUAL_IDENTITY_FUNCTIONS */

static void require(bool yes, const char *why) {
    if (!yes) { std::fprintf(stderr, "IDENTITY_FAIL %s\n", why); std::exit(3); }
}
int main() {
    AgxD3d12Device a, b;
    AgxD3d12PipelineState pa, pb;
    auto *guard = mmap(nullptr, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    require(guard != MAP_FAILED, "guard allocation");
    require(AgxD3d12ZeroCacheResolveDevice({&a}) == nullptr, "unpublished device");
    require(AgxD3d12ZeroCacheResolveDevice({guard}) == nullptr, "unknown device dereference");
    PublishDeviceIdentity(&a);
    PublishDeviceIdentity(&b);
    require(AgxD3d12ZeroCacheResolveDevice({&a}) == &a, "device a");
    require(AgxD3d12ZeroCacheResolveDevice({&b}) == &b, "device b");
    PublishPsoIdentity(&pa, &a);
    PublishPsoIdentity(&pb, &b);
    require(AgxD3d12ZeroCacheResolvePsoOwner({&pa}) == &a, "pso a owner");
    require(AgxD3d12ZeroCacheResolvePsoOwner({&pb}) == &b, "pso b owner");
    require(AgxD3d12ZeroCacheResolvePsoOwner({guard}) == nullptr, "unknown pso dereference");
    RetirePsoIdentity(&pa);
    require(AgxD3d12ZeroCacheResolvePsoOwner({&pa}) == nullptr, "retired pso a");
    require(AgxD3d12ZeroCacheResolvePsoOwner({&pb}) == &b, "unrelated pso b");
    RetireDeviceIdentity(&b);
    require(AgxD3d12ZeroCacheResolveDevice({&b}) == nullptr, "retired device b");
    require(AgxD3d12ZeroCacheResolvePsoOwner({&pb}) == nullptr, "orphan pso b");
    RetirePsoIdentity(&pb);

    constexpr unsigned workers = 8, total = 128;
    AgxD3d12PipelineState objects[total];
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < workers; ++t) threads.emplace_back([&, t] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = t; i < total; i += workers) PublishPsoIdentity(&objects[i], &a);
    });
    start.store(true, std::memory_order_release);
    for (auto &thread : threads) thread.join();
    for (auto &object : objects)
        require(AgxD3d12ZeroCacheResolvePsoOwner({&object}) == &a, "concurrent publication");
    threads.clear(); start.store(false, std::memory_order_release);
    for (unsigned t = 0; t < workers; ++t) threads.emplace_back([&, t] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = t; i < total; i += workers) RetirePsoIdentity(&objects[i]);
    });
    for (unsigned t = 0; t < workers; ++t) threads.emplace_back([&, t] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = t; i < total; i += workers) {
            auto *owner = AgxD3d12ZeroCacheResolvePsoOwner({&objects[i]});
            require(owner == nullptr || owner == &a, "concurrent owner mismatch");
        }
    });
    start.store(true, std::memory_order_release);
    for (auto &thread : threads) thread.join();
    for (auto &object : objects)
        require(AgxD3d12ZeroCacheResolvePsoOwner({&object}) == nullptr, "retirement incomplete");
    RetireDeviceIdentity(&a);
    require(AgxD3d12ZeroCacheResolveDevice({&a}) == nullptr, "retired device a");
    require(munmap(guard, 4096) == 0, "guard release");
    std::puts("D3D12_LIVE_IDENTITY_PASS");
}
