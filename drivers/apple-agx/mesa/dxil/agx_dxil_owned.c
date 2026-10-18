/* Original standalone owned converter checkpoint. SPDX-License-Identifier: MIT
 * No DDI integration; recoverable internal translator OOM is NOT guaranteed.
 * Source/ownership contract: D3D12-C15-owned-converter-contract.md. */
#include "agx_dxil_owned.h"
#include "dxil_spirv_c.h"
#include <stdlib.h>
#include <string.h>

static _Thread_local unsigned in_progress;
struct conversion_job {
   struct dxbc_binding_map map;
   uint32_t limits[DXBC_BINDING_KIND_COUNT];
   unsigned rejected;
};

static dxil_spv_bool collect(struct conversion_job *job, uint32_t kind,
   const dxil_spv_d3d_binding *b, unsigned counter)
{
   /* Scan SUCCESS does not propagate callback rejection. Always finish scanning
    * with a sticky rejection flag, never allocating a slot in callback order. */
   if (!b || kind != DXBC_BINDING_UAV || b->stage != DXIL_SPV_STAGE_COMPUTE ||
       b->kind != DXIL_SPV_RESOURCE_KIND_RAW_BUFFER || b->range_size != 1 || counter ||
       job->map.count >= DXBC_BINDING_MAP_MAX) {
      job->rejected = 1;
      return DXIL_SPV_TRUE;
   }
   for (unsigned i = 0; i < job->map.count; ++i) {
      const struct dxbc_binding *old = &job->map.bindings[i];
      if (old->kind == kind && (old->id == b->resource_index ||
          (old->space == b->register_space && old->reg == b->register_index))) {
         job->rejected = 1;
         return DXIL_SPV_TRUE;
      }
   }
   struct dxbc_binding entry = {kind, b->resource_index, b->register_space, b->register_index, 0};
   job->map.bindings[job->map.count++] = entry;
   return DXIL_SPV_TRUE;
}
static dxil_spv_bool collect_srv(void *u,const dxil_spv_d3d_binding *b,dxil_spv_srv_vulkan_binding *v)
{ memset(v,0,sizeof(*v));return collect(u,DXBC_BINDING_SRV,b,0); }
static dxil_spv_bool collect_sampler(void *u,const dxil_spv_d3d_binding *b,dxil_spv_vulkan_binding *v)
{ memset(v,0,sizeof(*v));return collect(u,DXBC_BINDING_SAMPLER,b,0); }
static dxil_spv_bool collect_cbv(void *u,const dxil_spv_d3d_binding *b,dxil_spv_cbv_vulkan_binding *v)
{ memset(v,0,sizeof(*v));return collect(u,DXBC_BINDING_CBV,b,0); }
static dxil_spv_bool collect_uav(void *u,const dxil_spv_uav_d3d_binding *b,dxil_spv_uav_vulkan_binding *v)
{ memset(v,0,sizeof(*v));return collect(u,DXBC_BINDING_UAV,&b->d3d_binding,b->has_counter); }

