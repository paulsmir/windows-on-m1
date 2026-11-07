#include <atomic>
#include <cstdint>
#include <cstdio>
#include <shared_mutex>
#include <thread>
#include <vector>
using UINT = unsigned;
using UINT64 = uint64_t;
struct SRWLOCK { std::shared_mutex value; };
static void AcquireSRWLockExclusive(SRWLOCK *p) { p->value.lock(); }
static void ReleaseSRWLockExclusive(SRWLOCK *p) { p->value.unlock(); }
static void AcquireSRWLockShared(SRWLOCK *p) { p->value.lock_shared(); }
static void ReleaseSRWLockShared(SRWLOCK *p) { p->value.unlock_shared(); }
struct AgxD3d12Heap { UINT64 gpuva, bytes; };
// Host shape for unused device members; this is not a Windows private ABI test.
struct AgxD3d12Device {
    UINT heap_count = 0;
    AgxD3d12Heap *heaps[4096] = {};
    SRWLOCK heap_lock;
};
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
/* CURRENT_FUNCTIONS */
int main() {
    constexpr unsigned threads = 16, per_thread = 32, count = threads * per_thread;
    AgxD3d12Device device;
    AgxD3d12Heap heaps[count];
    for (unsigned i = 0; i < count; ++i) heaps[i] = {UINT64(i + 1) * 65536, 65536};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < threads; ++t) workers.emplace_back([&,t] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = t; i < count; i += threads) RegisterHeap(&device, &heaps[i]);
    });
    start.store(true, std::memory_order_release);
    for (auto &t : workers) t.join();
    if (device.heap_count != count) { std::fprintf(stderr, "lost registrations: %u of %u\n", device.heap_count, count); return 1; }
    for (unsigned i = 0; i < count; ++i) {
        UINT64 offset = 0;
        if (HeapAt(&device, heaps[i].gpuva + 123, &offset) != &heaps[i] || offset != 123) return 2;
    }
    workers.clear(); start.store(false, std::memory_order_release);
    for (unsigned t = 0; t < threads; ++t) workers.emplace_back([&,t] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = t; i < count; i += threads) UnregisterHeap(&device, &heaps[i]);
    });
    for (unsigned t = 0; t < threads; ++t) workers.emplace_back([&,t] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = t; i < count; i += threads) {
            UINT64 offset = 0;
            auto *h = HeapAt(&device, heaps[i].gpuva + 31, &offset);
            if (h && (h != &heaps[i] || offset != 31)) std::abort();
        }
    });
    start.store(true, std::memory_order_release);
    for (auto &t : workers) t.join();
    if (device.heap_count) return 3;
    UINT64 offset = 99;
    if (HeapAt(&device, heaps[0].gpuva, &offset) || offset != 99) return 4;
    std::puts("HEAP_REGISTRY_CONCURRENT_VALID_LIFETIMES_PASS");
}
