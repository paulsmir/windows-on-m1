/* CPU root-private size preflight; host WDK type model, not Windows ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
using UINT = std::uint32_t;
using SIZE_T = std::size_t;
using D3D12DDI_ROOT_PARAMETER_TYPE = UINT;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE = 0;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS = 1;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_CBV = 2;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_SRV = 3;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_UAV = 4;
struct D3D12DDI_DESCRIPTOR_RANGE { UINT dummy; };
struct D3D12DDI_ROOT_DESCRIPTOR_TABLE { UINT NumDescriptorRanges; const D3D12DDI_DESCRIPTOR_RANGE *pDescriptorRanges; };
struct D3D12DDI_ROOT_CONSTANTS { UINT ShaderRegister, RegisterSpace, Num32BitValues; };
struct D3D12DDI_ROOT_DESCRIPTOR { UINT ShaderRegister, RegisterSpace; };
struct D3D12DDI_ROOT_PARAMETER {
    D3D12DDI_ROOT_PARAMETER_TYPE ParameterType;
    union { D3D12DDI_ROOT_DESCRIPTOR_TABLE DescriptorTable;
            D3D12DDI_ROOT_CONSTANTS Constants; D3D12DDI_ROOT_DESCRIPTOR Descriptor; };
    UINT ShaderVisibility;
};
struct D3D12DDI_STATIC_SAMPLER { UINT data[13]; };
struct D3D12DDI_ROOT_SIGNATURE {
    UINT NumParameters; const D3D12DDI_ROOT_PARAMETER *pRootParameters;
    UINT NumStaticSamplers; const D3D12DDI_STATIC_SAMPLER *pStaticSamplers;
    UINT Flags;
};
struct AgxD3d12RootParameter { UINT data[8]; };
struct AgxD3d12Range { UINT data[5]; };
struct AgxD3d12RootSignature {
    UINT parameters, static_samplers, flags, dwords, range_count;
    AgxD3d12RootParameter parameter[1];
};
#include "../drivers/apple-agx/d3d12/agx_d3d12_r1_root_layout.h"

static unsigned checks, failures;
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_ROOT_LAYOUT_FAIL %s\n", name); }
}
int main() {
    D3D12DDI_DESCRIPTOR_RANGE ranges[2]{};
    D3D12DDI_ROOT_PARAMETER params[2]{};
    params[0].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable={2,ranges};
    params[1].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants={1,2,3};
    D3D12DDI_STATIC_SAMPLER sampler{};
    D3D12DDI_ROOT_SIGNATURE root{2,params,1,&sampler,0};
    const AgxR1RootLayoutPolicy policy{64,166,2032,64};
    SIZE_T bytes=0; UINT count=0;
    check(AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),"valid root");
    check(count==2&&bytes==offsetof(AgxD3d12RootSignature,parameter)+
          2*sizeof(AgxD3d12RootParameter)+2*sizeof(AgxD3d12Range)+
          sizeof(D3D12DDI_STATIC_SAMPLER),"exact private extent");
    root.NumParameters=0;root.pRootParameters=nullptr;root.NumStaticSamplers=0;root.pStaticSamplers=nullptr;
    check(AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),"empty root");
    check(count==0&&bytes==offsetof(AgxD3d12RootSignature,parameter)+sizeof(AgxD3d12RootParameter),
          "empty root reserves one parameter");
    root.NumParameters=2;root.pRootParameters=params;root.NumStaticSamplers=1;root.pStaticSamplers=&sampler;
    check(!AgxD3d12MeasureR1RootPrivate(&root,{1,166,2032,64},&bytes,&count),"parameter policy before array");
    check(bytes==0&&count==0,"failure clears outputs");
    root.pRootParameters=nullptr;
    check(!AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),"null parameter array refused");
    root.pRootParameters=params;root.pStaticSamplers=nullptr;
    check(!AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),"null sampler array refused");
    root.pStaticSamplers=&sampler;
    params[0].DescriptorTable.pDescriptorRanges=nullptr;
    check(!AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),"null ranges refused");
    params[0].DescriptorTable.pDescriptorRanges=ranges;
    params[0].DescriptorTable.NumDescriptorRanges=0xffffffffu;
    check(!AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),"range policy before array");
    params[0].DescriptorTable.NumDescriptorRanges=2;
    params[1].Constants.Num32BitValues=64;
    check(!AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),"64 DWORD budget exceeded");
    params[1].Constants.Num32BitValues=3;
    params[1].ParameterType=static_cast<D3D12DDI_ROOT_PARAMETER_TYPE>(99);
    check(!AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),"unknown parameter type refused");
    params[1].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    const long raw_page=sysconf(_SC_PAGESIZE);
    if(raw_page<4096)return 2;
    const auto page=static_cast<SIZE_T>(raw_page);
    void *mapping=mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(mapping==MAP_FAILED)return 3;
    if(mprotect(static_cast<unsigned char *>(mapping)+page,page,PROT_NONE))return 4;
    root.NumParameters=0xffffffffu;
    root.pRootParameters=reinterpret_cast<const D3D12DDI_ROOT_PARAMETER *>(
        static_cast<unsigned char *>(mapping)+page);
    check(!AgxD3d12MeasureR1RootPrivate(&root,policy,&bytes,&count),
          "huge count rejected before PROT_NONE parameters");
    check(munmap(mapping,page*2)==0,"guard mapping released");
    std::printf("R1_ROOT_LAYOUT_PASS checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
