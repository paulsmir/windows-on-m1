/* CPU root canonicalization fixture; type model, not Windows ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
using UINT = std::uint32_t;
using SIZE_T = std::size_t;
struct AgxD3d12RootParameter {
    UINT type, visibility, offset, dwords, shader_register, space, first_range, ranges;
};
struct AgxD3d12Range { UINT type, count, base_register, space, offset; };
struct AgxD3d12RootSignature {
    UINT parameters, static_samplers, flags, dwords, range_count;
    AgxD3d12RootParameter parameter[1];
};
struct D3D12DDI_STATIC_SAMPLER {
    int Filter, AddressU, AddressV, AddressW;
    float MipLODBias;
    UINT MaxAnisotropy;
    int ComparisonFunc, BorderColor;
    float MinLOD, MaxLOD;
    UINT ShaderRegister, RegisterSpace;
    int ShaderVisibility;
};
#include "../drivers/apple-agx/d3d12/agx_d3d12_r1_root_canonical.h"

static unsigned checks, failures;
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_ROOT_FAIL %s\n", name); }
}
constexpr SIZE_T kInput = offsetof(AgxD3d12RootSignature, parameter) +
    2 * sizeof(AgxD3d12RootParameter) + 2 * sizeof(AgxD3d12Range) +
    sizeof(D3D12DDI_STATIC_SAMPLER);
static void make_root(unsigned char *buffer, unsigned char fill) {
    std::memset(buffer, fill, kInput + 16);
    auto *root = reinterpret_cast<AgxD3d12RootSignature *>(buffer);
    root->parameters=2; root->static_samplers=1; root->flags=0x1234;
    root->dwords=5; root->range_count=2;
    root->parameter[0]={1,2,0,1,0,0,0,2};
    root->parameter[1]={3,4,1,4,7,8,0,0};
    auto *ranges=reinterpret_cast<AgxD3d12Range *>(buffer +
        offsetof(AgxD3d12RootSignature, parameter) + 2*sizeof(AgxD3d12RootParameter));
    ranges[0]={5,6,7,8,9}; ranges[1]={10,11,12,13,14};
    auto *sampler=reinterpret_cast<D3D12DDI_STATIC_SAMPLER *>(ranges+2);
    *sampler={1,2,3,4,1.25f,16,5,6,-0.5f,7.75f,9,10,11};
}
int main() {
    alignas(AgxD3d12RootSignature) unsigned char a[kInput+16],b[kInput+16];
    make_root(a,0xa5); make_root(b,0x5a);
    const auto *ra=reinterpret_cast<const AgxD3d12RootSignature *>(a);
    const auto *rb=reinterpret_cast<const AgxD3d12RootSignature *>(b);
    SIZE_T needed=0;
    check(AgxD3d12MeasureRootCanonical(ra,kInput,&needed),"measure valid root");
    check(needed==32+2*32+2*20+52,"exact canonical extent");
    struct Guarded { std::uint64_t pre; unsigned char bytes[256]; std::uint64_t post; };
    Guarded out_a{0x1234567812345678ull,{},0x8765432187654321ull};
    Guarded out_b{0x1234567812345678ull,{},0x8765432187654321ull};
    SIZE_T written=0;
    check(AgxD3d12EncodeRootCanonical(ra,kInput,out_a.bytes,sizeof(out_a.bytes),&written),"encode root A");
    check(written==needed&&out_a.pre==0x1234567812345678ull&&
          out_a.post==0x8765432187654321ull,"extent and canaries");
    check(AgxD3d12EncodeRootCanonical(rb,kInput,out_b.bytes,sizeof(out_b.bytes),&written),"encode root B");
    check(std::memcmp(out_a.bytes,out_b.bytes,needed)==0,"source fill and trailing garbage ignored");
    auto *ranges_b=reinterpret_cast<AgxD3d12Range *>(b+
        offsetof(AgxD3d12RootSignature,parameter)+2*sizeof(AgxD3d12RootParameter));
    ranges_b[1].offset++;
    check(AgxD3d12EncodeRootCanonical(rb,kInput,out_b.bytes,sizeof(out_b.bytes),&written),"changed range encodes");
    check(std::memcmp(out_a.bytes,out_b.bytes,needed)!=0,"range offset is semantic");
    ranges_b[1].offset--;
    auto *sampler_b=reinterpret_cast<D3D12DDI_STATIC_SAMPLER *>(ranges_b+2);
    sampler_b->MipLODBias=-0.0f;
    check(AgxD3d12EncodeRootCanonical(rb,kInput,out_b.bytes,sizeof(out_b.bytes),&written),"changed float encodes");
    check(std::memcmp(out_a.bytes,out_b.bytes,needed)!=0,"sampler float bits are semantic");
    const Guarded snapshot=out_a;
    check(!AgxD3d12EncodeRootCanonical(ra,kInput,out_a.bytes,needed-1,&written),"short output refused");
    check(written==0&&std::memcmp(&snapshot,&out_a,sizeof(snapshot))==0,"short output writes nothing");
    check(!AgxD3d12EncodeRootCanonical(ra,kInput,a,needed,&written),"source/destination alias refused");
    check(!AgxD3d12MeasureRootCanonical(ra,offsetof(AgxD3d12RootSignature,parameter),&needed),"truncated storage refused");
    const long raw_page=sysconf(_SC_PAGESIZE);
    if(raw_page<4096)return 2;
    const auto page=static_cast<SIZE_T>(raw_page);
    void *mapping=mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(mapping==MAP_FAILED)return 3;
    if(mprotect(static_cast<unsigned char *>(mapping)+page,page,PROT_NONE))return 4;
    unsigned char *end=static_cast<unsigned char *>(mapping)+page-
        offsetof(AgxD3d12RootSignature,parameter);
    auto *truncated=reinterpret_cast<AgxD3d12RootSignature *>(end);
    truncated->parameters=2;truncated->static_samplers=1;truncated->flags=0;
    truncated->dwords=5;truncated->range_count=2;
    check(!AgxD3d12MeasureRootCanonical(truncated,offsetof(AgxD3d12RootSignature,parameter),&needed),
          "guard-page truncated arrays refused before read");
    check(munmap(mapping,page*2)==0,"guard mapping released");
    std::printf("R1_ROOT_CANONICAL_PASS checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
