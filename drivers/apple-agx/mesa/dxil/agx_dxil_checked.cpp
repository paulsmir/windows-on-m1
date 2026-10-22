/* Original exception boundary; no upstream implementation copied here. SPDX-License-Identifier: MIT */
#include "agx_dxil_checked.h"
#include <new>
#include <stdexcept>
namespace {
template<class F> dxil_spv_result checked(F f) noexcept {
 try { return f(); }
 catch(const std::bad_alloc&) { return DXIL_SPV_ERROR_OUT_OF_MEMORY; }
 catch(const std::length_error&) { return DXIL_SPV_ERROR_INVALID_ARGUMENT; }
 catch(...) { return DXIL_SPV_ERROR_GENERIC; }
}
}
extern "C" dxil_spv_result agx_checked_version(unsigned*a,unsigned*b,unsigned*c) noexcept {
 if(a)*a=0;if(b)*b=0;if(c)*c=0;if(!a||!b||!c)return DXIL_SPV_ERROR_INVALID_ARGUMENT;
 return checked([&]{dxil_spv_get_version(a,b,c);return DXIL_SPV_SUCCESS;});
}
extern "C" dxil_spv_result agx_checked_begin() noexcept {return checked([]{dxil_spv_begin_thread_allocator_context();return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_end() noexcept {return checked([]{dxil_spv_end_thread_allocator_context();return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_parse(const void*p,size_t n,dxil_spv_parsed_blob*out) noexcept {
 if(!out)return DXIL_SPV_ERROR_INVALID_ARGUMENT;*out=nullptr;
 dxil_spv_result r=checked([&]{return dxil_spv_parse_dxil(p,n,out);});if(r!=DXIL_SPV_SUCCESS)*out=nullptr;return r;
}
extern "C" dxil_spv_result agx_checked_blob_free(dxil_spv_parsed_blob p) noexcept {return checked([&]{dxil_spv_parsed_blob_free(p);return DXIL_SPV_SUCCESS;});}
#ifdef AGX_DXIL_SPV_COMPUTE_PROFILE
extern "C" dxil_spv_result agx_checked_compute_profile(dxil_spv_parsed_blob p,dxil_spv_compute_profile*out) noexcept {
 if(!out)return DXIL_SPV_ERROR_INVALID_ARGUMENT;*out={};if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;
 const auto r=checked([&]{return dxil_spv_parsed_blob_get_compute_profile(p,out);});
 if(r!=DXIL_SPV_SUCCESS)*out={};return r;
}
#endif
extern "C" dxil_spv_result agx_checked_stage(dxil_spv_parsed_blob p,dxil_spv_shader_stage*out) noexcept {
 if(!out)return DXIL_SPV_ERROR_INVALID_ARGUMENT;*out=DXIL_SPV_STAGE_UNKNOWN;if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;
 dxil_spv_result r=checked([&]{*out=dxil_spv_parsed_blob_get_shader_stage(p);return DXIL_SPV_SUCCESS;});if(r!=DXIL_SPV_SUCCESS)*out=DXIL_SPV_STAGE_UNKNOWN;return r;
}
extern "C" dxil_spv_result agx_checked_entry_count(dxil_spv_parsed_blob p,unsigned*out) noexcept {
 if(!out)return DXIL_SPV_ERROR_INVALID_ARGUMENT;*out=0;if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;
 dxil_spv_result r=checked([&]{return dxil_spv_parsed_blob_get_num_entry_points(p,out);});if(r!=DXIL_SPV_SUCCESS)*out=0;return r;
}
extern "C" dxil_spv_result agx_checked_entry_name(dxil_spv_parsed_blob p,unsigned i,const char**out) noexcept {
 if(!out)return DXIL_SPV_ERROR_INVALID_ARGUMENT;*out=nullptr;if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;
 dxil_spv_result r=checked([&]{return dxil_spv_parsed_blob_get_entry_point_name(p,i,out);});if(r!=DXIL_SPV_SUCCESS)*out=nullptr;return r;
}
extern "C" dxil_spv_result agx_checked_scan(dxil_spv_parsed_blob p,dxil_spv_srv_remapper_cb a,dxil_spv_sampler_remapper_cb b,dxil_spv_cbv_remapper_cb c,dxil_spv_uav_remapper_cb d,void*u) noexcept {
 if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{return dxil_spv_parsed_blob_scan_resources(p,a,b,c,d,u);});
}
extern "C" dxil_spv_result agx_checked_create(dxil_spv_parsed_blob p,dxil_spv_converter*out) noexcept {
 if(!out)return DXIL_SPV_ERROR_INVALID_ARGUMENT;*out=nullptr;if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;
 dxil_spv_result r=checked([&]{return dxil_spv_create_converter(p,out);});if(r!=DXIL_SPV_SUCCESS)*out=nullptr;return r;
}
extern "C" dxil_spv_result agx_checked_converter_free(dxil_spv_converter p) noexcept {return checked([&]{dxil_spv_converter_free(p);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_entry(dxil_spv_converter p,const char*s) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{dxil_spv_converter_set_entry_point(p,s);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_srv(dxil_spv_converter p,dxil_spv_srv_remapper_cb f,void*u) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{dxil_spv_converter_set_srv_remapper(p,f,u);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_sampler(dxil_spv_converter p,dxil_spv_sampler_remapper_cb f,void*u) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{dxil_spv_converter_set_sampler_remapper(p,f,u);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_cbv(dxil_spv_converter p,dxil_spv_cbv_remapper_cb f,void*u) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{dxil_spv_converter_set_cbv_remapper(p,f,u);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_uav(dxil_spv_converter p,dxil_spv_uav_remapper_cb f,void*u) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{dxil_spv_converter_set_uav_remapper(p,f,u);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_run(dxil_spv_converter p) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{return dxil_spv_converter_run(p);});}
extern "C" dxil_spv_result agx_checked_spirv(dxil_spv_converter p,dxil_spv_compiled_spirv*out) noexcept {
 if(!out)return DXIL_SPV_ERROR_INVALID_ARGUMENT;*out={};if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;
 dxil_spv_result r=checked([&]{return dxil_spv_converter_get_compiled_spirv(p,out);});if(r!=DXIL_SPV_SUCCESS)*out={};return r;
}
extern "C" dxil_spv_result agx_checked_workgroup(dxil_spv_converter p,unsigned*x,unsigned*y,unsigned*z) noexcept {
 if(x)*x=0;if(y)*y=0;if(z)*z=0;if(!p||!x||!y||!z)return DXIL_SPV_ERROR_INVALID_ARGUMENT;
 dxil_spv_result r=checked([&]{return dxil_spv_converter_get_compute_workgroup_dimensions(p,x,y,z);});if(r!=DXIL_SPV_SUCCESS){*x=*y=*z=0;}return r;
}
extern "C" dxil_spv_result agx_checked_parse_blob(const void*p,size_t n,dxil_spv_parsed_blob*out) noexcept {
 if(!out)return DXIL_SPV_ERROR_INVALID_ARGUMENT;*out=nullptr;
 dxil_spv_result r=checked([&]{return dxil_spv_parse_dxil_blob(p,n,out);});if(r!=DXIL_SPV_SUCCESS)*out=nullptr;return r;
}
extern "C" dxil_spv_result agx_checked_stage_input(dxil_spv_converter p,dxil_spv_shader_stage_io_remapper_cb f,void*u) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{dxil_spv_converter_set_stage_input_remapper(p,f,u);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_stage_output(dxil_spv_converter p,dxil_spv_shader_stage_io_remapper_cb f,void*u) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{dxil_spv_converter_set_stage_output_remapper(p,f,u);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_vertex_input(dxil_spv_converter p,dxil_spv_vertex_input_remapper_cb f,void*u) noexcept {if(!p)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{dxil_spv_converter_set_vertex_input_remapper(p,f,u);return DXIL_SPV_SUCCESS;});}
extern "C" dxil_spv_result agx_checked_option(dxil_spv_converter p,const dxil_spv_option_base*o) noexcept {if(!p||!o)return DXIL_SPV_ERROR_INVALID_ARGUMENT;return checked([&]{return dxil_spv_converter_add_option(p,o);});}
