/* CPU dependent-load latency (EXP2240): same footprints as the GPU MEMBW probe, to tell a GPU-side
 * problem from a SoC fabric / memory-controller performance state. Builds on Windows (clang-cl) and
 * macOS (clang). The chase visits the 64-byte lines of one 16 KiB page in random order before
 * jumping to a random next page, so TLB misses are amortised over 256 loads (Windows/ARM64 uses
 * 4 KiB pages and runs under stage 2) and the result is cache/DRAM latency.
 * Usage: cpu_memlat [cpu-index]  (Windows: pins the thread to that logical CPU; macOS: user-interactive QoS) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
static double now_ns(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e9 / (double)f.QuadPart; }
#else
#include <time.h>
#include <pthread.h>
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
#endif
static uint32_t rng = 2463534242u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
int main(int argc, char **argv) {
    const size_t MAXB = (size_t)256 << 20, LINE = 64, PAGE = 16384, LPP = PAGE / LINE;
    static const unsigned kib[] = {16, 64, 256, 1024, 4096, 8192, 16384, 32768, 65536, 131072};
#ifdef _WIN32
    if (argc > 1) SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << atoi(argv[1]));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    char *buf = (char *)VirtualAlloc(NULL, MAXB, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    (void)argc; (void)argv;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    char *buf = NULL; if (posix_memalign((void **)&buf, PAGE, MAXB)) buf = NULL;
#endif
    if (!buf) { printf("alloc failed\n"); return 1; }
    for (size_t i = 0; i < MAXB; i += 4096) buf[i] = 1;
    for (unsigned k = 0; k < sizeof kib / sizeof kib[0]; ++k) {
        size_t bytes = (size_t)kib[k] << 10, pages = bytes / PAGE, n = bytes / LINE;
        if (pages == 0) pages = 1;
        uint32_t *porder = malloc(pages * 4), *lorder = malloc(LPP * 4);
        for (size_t i = 0; i < pages; ++i) porder[i] = (uint32_t)i;
        for (size_t i = pages - 1; i > 0; --i) { size_t j = xr() % (i + 1); uint32_t v = porder[i]; porder[i] = porder[j]; porder[j] = v; }
        /* build one cycle: for each page in porder, its lines in a random order */
        size_t lines_in_page = bytes < PAGE ? n : LPP, prev = (size_t)-1, first = 0;
        for (size_t p = 0; p < pages; ++p) {
            for (size_t i = 0; i < lines_in_page; ++i) lorder[i] = (uint32_t)i;
            for (size_t i = lines_in_page - 1; i > 0; --i) { size_t j = xr() % (i + 1); uint32_t v = lorder[i]; lorder[i] = lorder[j]; lorder[j] = v; }
            for (size_t i = 0; i < lines_in_page; ++i) {
                size_t off = (size_t)porder[p] * PAGE + (size_t)lorder[i] * LINE;
                if (prev == (size_t)-1) first = off; else *(void **)(buf + prev) = buf + off;
                prev = off;
            }
        }
        *(void **)(buf + prev) = buf + first;
        void **q = (void **)(buf + first);
        const size_t steps = 20u << 20;
        for (size_t i = 0; i < steps / 4; ++i) q = (void **)*q;   /* warm */
        double best = 1e30;
        for (int r = 0; r < 3; ++r) {
            double t0 = now_ns();
            for (size_t i = 0; i < steps; ++i) q = (void **)*q;
            double ns = (now_ns() - t0) / (double)steps;
            if (ns < best) best = ns;
        }
        printf("CPU_CHASE_NS %u KiB %.2f%s\n", kib[k], best, q ? "" : " ");
        free(porder); free(lorder);
    }
    return 0;
}
