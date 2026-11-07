/* Actual DDI root Calc/Create preflight; host WDK model, not Windows ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include <limits>
#include <vector>
#include <cstdlib>
using UINT = std::uint32_t;
using SIZE_T = std::size_t;
using HRESULT = std::int32_t;
using VOID = void;
#define APIENTRY
#define S_OK static_cast<HRESULT>(0)
#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)
#define E_OUTOFMEMORY static_cast<HRESULT>(0x8007000eu)
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HROOTSIGNATURE { void *pDrvPrivate; };
using D3D12DDI_ROOT_PARAMETER_TYPE = UINT;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE=0;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS=1;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_CBV=2;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_SRV=3;
constexpr UINT D3D12DDI_ROOT_PARAMETER_TYPE_UAV=4;
constexpr UINT D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND=UINT_MAX;
struct D3D12DDI_DESCRIPTOR_RANGE {
    UINT RangeType, NumDescriptors, BaseShaderRegister, RegisterSpace,
         OffsetInDescriptorsFromTableStart;
};
struct D3D12DDI_ROOT_DESCRIPTOR_TABLE {
    UINT NumDescriptorRanges; const D3D12DDI_DESCRIPTOR_RANGE *pDescriptorRanges;
};
struct D3D12DDI_ROOT_CONSTANTS { UINT ShaderRegister, RegisterSpace, Num32BitValues; };
struct D3D12DDI_ROOT_DESCRIPTOR { UINT ShaderRegister, RegisterSpace; };
struct D3D12DDI_ROOT_PARAMETER {
    D3D12DDI_ROOT_PARAMETER_TYPE ParameterType;
    union { D3D12DDI_ROOT_DESCRIPTOR_TABLE DescriptorTable;
            D3D12DDI_ROOT_CONSTANTS Constants; D3D12DDI_ROOT_DESCRIPTOR Descriptor; };
    UINT ShaderVisibility;
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
struct D3D12DDI_ROOT_SIGNATURE {
    UINT NumParameters; const D3D12DDI_ROOT_PARAMETER *pRootParameters;
    UINT NumStaticSamplers; const D3D12DDI_STATIC_SAMPLER *pStaticSamplers;
    UINT Flags;
};
struct D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 {
    const D3D12DDI_ROOT_SIGNATURE *pRootSignature; UINT NodeMask;
};
static void AdmissionUmdDiagnostic(const char *, HRESULT, const UINT *, UINT) {}
struct AgxCpuProvider {
    std::vector<unsigned char> canonical;
    bool registered=false, retired=false, fail_registration=false;
};
struct AgxCpuDevice { std::uint64_t value; };
struct AgxCpuRoot { std::uint64_t value; };
struct AgxShaderTrustedSpan { const void *data; SIZE_T bytes; };
enum class AgxCpuProviderStatus { Ok, Invalid, ProjectOOM, Exhausted };
struct AgxD3d12Device { AgxCpuProvider *shader_provider; AgxCpuDevice shader_provider_device; };
static AgxCpuProviderStatus AgxCpuRegisterRoot(AgxCpuProvider *provider,AgxCpuDevice device,
    D3D12DDI_HROOTSIGNATURE root,AgxShaderTrustedSpan bytes,AgxCpuRoot *out) {
    if (!provider || !device.value || !root.pDrvPrivate || !out || provider->registered)
        return AgxCpuProviderStatus::Invalid;
    if (provider->fail_registration) return AgxCpuProviderStatus::ProjectOOM;
    const auto *first=static_cast<const unsigned char *>(bytes.data);
    provider->canonical.assign(first,first+bytes.bytes);
    provider->registered=true;*out={73};return AgxCpuProviderStatus::Ok;
}
static AgxCpuProviderStatus AgxCpuRetireRoot(AgxCpuProvider *provider,AgxCpuDevice device,AgxCpuRoot root) {
    if (!provider || !device.value || root.value!=73 || !provider->registered)
        return AgxCpuProviderStatus::Invalid;
    provider->retired=true;provider->registered=false;return AgxCpuProviderStatus::Ok;
}

/* ACTUAL_ROOT_PRIVATE_TYPES */
#include "agx_d3d12_r1_root_layout.h"
#include "agx_d3d12_r1_root_canonical.h"
/* ACTUAL_ROOT_CALLBACKS */

