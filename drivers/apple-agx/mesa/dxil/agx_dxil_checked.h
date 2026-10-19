/* Original checked C boundary for the selected owned-adapter API. SPDX-License-Identifier: MIT */
#ifndef AGX_DXIL_CHECKED_H
#define AGX_DXIL_CHECKED_H
#include "dxil_spirv_c.h"
#ifdef __cplusplus
#define AGX_DXIL_NOEXCEPT noexcept
extern "C" {
#else
#define AGX_DXIL_NOEXCEPT
#endif
dxil_spv_result agx_checked_version(unsigned*,unsigned*,unsigned*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_begin(void) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_end(void) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_parse(const void*,size_t,dxil_spv_parsed_blob*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_blob_free(dxil_spv_parsed_blob) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_compute_profile(dxil_spv_parsed_blob,dxil_spv_compute_profile*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_stage(dxil_spv_parsed_blob,dxil_spv_shader_stage*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_entry_count(dxil_spv_parsed_blob,unsigned*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_entry_name(dxil_spv_parsed_blob,unsigned,const char**) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_scan(dxil_spv_parsed_blob,dxil_spv_srv_remapper_cb,dxil_spv_sampler_remapper_cb,dxil_spv_cbv_remapper_cb,dxil_spv_uav_remapper_cb,void*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_create(dxil_spv_parsed_blob,dxil_spv_converter*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_converter_free(dxil_spv_converter) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_entry(dxil_spv_converter,const char*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_srv(dxil_spv_converter,dxil_spv_srv_remapper_cb,void*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_sampler(dxil_spv_converter,dxil_spv_sampler_remapper_cb,void*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_cbv(dxil_spv_converter,dxil_spv_cbv_remapper_cb,void*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_uav(dxil_spv_converter,dxil_spv_uav_remapper_cb,void*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_run(dxil_spv_converter) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_spirv(dxil_spv_converter,dxil_spv_compiled_spirv*) AGX_DXIL_NOEXCEPT;
dxil_spv_result agx_checked_workgroup(dxil_spv_converter,unsigned*,unsigned*,unsigned*) AGX_DXIL_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef AGX_DXIL_NOEXCEPT
#endif
