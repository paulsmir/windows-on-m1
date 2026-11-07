/* Original actual-SDK internal API consumer. Object only, NOT DDI callback ABI. */
#include <windows.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d12umddi.h>
#pragma warning(pop)
#include <type_traits>
#include <cstddef>
#include "agx_shader_input_provider.h"
static_assert(sizeof(AgxCpuDevice)==8&&sizeof(AgxCpuRoot)==8&&sizeof(AgxCpuInputLease)==8,"opaque IDs");
static_assert(sizeof(AgxCpuProviderResult)==12,"internal status record ARM64");
static_assert(offsetof(AgxCpuProviderResult,status)==0&&offsetof(AgxCpuProviderResult,core_status)==4&&offsetof(AgxCpuProviderResult,core_called)==8,"exact internal result fields");
static_assert(std::is_trivially_copyable<AgxCpuProviderResult>::value,"value status");
using Register=AgxCpuProviderStatus(*)(AgxCpuProvider*,D3D12DDI_HDEVICE,AgxCpuFailureSink,void*,AgxCpuDevice*);
using Root=AgxCpuProviderStatus(*)(AgxCpuProvider*,AgxCpuDevice,D3D12DDI_HROOTSIGNATURE,AgxShaderTrustedSpan,AgxCpuRoot*);
using Acquire=AgxCpuProviderResult(*)(AgxCpuProvider*,AgxCpuDevice,AgxCpuRoot,const D3D12DDIARG_CREATE_SHADER_0010*,uint32_t,AgxShaderTrustedSpan,AgxShaderTrustedSpan,AgxShaderTrustedSpan,AgxCpuInputLease*);
using Calculate=AgxCpuProviderResult(*)(AgxCpuProvider*,AgxCpuInputLease,AgxShaderOwnedLayout*);
using Copy=AgxCpuProviderResult(*)(AgxCpuProvider*,AgxCpuInputLease,void*,SIZE_T,AgxShaderOwned**);
using Release=AgxCpuProviderStatus(*)(AgxCpuProvider*,AgxCpuInputLease);
using Identity=const void*(*)(AgxCpuProvider*,D3D12DDI_HDEVICE);
using Sink=void(*)(void*,AgxCpuDevice,AgxCpuProviderResult)noexcept;
static_assert(std::is_same<decltype(&AgxCpuRegisterDevice),Register>::value,"typed device");
static_assert(std::is_same<decltype(&AgxCpuRegisterRoot),Root>::value,"typed root");
static_assert(std::is_same<decltype(&AgxCpuAcquireInput),Acquire>::value,"typed explicit-span lease");
static_assert(std::is_same<decltype(&AgxCpuCalculate),Calculate>::value,"internal Calculate");
static_assert(std::is_same<decltype(&AgxCpuCopyOwned),Copy>::value,"internal copy");
static_assert(std::is_same<decltype(&AgxCpuReleaseInput),Release>::value,"lease release");
static_assert(std::is_same<decltype(&AgxCpuResolveDeviceIdentity),Identity>::value,"identity observer");
static_assert(std::is_same<AgxCpuFailureSink,Sink>::value,"sink stays noexcept CPU-only");
// Deliberately no equality/cast to a VOID-DDI function pointer.
