/* First-eight-byte R1 PROGRAM span contract; next page remains PROT_NONE. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
using UINT = std::uint32_t;
using SIZE_T = std::size_t;
#include "../drivers/apple-agx/d3d12/agx_d3d12_r1_shader_span.h"

static unsigned checks, failures;
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_SHADER_SPAN_FAIL %s\n", name); }
}
int main() {
    const long raw_page = sysconf(_SC_PAGESIZE);
    if (raw_page < 4096) return 2;
    const auto page = static_cast<std::size_t>(raw_page);
    void *mapping = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapping == MAP_FAILED) return 3;
    if (mprotect(static_cast<unsigned char *>(mapping) + page, page, PROT_NONE)) return 4;
    auto *bytes = static_cast<unsigned char *>(mapping) + page - 8;
    UINT version = 0x00050060u, words = 0xffffffffu;
    std::memcpy(bytes, &version, 4); std::memcpy(bytes + 4, &words, 4);
    const auto *code = reinterpret_cast<const UINT *>(bytes);
    AgxD3d12ProgramSpan span{reinterpret_cast<const UINT *>(0x1234), 99, 99};
    check(!AgxD3d12AcquireR1ProgramSpan(code, 16u * 1024u * 1024u, &span),
          "huge declared length refused without reading inaccessible body");
    check(span.data == nullptr && span.bytes == 0 && span.words == 0,
          "failure clears output");
    words = 1; std::memcpy(bytes + 4, &words, 4);
    check(!AgxD3d12AcquireR1ProgramSpan(code, 1024, &span), "one word refused");
    words = 2; std::memcpy(bytes + 4, &words, 4);
    check(AgxD3d12AcquireR1ProgramSpan(code, 8, &span), "two-word boundary accepted");
    check(span.data == code && span.bytes == 8 && span.words == 2,
          "exact bounded PROGRAM span");
    check(!AgxD3d12AcquireR1ProgramSpan(code, 7, &span), "short policy refused");
    check(!AgxD3d12AcquireR1ProgramSpan(nullptr, 1024, &span), "null code refused");
    check(!AgxD3d12AcquireR1ProgramSpan(code, 1024, nullptr), "null output refused");
    check(munmap(mapping, page * 2) == 0, "mapping released");
    std::printf("R1_SHADER_SPAN_PASS checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
