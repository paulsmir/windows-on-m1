/* Original actual-SDK record/helper ABI assertions; no DDI adapter is claimed. */
#include <windows.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d12umddi.h>
#pragma warning(pop)
#include <type_traits>
#include <cstddef>
#include "agx_d3d12_shader_owned_core.h"
static_assert(sizeof(D3D12DDI_LIBRARY_REFERENCE_0010)==16,"reference extent");
static_assert(sizeof(D3D12DDIARG_CREATE_SHADER_0010)==48,"R1 shader extent");
static_assert(offsetof(D3D12DDIARG_CREATE_SHADER_0010,hRootSignature)==0,"root");
static_assert(offsetof(D3D12DDIARG_CREATE_SHADER_0010,pShaderCode)==8,"code");
static_assert(offsetof(D3D12DDIARG_CREATE_SHADER_0010,IOSignatures)==16,"union");
static_assert(offsetof(D3D12DDIARG_CREATE_SHADER_0010,Flags)==24,"flags");
static_assert(offsetof(D3D12DDIARG_CREATE_SHADER_0010,LibraryReference)==32,"library");
static_assert(sizeof(D3D12DDIARG_SIGNATURE_ENTRY)==20,"legacy signature extent");
static_assert(sizeof(D3D12DDIARG_STAGE_IO_SIGNATURES)==32,"signature descriptor extent");
using Calc=AgxShaderStorageStatus(*)(D3D12DDI_HDEVICE,const D3D12DDIARG_CREATE_SHADER_0010 *,uint32_t,const AgxShaderTrustedInput *,AgxShaderOwnedLayout *);
using Create=AgxShaderStorageStatus(*)(D3D12DDI_HDEVICE,const D3D12DDIARG_CREATE_SHADER_0010 *,uint32_t,const AgxShaderTrustedInput *,void *,SIZE_T,AgxShaderOwned **);
static_assert(std::is_same<decltype(&AgxShaderOwnedCalculate),Calc>::value,"internal Calc exact typed input");
static_assert(std::is_same<decltype(&AgxShaderOwnedCreate),Create>::value,"internal Create exact typed input");
// Internal helpers intentionally do NOT assert equality to VOID DDI PFNs.

static_assert(sizeof(AgxShaderOwned)==88,"preserve existing ARM64 owned record extent");
static_assert(offsetof(AgxShaderOwned,magic)==0,"private owned format/kind tag");
static_assert(offsetof(AgxShaderOwned,stage)==8,"preserve stage field");
static_assert(offsetof(AgxShaderOwned,flags)==12,"flags not overloaded by program kind");
static_assert(offsetof(AgxShaderOwned,layout)==16,"preserve layout field");
using Kind=AgxShaderProgramKind(*)(const AgxShaderOwned *);
static_assert(std::is_same<decltype(&AgxShaderOwnedGetProgramKind),Kind>::value,"kind getter exact return type");