static int earlier(const struct dxbc_binding *a,const struct dxbc_binding *b)
{
   if (a->kind != b->kind) return a->kind < b->kind;
   if (a->space != b->space) return a->space < b->space;
   return a->reg < b->reg;
}
static int freeze_map(struct conversion_job *job)
{
   if (job->rejected) return 0;
   for (unsigned i=1;i<job->map.count;++i) {
      struct dxbc_binding entry=job->map.bindings[i];unsigned j=i;
      while (j && earlier(&entry,&job->map.bindings[j-1])) {
         job->map.bindings[j]=job->map.bindings[j-1];--j;
      }
      job->map.bindings[j]=entry;
   }
   uint32_t counts[DXBC_BINDING_KIND_COUNT]={0};
   for (unsigned i=0;i<job->map.count;++i) {
      struct dxbc_binding *b=&job->map.bindings[i];
      if (counts[b->kind]>=job->limits[b->kind]) return 0;
      b->slot=counts[b->kind]++;
   }
   job->map.active=1;
   return 1;
}
static dxil_spv_bool remap_uav(void *u,const dxil_spv_uav_d3d_binding *binding,
   dxil_spv_uav_vulkan_binding *v)
{
   struct conversion_job *job=u;const dxil_spv_d3d_binding *b=&binding->d3d_binding;
   memset(v,0,sizeof(*v));
   const struct dxbc_binding *found=dxbc_binding_find(&job->map,DXBC_BINDING_UAV,b->register_space,b->register_index);
   if (job->rejected || !found || found->id!=b->resource_index || b->stage!=DXIL_SPV_STAGE_COMPUTE ||
       b->kind!=DXIL_SPV_RESOURCE_KIND_RAW_BUFFER || b->range_size!=1 || binding->has_counter || b->alignment!=16) {
      job->rejected=1;return DXIL_SPV_FALSE;
   }
   v->buffer_binding.set=DXBC_BINDING_UAV;
   v->buffer_binding.binding=found->slot;
   v->buffer_binding.descriptor_type=DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_SSBO;
   return DXIL_SPV_TRUE;
}
static dxil_spv_bool refuse_srv(void *u,const dxil_spv_d3d_binding *b,dxil_spv_srv_vulkan_binding *v)
{ (void)b;memset(v,0,sizeof(*v));((struct conversion_job *)u)->rejected=1;return DXIL_SPV_FALSE; }
static dxil_spv_bool refuse_sampler(void *u,const dxil_spv_d3d_binding *b,dxil_spv_vulkan_binding *v)
{ (void)b;memset(v,0,sizeof(*v));((struct conversion_job *)u)->rejected=1;return DXIL_SPV_FALSE; }
static dxil_spv_bool refuse_cbv(void *u,const dxil_spv_d3d_binding *b,dxil_spv_cbv_vulkan_binding *v)
{ (void)b;memset(v,0,sizeof(*v));((struct conversion_job *)u)->rejected=1;return DXIL_SPV_FALSE; }

void agx_dxil_owned_free(struct agx_dxil_owned_result *result)
{
   if (!result) return;
   free(result->spirv);memset(result,0,sizeof(*result));
}

