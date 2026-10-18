/* Original compile-only actual-WDK API consumer. SPDX-License-Identifier: MIT */
#include <windows.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d12umddi.h>
#pragma warning(pop)
#include "agx_shader_input_provider.h"
#include <type_traits>
static_assert(std::is_same<decltype(&AgxCpuBeginOperation),AgxCpuProviderResult(*)(AgxCpuProvider*,AgxCpuInputLease,AgxCpuOperation*)>::value,"Begin actual API");
static_assert(std::is_same<decltype(&AgxCpuCalculatePinned),AgxCpuProviderResult(*)(AgxCpuProvider*,AgxCpuOperation,AgxShaderOwnedLayout*)>::value,"Calculate actual API");
static_assert(std::is_same<decltype(&AgxCpuCopyPinned),AgxCpuProviderResult(*)(AgxCpuProvider*,AgxCpuOperation,void*,SIZE_T,AgxShaderOwned**)>::value,"Copy actual API");
static_assert(std::is_same<decltype(&AgxCpuEndOperation),AgxCpuProviderStatus(*)(AgxCpuProvider*,AgxCpuOperation*)>::value,"End actual API");
static_assert(std::is_same<decltype(&AgxCpuResolvePinnedDeviceIdentity),const void*(*)(AgxCpuProvider*,D3D12DDI_HDEVICE)>::value,"Pinned resolver");
static_assert(std::is_same<decltype(&AgxCpuBeginDeviceScope),AgxCpuProviderStatus(*)(AgxCpuProvider*,AgxCpuDevice,AgxCpuDeviceScope*)>::value,"Scope Begin");
static_assert(std::is_same<decltype(&AgxCpuEndDeviceScope),AgxCpuProviderStatus(*)(AgxCpuProvider*,AgxCpuDeviceScope*)>::value,"Scope End");
static_assert(std::is_same<decltype(&AgxCpuBeginQuiesce),AgxCpuProviderStatus(*)(AgxCpuProvider*,AgxCpuDevice,AgxCpuQuiesceTicket*)>::value,"Quiesce Begin");
static_assert(std::is_same<decltype(&AgxCpuTryQuiesce),AgxCpuProviderStatus(*)(AgxCpuProvider*,AgxCpuQuiesceTicket*)>::value,"Quiesce Try");
