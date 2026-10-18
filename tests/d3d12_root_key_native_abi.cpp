/* Original actual-WDK internal API consumer; no DDI cast/link/execution. MIT. */
#include <windows.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d12umddi.h>
#pragma warning(pop)
#include <type_traits>
#include <cstddef>
#include "agx_root_description_key.h"
static_assert(sizeof(FLOAT)==4&&sizeof(uint32_t)==4,"raw32 words");
static_assert(sizeof(D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001)==16,"actual0001 top args");
static_assert(sizeof(D3D12DDI_ROOT_SIGNATURE)==40,"actual0001 description");
static_assert(sizeof(D3D12DDI_ROOT_PARAMETER)==32,"actual parameter");
static_assert(sizeof(D3D12DDI_DESCRIPTOR_RANGE)==20,"actual range");
static_assert(sizeof(D3D12DDI_STATIC_SAMPLER)==52,"actual sampler");
static_assert(sizeof(AgxRootKeySpan)==16&&offsetof(AgxRootKeySpan,bytes)==8,"typed explicit span");
static_assert(sizeof(AgxRootKeyInput)==80&&offsetof(AgxRootKeyInput,table_spans)==64,"input five spans");
static_assert(sizeof(AgxRootKeyView)==16&&offsetof(AgxRootKeyView,bytes)==8,"published view");
using Size=AgxRootKeyStatus(*)(const AgxRootKeyInput*,SIZE_T*);
using Encode=AgxRootKeyStatus(*)(const AgxRootKeyInput*,void*,SIZE_T,AgxRootKeyView*);
static_assert(std::is_same<decltype(&AgxRootKeyCalculate),Size>::value,"internal preflight helper");
static_assert(std::is_same<decltype(&AgxRootKeyEncode),Encode>::value,"internal exact-field encoder");
static_assert(std::is_trivially_copyable<AgxRootKeyInput>::value&&std::is_standard_layout<AgxRootKeyView>::value,"value records");
// Output bytes are original schema, not sizeof(WDKstruct) or Windows admission.
