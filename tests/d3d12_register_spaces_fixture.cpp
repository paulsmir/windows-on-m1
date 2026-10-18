#include <cassert>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstring>
#include "dxbc_bindings.h"
using UINT=uint32_t; using UINT64=uint64_t; using BYTE=unsigned char;
static constexpr UINT kAnyStage=~0u,kRootArguments=64,kCbSlots=14,kSrvSlots=128,kUavSlots=8,kSamplerSlots=16;
enum {D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE=0,D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS=1,
 D3D12DDI_ROOT_PARAMETER_TYPE_CBV=2,D3D12DDI_ROOT_PARAMETER_TYPE_SRV=3,D3D12DDI_ROOT_PARAMETER_TYPE_UAV=4};
enum {D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV=0,D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV=1,
 D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV=2,D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER=3};
enum {D3D12DDI_SHADER_VISIBILITY_ALL=0,D3D12DDI_SHADER_VISIBILITY_VERTEX=1,D3D12DDI_SHADER_VISIBILITY_PIXEL=5};
enum {D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV=0,D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER=1};
struct AgxD3d12Heap {void *cpu; UINT64 bytes;};
struct AgxD3d12Descriptor {BYTE bytes[64];};
struct AgxD3d12SamplerDescriptor {BYTE bytes[32];};
struct D3D12DDI_STATIC_SAMPLER {UINT ShaderVisibility,RegisterSpace,ShaderRegister;};
struct AgxD3d12Range {UINT type,count,base_register,space,offset;};
struct AgxD3d12RootParameter {UINT type,shader_register,space,offset,dwords,visibility,first_range,ranges;};
struct AgxD3d12RootSignature {UINT parameters,static_samplers; AgxD3d12RootParameter parameter[3];};
struct FakePso {struct {dxbc_binding_map Bindings;} compute;
 struct {dxbc_binding_map VsBindings,PsBindings;} graphics; UINT vs_dwords=1,ps_dwords=1;};
struct AgxD3d12CommandList {void *device; FakePso *pso;};
struct AgxD3d12Dispatch {
 AgxD3d12Heap *uav_heap[8]={},*cb_heap[14]={},*srv_heap[128]={};
 UINT64 uav_offset[8]={},cb_offset[14]={},srv_offset[128]={};
 UINT cb_constants[14]={},cb_count[14]={},root_dwords[64]={};
 const AgxD3d12Descriptor *uav_descriptor[8]={},*cb_descriptor[14]={},*srv_descriptor[128]={};
 const AgxD3d12SamplerDescriptor *sampler[16]={};
 const D3D12DDI_STATIC_SAMPLER *static_sampler[16]={};
};
static AgxD3d12Heap heaps[3];
static AgxD3d12Heap *HeapAt(void *,UINT64 va,UINT64 *offset) { *offset=0;return va>=1&&va<=3 ? &heaps[va-1] : nullptr; }
static AgxD3d12Range *RangesOf(const AgxD3d12RootSignature *) {static AgxD3d12Range r[2];return r;}
static D3D12DDI_STATIC_SAMPLER *StaticSamplersOf(const AgxD3d12RootSignature *) {static D3D12DDI_STATIC_SAMPLER s[1];return s;}
static UINT DescriptorBytes(UINT type) {return type==D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 32 : 64;}
static UINT BindTable(AgxD3d12CommandList *,const AgxD3d12RootSignature *,const AgxD3d12RootParameter &,UINT64,AgxD3d12Dispatch *) {return 0;}
/* PRODUCTION_ROOT_RESOLUTION */

static void root_descriptors(bool missing) {
 FakePso pso={}; pso.compute.Bindings.active=1;pso.compute.Bindings.count=3;
 pso.compute.Bindings.bindings[0]={DXBC_BINDING_UAV,0,1,0,0};
 pso.compute.Bindings.bindings[1]={DXBC_BINDING_UAV,1,2,0,1};
 pso.compute.Bindings.bindings[2]={DXBC_BINDING_CBV,0,3,0,0};
 AgxD3d12CommandList list={nullptr,&pso}; AgxD3d12RootSignature root={};root.parameters=3;
 root.parameter[0].type=D3D12DDI_ROOT_PARAMETER_TYPE_UAV;root.parameter[0].space=1;
 root.parameter[1].type=D3D12DDI_ROOT_PARAMETER_TYPE_UAV;root.parameter[1].space=missing ? 1 : 2;
 root.parameter[2].type=D3D12DDI_ROOT_PARAMETER_TYPE_CBV;root.parameter[2].space=3;
 UINT64 va[64]={1,2,3};UINT words[64]={};AgxD3d12Dispatch out={};
 UINT result=ResolveRoot(&list,&root,va,words,kAnyStage,&out);
 if(missing) {assert(result);return;}
 assert(!result && out.uav_heap[0]==&heaps[0] && out.uav_heap[1]==&heaps[1] && out.cb_heap[0]==&heaps[2]);
}

/* Exercise the actual extracted resolver: table delta arithmetic must not
 * alias a sparse logical register, visibility must isolate stage namespaces,
 * and ambiguous matches must refuse rather than pick the first source. */