enum agx_dxil_owned_status agx_dxil_convert_owned(
   const struct agx_dxil_owned_request *request,struct agx_dxil_owned_result *result)
{
   if (!result) return AGX_DXIL_OWNED_INVALID;
   memset(result,0,sizeof(*result));
   if (in_progress) return AGX_DXIL_OWNED_REENTRANT;
   if (!request || !request->entry || !request->entry_bytes || request->entry_bytes>255 ||
       memchr(request->entry,0,request->entry_bytes)) return AGX_DXIL_OWNED_INVALID;
   const uint32_t maximum[DXBC_BINDING_KIND_COUNT]={14,128,8,16};
   for (unsigned i=0;i<DXBC_BINDING_KIND_COUNT;++i)
      if (request->binding_limits[i]>maximum[i]) return AGX_DXIL_OWNED_UNSUPPORTED;
   const struct agx_dxil_profile profile={UINT32_C(1)<<5,0x60,0x60};
   struct agx_dxil_program program;
   enum agx_dxil_parse_status framing=agx_dxil_parse(request->input,request->input_bytes,request->input_kind,&profile,&program);
   if (framing!=AGX_DXIL_PARSE_OK)
      return framing==AGX_DXIL_PARSE_UNSUPPORTED?AGX_DXIL_OWNED_UNSUPPORTED:AGX_DXIL_OWNED_INVALID;
   unsigned major=0,minor=0,patch=0;dxil_spv_get_version(&major,&minor,&patch);
   if (major!=2 || minor!=75 || patch!=0) return AGX_DXIL_OWNED_UNSUPPORTED;
   unsigned char *input=malloc(program.bitcode_bytes);
   if (!input) return AGX_DXIL_OWNED_PROJECT_OOM;
   memcpy(input,(const unsigned char *)request->input+program.bitcode_offset,program.bitcode_bytes);
   char entry[256]={0};memcpy(entry,request->entry,request->entry_bytes);
   struct conversion_job job={0};memcpy(job.limits,request->binding_limits,sizeof(job.limits));
   dxil_spv_parsed_blob blob=NULL;dxil_spv_converter converter=NULL;
   uint32_t *owned=NULL;size_t owned_bytes=0;unsigned workgroup[3]={0};
   enum agx_dxil_owned_status status=AGX_DXIL_OWNED_REPORTED_ERROR;
   in_progress=1;
   /* begin itself has no recoverable OOM status. All cleanup below concerns
    * normal returns/reported errors only, per the explicit dependency contract. */
   dxil_spv_begin_thread_allocator_context();
   if (dxil_spv_parse_dxil(input,program.bitcode_bytes,&blob)!=DXIL_SPV_SUCCESS) goto cleanup;
   unsigned entries=0;const char *name=NULL;
   if (dxil_spv_parsed_blob_get_shader_stage(blob)!=DXIL_SPV_STAGE_COMPUTE ||
       dxil_spv_parsed_blob_get_num_entry_points(blob,&entries)!=DXIL_SPV_SUCCESS || entries!=1 ||
       dxil_spv_parsed_blob_get_entry_point_name(blob,0,&name)!=DXIL_SPV_SUCCESS || !name || strcmp(name,entry)) {
      status=AGX_DXIL_OWNED_UNSUPPORTED;goto cleanup;
   }
   if (dxil_spv_parsed_blob_scan_resources(blob,collect_srv,collect_sampler,collect_cbv,collect_uav,&job)!=DXIL_SPV_SUCCESS) goto cleanup;
   if (!freeze_map(&job)) {status=AGX_DXIL_OWNED_UNSUPPORTED;goto cleanup;}
   if (dxil_spv_create_converter(blob,&converter)!=DXIL_SPV_SUCCESS) goto cleanup;
   dxil_spv_converter_set_entry_point(converter,entry);
   dxil_spv_converter_set_srv_remapper(converter,refuse_srv,&job);
   dxil_spv_converter_set_sampler_remapper(converter,refuse_sampler,&job);
   dxil_spv_converter_set_cbv_remapper(converter,refuse_cbv,&job);
   dxil_spv_converter_set_uav_remapper(converter,remap_uav,&job);
   if (dxil_spv_converter_run(converter)!=DXIL_SPV_SUCCESS || job.rejected) goto cleanup;
   dxil_spv_compiled_spirv output={0};
   if (dxil_spv_converter_get_compiled_spirv(converter,&output)!=DXIL_SPV_SUCCESS ||
       !output.data || output.size<20 || output.size%4) goto cleanup;
   uint32_t magic=0;memcpy(&magic,output.data,sizeof(magic));if (magic!=UINT32_C(0x07230203)) goto cleanup;
   if (dxil_spv_converter_get_compute_workgroup_dimensions(converter,&workgroup[0],&workgroup[1],&workgroup[2])!=DXIL_SPV_SUCCESS) goto cleanup;
   owned=malloc(output.size);
   if (!owned) {status=AGX_DXIL_OWNED_PROJECT_OOM;goto cleanup;}
   memcpy(owned,output.data,output.size);owned_bytes=output.size;status=AGX_DXIL_OWNED_OK;
cleanup:
   if (converter) dxil_spv_converter_free(converter);
   if (blob) dxil_spv_parsed_blob_free(blob);
   dxil_spv_end_thread_allocator_context();in_progress=0;free(input);
   if (status==AGX_DXIL_OWNED_OK) {
      result->spirv=owned;result->spirv_bytes=owned_bytes;result->bindings=job.map;
      memcpy(result->workgroup,workgroup,sizeof(workgroup));memcpy(result->entry,entry,sizeof(entry));
   } else free(owned);
   return status;
}
