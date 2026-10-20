/* Actual DDI root Calc/Create preflight; host WDK model, not Windows ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include <limits>
using UINT = std::uint32_t;
using SIZE_T = std::size_t;
using HRESULT = std::int32_t;
using VOID = void;
#define APIENTRY
#define S_OK static_cast<HRESULT>(0)
#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)
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
struct D3D12DDI_STATIC_SAMPLER { UINT fields[13]; };
struct D3D12DDI_ROOT_SIGNATURE {
    UINT NumParameters; const D3D12DDI_ROOT_PARAMETER *pRootParameters;
    UINT NumStaticSamplers; const D3D12DDI_STATIC_SAMPLER *pStaticSamplers;
    UINT Flags;
};
struct D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 {
    const D3D12DDI_ROOT_SIGNATURE *pRootSignature; UINT NodeMask;
};
static void AdmissionUmdDiagnostic(const char *, HRESULT, const UINT *, UINT) {}

/* ACTUAL_ROOT_PRIVATE_TYPES */
#include "agx_d3d12_r1_root_layout.h"
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
    check(CreateRootSignature({},&args,destination)==S_OK,"valid Create");
    auto *root=reinterpret_cast<AgxD3d12RootSignature *>(output.data);
    check(root->parameters==2&&root->range_count==1&&root->static_samplers==1&&
          root->dwords==4&&root->flags==0x31,"valid fields and DWORD cost");
    check(RangesOf(root)[0].offset==0&&RangesOf(root)[0].count==2,"resolved APPEND range");
    check(output.pre==0x1234567812345678ull&&output.post==0x8765432187654321ull,"canaries");
    check(CalcPrivateRootSignatureSize({},nullptr)==0,"null Calc refused");
    const Guarded before=output;
    params[1].ParameterType=99;
    check(CalcPrivateRootSignatureSize({},&args)==0,"unknown type Calc refused");
    check(CreateRootSignature({},&args,destination)==E_INVALIDARG,"unknown type Create refused");
    check(std::memcmp(&before,&output,sizeof(output))==0,"invalid Create writes nothing");
    params[1].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues=64;
    check(CalcPrivateRootSignatureSize({},&args)==0,"over-budget Calc refused");
    check(CreateRootSignature({},&args,destination)==E_INVALIDARG,"over-budget Create refused");
    params[1].Constants.Num32BitValues=3;
    const D3D12DDI_DESCRIPTOR_RANGE original_range=range;
    range.NumDescriptors=4;
    range.OffsetInDescriptorsFromTableStart=UINT_MAX-2;
    check(CalcPrivateRootSignatureSize({},&args)==0,"explicit range end overflow Calc refused");
    check(CreateRootSignature({},&args,destination)==E_INVALIDARG,"explicit range end overflow Create refused");
    check(std::memcmp(&before,&output,sizeof(output))==0,"overflow Create writes nothing");
    D3D12DDI_DESCRIPTOR_RANGE pair[2]={{1,UINT_MAX,3,4,0},{1,1,4,4,D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND}};
    params[0].DescriptorTable={2,pair};
    check(CalcPrivateRootSignatureSize({},&args)==0,"APPEND after unbounded Calc refused");
    check(CreateRootSignature({},&args,destination)==E_INVALIDARG,"APPEND after unbounded Create refused");
    pair[1].OffsetInDescriptorsFromTableStart=7;
    check(CalcPrivateRootSignatureSize({},&args)>0,"explicit range after unbounded accepted");
    check(CreateRootSignature({},&args,destination)==S_OK,"explicit range after unbounded Create accepted");
    check(RangesOf(root)[1].offset==7,"explicit range after unbounded preserved");
    params[0].DescriptorTable={1,&range};
    range=original_range;
    desc.pRootParameters=nullptr;
    check(CalcPrivateRootSignatureSize({},&args)==0,"missing parameter array Calc refused");
    check(CreateRootSignature({},&args,destination)==E_INVALIDARG,"missing parameter array Create refused");
    std::printf("R1_ROOT_PREFLIGHT_PASS checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