static void extended(const char *mode) {
 FakePso pso={}; AgxD3d12CommandList list={nullptr,&pso}; AgxD3d12RootSignature root={};
 UINT64 va[64]={1,2,3}; UINT words[64]={}; AgxD3d12Dispatch out={};
 BYTE memory[256]={}; heaps[0].cpu=memory; heaps[0].bytes=sizeof(memory);
 auto &map=pso.graphics.PsBindings; map.active=1; map.count=1;
 if(!std::strcmp(mode,"table")) {
  root.parameters=1; root.parameter[0].type=D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root.parameter[0].ranges=1; root.parameter[0].visibility=D3D12DDI_SHADER_VISIBILITY_PIXEL;
  map.bindings[0]={DXBC_BINDING_SRV,71,33,UINT_MAX-1,7};
  RangesOf(&root)[0]={D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV,4,UINT_MAX-3,33,1};
  assert(!ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  assert(out.srv_descriptor[7]==reinterpret_cast<AgxD3d12Descriptor*>(memory+192));
  RangesOf(&root)[0].count=2;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  RangesOf(&root)[0].count=4; RangesOf(&root)[0].offset=UINT_MAX;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  RangesOf(&root)[0].offset=2;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  RangesOf(&root)[0].offset=1; RangesOf(&root)[0].count=UINT_MAX;
  assert(!ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  RangesOf(&root)[0].space=34;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
 } else if(!std::strcmp(mode,"sampler")) {
  root.static_samplers=1; map.bindings[0]={DXBC_BINDING_SAMPLER,55,34,129,4};
  StaticSamplersOf(&root)[0]={D3D12DDI_SHADER_VISIBILITY_PIXEL,34,129};
  assert(!ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  assert(out.static_sampler[4]==StaticSamplersOf(&root));
  StaticSamplersOf(&root)[0].RegisterSpace=35;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  StaticSamplersOf(&root)[0].RegisterSpace=34;
  StaticSamplersOf(&root)[0].ShaderVisibility=D3D12DDI_SHADER_VISIBILITY_VERTEX;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  root.static_samplers=0; root.parameters=1;
  root.parameter[0].type=D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; root.parameter[0].ranges=1;
  RangesOf(&root)[0]={D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER,1,129,34,7};
  assert(!ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  assert(out.sampler[4]==reinterpret_cast<AgxD3d12SamplerDescriptor*>(memory+224));
  RangesOf(&root)[0].offset=8;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
 } else if(!std::strcmp(mode,"constants")) {
  root.parameters=1; map.bindings[0]={DXBC_BINDING_CBV,55,34,129,4};
  root.parameter[0]={D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,129,34,61,3,D3D12DDI_SHADER_VISIBILITY_PIXEL,0,0};
  words[61]=17;words[62]=29;words[63]=37;
  assert(!ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  assert(out.cb_constants[4]==62 && out.cb_count[4]==3 && out.root_dwords[63]==37);
  root.parameter[0].dwords=4;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  root.parameter[0].offset=UINT_MAX;root.parameter[0].dwords=1;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
 } else if(!std::strcmp(mode,"visibility")) {
  root.parameters=2; map.bindings[0]={DXBC_BINDING_UAV,55,34,129,4};
  root.parameter[0]={D3D12DDI_ROOT_PARAMETER_TYPE_UAV,129,34,0,2,D3D12DDI_SHADER_VISIBILITY_VERTEX,0,0};
  root.parameter[1]={D3D12DDI_ROOT_PARAMETER_TYPE_UAV,129,34,2,2,D3D12DDI_SHADER_VISIBILITY_PIXEL,0,0};
  assert(!ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  assert(out.uav_heap[4]==&heaps[1]);
  pso.graphics.VsBindings=map; AgxD3d12Dispatch vs={};
  assert(!ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_VERTEX,&vs));
  assert(vs.uav_heap[4]==&heaps[0]);
  root.parameter[1].visibility=D3D12DDI_SHADER_VISIBILITY_ALL;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_VERTEX,&vs));
 } else if(!std::strcmp(mode,"ambiguity")) {
  root.parameters=2;map.bindings[0]={DXBC_BINDING_UAV,55,34,129,4};
  root.parameter[0]={D3D12DDI_ROOT_PARAMETER_TYPE_UAV,129,34,0,2,D3D12DDI_SHADER_VISIBILITY_ALL,0,0};
  root.parameter[1]=root.parameter[0];
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  root.parameters=1; map.count=2;map.bindings[1]={DXBC_BINDING_UAV,56,35,130,4};
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  map.bindings[1]={DXBC_BINDING_UAV,56,34,129,5};
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  map.count=1; map.bindings[0].slot=8;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  map.bindings[0]={DXBC_BINDING_SAMPLER,55,34,129,4};
  root.parameter[0]={D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,0,0,0,1,D3D12DDI_SHADER_VISIBILITY_ALL,0,1};
  RangesOf(&root)[0]={D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER,1,129,34,0};
  root.static_samplers=1; StaticSamplersOf(&root)[0]={D3D12DDI_SHADER_VISIBILITY_ALL,34,129};
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  root.static_samplers=0; root.parameter[0].ranges=2;
  RangesOf(&root)[1]=RangesOf(&root)[0];
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
  map.count=DXBC_BINDING_MAP_MAX+1;
  assert(ResolveRoot(&list,&root,va,words,D3D12DDI_SHADER_VISIBILITY_PIXEL,&out));
 } else {assert(!"unknown test mode");}
}
int main(int argc,char **argv) {
 if(argc==1 || !std::strcmp(argv[1],"missing")) root_descriptors(argc>1);
 else extended(argv[1]);
 puts("PASS static register spaces"); return 0;
}