static unsigned checks, failures;
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_ROOT_PREFLIGHT_FAIL %s\n", name); }
}
int main() {
    D3D12DDI_DESCRIPTOR_RANGE range{1,2,3,4,D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND};
    D3D12DDI_ROOT_PARAMETER params[2]{};
    params[0].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable={1,&range};params[0].ShaderVisibility=1;
    params[1].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants={5,6,3};params[1].ShaderVisibility=2;
    D3D12DDI_STATIC_SAMPLER sampler{};
    D3D12DDI_ROOT_SIGNATURE desc{2,params,1,&sampler,0x31};
    D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 args{&desc,0};
    constexpr SIZE_T expected=offsetof(AgxD3d12RootSignature,parameter)+
        2*sizeof(AgxD3d12RootParameter)+sizeof(AgxD3d12Range)+
        sizeof(D3D12DDI_STATIC_SAMPLER);
    check(CalcPrivateRootSignatureSize({},&args)==expected,"valid Calc exact extent");
    struct Guarded { std::uint64_t pre; alignas(AgxD3d12RootSignature) unsigned char data[256]; std::uint64_t post; };
    Guarded output{0x1234567812345678ull,{},0x8765432187654321ull};
    D3D12DDI_HROOTSIGNATURE destination{output.data};
    AgxCpuProvider provider{};
    AgxD3d12Device device{&provider,{11}};
    D3D12DDI_HDEVICE device_handle{&device};
    check(CreateRootSignature(device_handle,&args,destination)==S_OK,"valid Create");
    auto *root=reinterpret_cast<AgxD3d12RootSignature *>(output.data);
    check(root->parameters==2&&root->range_count==1&&root->static_samplers==1&&
          root->dwords==4&&root->flags==0x31,"valid fields and DWORD cost");
    check(RangesOf(root)[0].offset==0&&RangesOf(root)[0].count==2,"resolved APPEND range");
    check(output.pre==0x1234567812345678ull&&output.post==0x8765432187654321ull,"canaries");
    SIZE_T canonical_bytes=0;
    check(provider.registered&&root->provider_root.value==73&&
          AgxD3d12MeasureRootCanonical(root,expected,&canonical_bytes)&&
          provider.canonical.size()==canonical_bytes&&
          std::memcmp(provider.canonical.data(),"AGXR1RT1",8)==0,"provider owns canonical root");
    check(CalcPrivateRootSignatureSize({},nullptr)==0,"null Calc refused");
    const Guarded before=output;
    params[1].ParameterType=99;
    check(CalcPrivateRootSignatureSize({},&args)==0,"unknown type Calc refused");
    check(CreateRootSignature(device_handle,&args,destination)==E_INVALIDARG,"unknown type Create refused");
    check(std::memcmp(&before,&output,sizeof(output))==0,"invalid Create writes nothing");
    params[1].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues=64;
    check(CalcPrivateRootSignatureSize({},&args)==0,"over-budget Calc refused");
    check(CreateRootSignature(device_handle,&args,destination)==E_INVALIDARG,"over-budget Create refused");
    params[1].Constants.Num32BitValues=3;
    const D3D12DDI_DESCRIPTOR_RANGE original_range=range;
    range.NumDescriptors=4;
    range.OffsetInDescriptorsFromTableStart=UINT_MAX-2;
    check(CalcPrivateRootSignatureSize({},&args)==0,"explicit range end overflow Calc refused");
    check(CreateRootSignature(device_handle,&args,destination)==E_INVALIDARG,"explicit range end overflow Create refused");
    check(std::memcmp(&before,&output,sizeof(output))==0,"overflow Create writes nothing");
    D3D12DDI_DESCRIPTOR_RANGE pair[2]={{1,UINT_MAX,3,4,0},{1,1,4,4,D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND}};
    params[0].DescriptorTable={2,pair};
    check(CalcPrivateRootSignatureSize({},&args)==0,"APPEND after unbounded Calc refused");
    check(CreateRootSignature(device_handle,&args,destination)==E_INVALIDARG,"APPEND after unbounded Create refused");
    pair[1].OffsetInDescriptorsFromTableStart=7;
    check(CalcPrivateRootSignatureSize({},&args)>0,"explicit range after unbounded accepted");
    AgxCpuProvider next_provider{};
    device.shader_provider=&next_provider;
    check(CreateRootSignature(device_handle,&args,destination)==S_OK,"explicit range after unbounded Create accepted");
    check(RangesOf(root)[1].offset==7,"explicit range after unbounded preserved");
    params[0].DescriptorTable={1,&range};
    range=original_range;
    desc.pRootParameters=nullptr;
    check(CalcPrivateRootSignatureSize({},&args)==0,"missing parameter array Calc refused");
    check(CreateRootSignature(device_handle,&args,destination)==E_INVALIDARG,"missing parameter array Create refused");
    DestroyRootSignature(device_handle,destination);
    check(next_provider.retired&&!root->provider_root.value,"Destroy retires provider root");
    desc.pRootParameters=params;
    AgxCpuProvider fail_provider{};
    fail_provider.fail_registration=true;
    device.shader_provider=&fail_provider;
    check(CreateRootSignature(device_handle,&args,destination)==E_OUTOFMEMORY,
          "provider allocation failure maps to OOM");
    check(!root->provider_root.value&&!fail_provider.registered,
          "failed registration publishes no root token");
    std::printf("R1_ROOT_PREFLIGHT_PASS checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
