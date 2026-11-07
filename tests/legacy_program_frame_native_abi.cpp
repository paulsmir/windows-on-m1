/* Original actual MSVC C/C++ header compatibility gate; no runtime/link. */
#include <cstddef>
#include <type_traits>
#include "agx_legacy_program_frame.h"
using Frame=agx_legacy_frame_status(*)(const void *,size_t,const agx_legacy_frame_profile *,struct agx_legacy_program_frame *);
static_assert(std::is_same<decltype(&agx_legacy_frame_program),Frame>::value,"actual C export signature");
static_assert(sizeof(size_t)==8,"ARM64 size_t");
static_assert(sizeof(agx_legacy_program_frame)==24,"ARM64 result extent");
static_assert(offsetof(agx_legacy_program_frame,program_bytes)==0,"extent first");
static_assert(offsetof(agx_legacy_program_frame,words)==8,"words");
static_assert(offsetof(agx_legacy_program_frame,version)==12,"version");
static_assert(offsetof(agx_legacy_program_frame,stage)==16,"stage");
static_assert(offsetof(agx_legacy_program_frame,major)==20,"major");
static_assert(offsetof(agx_legacy_program_frame,minor)==21,"minor");
