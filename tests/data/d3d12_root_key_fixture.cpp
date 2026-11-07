/* Original exact-field byte oracle. Actual records/owned buffers only. MIT. */
#include <windows.h>
#include <d3d12umddi.h>
#include "agx_root_description_key.h"
#include "agx_shader_input_provider.h"
#include "agx_d3d12_zero_cache_library.h"
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <algorithm>
static unsigned checks;static AgxCpuProvider *provider;
static void need(bool b,const char*s){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",s);std::abort();}}
template<class T>static uint32_t raw(const T&x){static_assert(sizeof(T)==4,"word");uint32_t n;std::memcpy(&n,&x,4);return n;}
template<class T>static void put(T&x,uint32_t n){static_assert(sizeof(T)==4,"word");std::memcpy(&x,&n,4);}
template<class T>static void change(T&x){put(x,raw(x)^1u);}
struct Owned {
 D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 args{};D3D12DDI_ROOT_SIGNATURE desc{};
 D3D12DDI_ROOT_PARAMETER p[3]{};D3D12DDI_DESCRIPTOR_RANGE r[3]{};D3D12DDI_STATIC_SAMPLER s[2]{};AgxRootKeySpan tables[3]{};AgxRootKeyInput in{};
 void bind(){args.pRootSignature=&desc;desc.pRootParameters=p;desc.pStaticSamplers=s;
  for(unsigned n=0;n<3;n++){tables[n]={r,sizeof(r)};if(raw(p[n].ParameterType)==0)p[n].DescriptorTable.pDescriptorRanges=r;}
  in={{&args,sizeof(args)},{&desc,sizeof(desc)},{p,sizeof(p)},{s,sizeof(s)},{tables,sizeof(tables)}};
 }
};
static std::vector<unsigned char> encode(Owned&x){x.bind();SIZE_T size=~SIZE_T(0);need(AgxRootKeyCalculate(&x.in,&size)==AgxRootKeyStatus::Ok&&size>=32,"Calculate structural size");
 std::vector<unsigned char> bytes(size+32,0xa5);AgxRootKeyView view{};need(AgxRootKeyEncode(&x.in,bytes.data()+16,size,&view)==AgxRootKeyStatus::Ok&&view.data==bytes.data()+16&&view.bytes==size,"complete view published");
 for(unsigned i=0;i<16;i++)need(bytes[i]==0xa5&&bytes[16+size+i]==0xa5,"output guards");return {view.data,view.data+view.bytes};}
static std::vector<unsigned char> hex(const char*s){std::vector<unsigned char> b;int high=-1;
 for(;*s;s++){int n=*s>='0'&&*s<='9'?*s-'0':*s>='a'&&*s<='f'?*s-'a'+10:-1;if(n<0)continue;if(high<0)high=n;else{b.push_back(static_cast<unsigned char>((high<<4)|n));high=-1;}}
 need(high==-1,"hand oracle hex complete");return b;}
static Owned base(){Owned x;x.args.NodeMask=1;put(x.desc.Flags,1);x.desc.NumParameters=3;x.desc.NumStaticSamplers=2;
 x.p[0].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;x.p[0].Constants={1,2,3};x.p[0].ShaderVisibility=D3D12DDI_SHADER_VISIBILITY_ALL;
 x.p[1].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_CBV;x.p[1].Descriptor={4,5};x.p[1].ShaderVisibility=D3D12DDI_SHADER_VISIBILITY_PIXEL;
 x.p[2].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;x.p[2].DescriptorTable.NumDescriptorRanges=2;x.p[2].ShaderVisibility=D3D12DDI_SHADER_VISIBILITY_VERTEX;
 x.r[0]={D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV,2,3,4,5};x.r[1]={D3D12DDI_DESCRIPTOR_RANGE_TYPE_UAV,3,8,9,UINT32_MAX};
 for(unsigned n=0;n<2;n++){x.s[n].Filter=D3D12DDI_FILTER_MIN_MAG_MIP_POINT;x.s[n].AddressU=D3D12DDI_TEXTURE_ADDRESS_MODE_WRAP;x.s[n].AddressV=D3D12DDI_TEXTURE_ADDRESS_MODE_MIRROR;x.s[n].AddressW=D3D12DDI_TEXTURE_ADDRESS_MODE_CLAMP;put(x.s[n].MipLODBias,0x3f800000);x.s[n].MaxAnisotropy=1;x.s[n].ComparisonFunc=D3D12DDI_COMPARISON_FUNC_NEVER;x.s[n].BorderColor=D3D12DDI_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;put(x.s[n].MinLOD,0x80000000);put(x.s[n].MaxLOD,0x7f7fffff);x.s[n].ShaderRegister=4+n;x.s[n].RegisterSpace=5;x.s[n].ShaderVisibility=D3D12DDI_SHADER_VISIBILITY_ALL;}
 return x;}
extern "C" const void*AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE d){return AgxCpuResolveDeviceIdentity(provider,d);}
extern "C" const void*AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE){std::abort();}
int main(int argc,char**argv){
 need(argc==2,"actual PROGRAM input");
 // Independent literal byte oracles, not a second field encoder.
 Owned empty;need(encode(empty)==hex("414758524b455931 01000000 01000000 00000000 00000000 00000000 00000000"),"hand empty32");
 Owned pair;pair.args.NodeMask=1;put(pair.desc.Flags,1);pair.desc.NumParameters=2;
 pair.p[0].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;pair.p[0].ShaderVisibility=D3D12DDI_SHADER_VISIBILITY_PIXEL;pair.p[0].Constants={2,3,4};
 pair.p[1].ParameterType=D3D12DDI_ROOT_PARAMETER_TYPE_SRV;pair.p[1].ShaderVisibility=D3D12DDI_SHADER_VISIBILITY_VERTEX;pair.p[1].Descriptor={7,2};
 auto pairkey=encode(pair);need(pairkey==hex("414758524b455931 01000000 01000000 01000000 01000000 02000000 00000000 01000000 05000000 02000000 03000000 04000000 03000000 01000000 07000000 02000000"),"hand constants+descriptor68");
 Owned sampler=base();sampler.args.NodeMask=0;put(sampler.desc.Flags,0);sampler.desc.NumParameters=0;sampler.desc.NumStaticSamplers=1;
 need(encode(sampler)==hex("414758524b455931 01000000 01000000 00000000 00000000 00000000 01000000 00000000 01000000 02000000 03000000 0000803f 01000000 01000000 00000000 00000080 ffff7f7f 04000000 05000000 00000000"),"hand all13sampler84");
 Owned table;table.desc.NumParameters=1;table.p[0].DescriptorTable.NumDescriptorRanges=2;table.r[0]={D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV,1,2,3,5};table.r[1]={D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV,UINT32_MAX,8,9,UINT32_MAX};
 need(encode(table)==hex("414758524b455931 01000000 01000000 00000000 00000000 01000000 00000000 00000000 00000000 02000000 00000000 01000000 02000000 03000000 05000000 00000000 ffffffff 08000000 09000000 ffffffff"),"hand ranges/unbounded/APPEND84");
 Owned a=base(),b=base();auto baseline=encode(a);need(encode(b)==baseline,"different record addresses same fields");
 // Poison only inactive/padding bytes, then initialize every active field. No
 // invalid enum value is read; active discrimination always reads raw bytes.
 Owned poisonA,poisonB;std::memset(poisonA.p,0xa5,sizeof(poisonA.p));std::memset(poisonB.p,0x5a,sizeof(poisonB.p));
 for(Owned*x:{&poisonA,&poisonB}){x->args.NodeMask=1;put(x->desc.Flags,1);x->desc.NumParameters=2;
  put(x->p[0].ParameterType,1);put(x->p[0].ShaderVisibility,5);x->p[0].Constants={2,3,4};put(x->p[1].ParameterType,3);put(x->p[1].ShaderVisibility,1);x->p[1].Descriptor={7,2};}
 need(encode(poisonA)==pairkey&&encode(poisonB)==pairkey,"padding/inactive union excluded");
 auto mutation=[&](auto f){Owned changed=base();f(changed);need(encode(changed)!=baseline,"each meaningful field affects key");};
 mutation([](Owned&x){change(x.args.NodeMask);});mutation([](Owned&x){change(x.desc.Flags);});mutation([](Owned&x){--x.desc.NumParameters;});mutation([](Owned&x){--x.desc.NumStaticSamplers;});
 mutation([](Owned&x){put(x.p[1].ParameterType,3);});mutation([](Owned&x){change(x.p[0].ShaderVisibility);});
 mutation([](Owned&x){change(x.p[0].Constants.ShaderRegister);});mutation([](Owned&x){change(x.p[0].Constants.RegisterSpace);});mutation([](Owned&x){change(x.p[0].Constants.Num32BitValues);});
 mutation([](Owned&x){change(x.p[1].Descriptor.ShaderRegister);});mutation([](Owned&x){change(x.p[1].Descriptor.RegisterSpace);});mutation([](Owned&x){--x.p[2].DescriptorTable.NumDescriptorRanges;});
 mutation([](Owned&x){change(x.r[0].RangeType);});mutation([](Owned&x){change(x.r[0].NumDescriptors);});mutation([](Owned&x){change(x.r[0].BaseShaderRegister);});mutation([](Owned&x){change(x.r[0].RegisterSpace);});mutation([](Owned&x){change(x.r[0].OffsetInDescriptorsFromTableStart);});
 mutation([](Owned&x){change(x.s[0].Filter);});mutation([](Owned&x){change(x.s[0].AddressU);});mutation([](Owned&x){change(x.s[0].AddressV);});mutation([](Owned&x){change(x.s[0].AddressW);});mutation([](Owned&x){change(x.s[0].MipLODBias);});mutation([](Owned&x){change(x.s[0].MaxAnisotropy);});mutation([](Owned&x){change(x.s[0].ComparisonFunc);});mutation([](Owned&x){change(x.s[0].BorderColor);});mutation([](Owned&x){change(x.s[0].MinLOD);});mutation([](Owned&x){change(x.s[0].MaxLOD);});mutation([](Owned&x){change(x.s[0].ShaderRegister);});mutation([](Owned&x){change(x.s[0].RegisterSpace);});mutation([](Owned&x){change(x.s[0].ShaderVisibility);});
 mutation([](Owned&x){std::swap(x.p[0],x.p[1]);});mutation([](Owned&x){std::swap(x.r[0],x.r[1]);});mutation([](Owned&x){std::swap(x.s[0],x.s[1]);});
 Owned rawData=base();put(rawData.desc.Flags,UINT32_MAX);put(rawData.r[0].RangeType,UINT32_MAX);put(rawData.s[0].Filter,UINT32_MAX);put(rawData.s[0].MinLOD,0x7fc00001);put(rawData.s[0].MaxLOD,0x7fc00002);put(rawData.s[0].ShaderVisibility,UINT32_MAX);need(!encode(rawData).empty(),"raw enum/float representations data only");
 auto raw1=encode(rawData);put(rawData.s[0].MinLOD,0x7fc00002);need(encode(rawData)!=raw1,"NaN payload preserved without float evaluation");
 Owned noPolicy=table;noPolicy.r[0].NumDescriptors=UINT32_MAX;need(!encode(noPolicy).empty(),"unbounded predecessor+APPEND remains data no admission");noPolicy.r[1].OffsetInDescriptorsFromTableStart=5;need(!encode(noPolicy).empty(),"explicit overlap remains data");
 Owned spell=table;auto appendKey=encode(spell);spell.r[1].OffsetInDescriptorsFromTableStart=6;need(encode(spell)!=appendKey,"raw APPEND spelling differs from explicit6");
 // Structural invalidations use owned actual records/buffers, never pointer faults.
 a.bind();std::array<unsigned char,1024> dest;dest.fill(0xa5);AgxRootKeyView view{};SIZE_T size=0;need(AgxRootKeyCalculate(&a.in,&size)==AgxRootKeyStatus::Ok,"size for negatives");
 need(AgxRootKeyEncode(&a.in,dest.data(),size-1,&view)==AgxRootKeyStatus::DestinationInvalid&&!view.data&&!view.bytes,"short destination no published key");need(std::all_of(dest.begin(),dest.end(),[](unsigned char c){return c==0xa5;}),"short destination untouched");
 auto reject=[&](AgxRootKeyInput in,AgxRootKeyStatus expected){AgxRootKeyView v{};SIZE_T n=7;need(AgxRootKeyCalculate(&in,&n)==expected&&!n,"invalid size zero");need(AgxRootKeyEncode(&in,dest.data(),dest.size(),&v)==expected&&!v.data&&!v.bytes,"invalid no published key");need(std::all_of(dest.begin(),dest.end(),[](unsigned char c){return c==0xa5;}),"invalid destination untouched");};
 auto bad=a.in;bad.args.bytes=sizeof(a.args)-1;reject(bad,AgxRootKeyStatus::InvalidSpan);bad=a.in;bad.description.bytes=sizeof(a.desc)-1;reject(bad,AgxRootKeyStatus::InvalidSpan);bad=a.in;bad.parameters.bytes=sizeof(a.p)-1;reject(bad,AgxRootKeyStatus::InvalidSpan);bad=a.in;bad.samplers.bytes=sizeof(a.s)-1;reject(bad,AgxRootKeyStatus::InvalidSpan);bad=a.in;bad.table_spans.bytes=sizeof(a.tables)-1;reject(bad,AgxRootKeyStatus::InvalidSpan);
 bad=a.in;bad.parameters.data=b.p;reject(bad,AgxRootKeyStatus::InvalidSpan);bad=a.in;bad.samplers.bytes=std::numeric_limits<SIZE_T>::max();reject(bad,AgxRootKeyStatus::InvalidSpan);
 a.desc.NumParameters=UINT32_MAX;reject(a.in,AgxRootKeyStatus::InvalidSpan);a.desc.NumParameters=3;
 a.p[2].DescriptorTable.NumDescriptorRanges=UINT32_MAX;reject(a.in,AgxRootKeyStatus::InvalidSpan);a.p[2].DescriptorTable.NumDescriptorRanges=2;
 a.tables[2].bytes=sizeof(D3D12DDI_DESCRIPTOR_RANGE)-1;reject(a.in,AgxRootKeyStatus::InvalidSpan);a.tables[2]={a.r,sizeof(a.r)};
 put(a.p[0].ParameterType,99);reject(a.in,AgxRootKeyStatus::UnsupportedUnion);put(a.p[0].ParameterType,1);
 std::array<unsigned char,sizeof(Owned)> beforeObject;std::memcpy(beforeObject.data(),&a,sizeof(a));need(AgxRootKeyEncode(&a.in,&a,sizeof(a),&view)==AgxRootKeyStatus::DestinationInvalid&&!view.data,"source/destination overlap refused");need(!std::memcmp(beforeObject.data(),&a,sizeof(a)),"whole overlapped input unchanged");
 // Completed key copied to actual opaque provider and existing owned core.
 provider=AgxCpuProviderCreate();need(provider!=nullptr,"provider");int deviceIdentity=0,rootIdentity=0;AgxCpuDevice d{};AgxCpuRoot root{};
 need(AgxCpuRegisterDevice(provider,{&deviceIdentity},nullptr,nullptr,&d)==AgxCpuProviderStatus::Ok,"device");
 auto completed=encode(a),snapshot=completed;need(AgxCpuRegisterRoot(provider,d,{&rootIdentity},{completed.data(),completed.size()},&root)==AgxCpuProviderStatus::Ok,"key bytes copied by provider");completed.assign(completed.size(),0xcc);
 std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char> program((std::istreambuf_iterator<char>(f)),{});need(program.size()==1680,"actual1397 PROGRAM");D3D12DDIARG_CREATE_SHADER_0010 shader{};shader.pShaderCode=reinterpret_cast<const UINT*>(program.data());shader.hRootSignature={&rootIdentity};AgxCpuInputLease lease{};
 need(AgxCpuAcquireInput(provider,d,root,&shader,5,{program.data(),program.size()},{nullptr,0},{nullptr,0},&lease).status==AgxCpuProviderStatus::Ok,"key lease");AgxShaderTrustedSpan rootBytes{};need(AgxCpuLeaseRootBytes(provider,lease,&rootBytes)==AgxCpuProviderStatus::Ok&&rootBytes.bytes==snapshot.size()&&!std::memcmp(rootBytes.data,snapshot.data(),snapshot.size()),"opaque key independence");
 AgxShaderOwnedLayout layout{};need(AgxCpuCalculate(provider,lease,&layout).status==AgxCpuProviderStatus::Ok,"core key size");alignas(64)std::array<unsigned char,8192> storage{};AgxShaderOwned*owned=nullptr;need(layout.total<=storage.size()&&AgxCpuCopyOwned(provider,lease,storage.data(),storage.size(),&owned).status==AgxCpuProviderStatus::Ok&&owned,"core key copy");need(owned->layout.root_bytes==snapshot.size()&&!std::memcmp(reinterpret_cast<unsigned char*>(owned)+owned->layout.root_offset,snapshot.data(),snapshot.size()),"completed key owned by core");
 need(AgxCpuReleaseInput(provider,lease)==AgxCpuProviderStatus::Ok&&AgxCpuProviderDestroy(&provider)==AgxCpuProviderStatus::Ok&&!provider,"teardown");
 std::printf("ROOT_EXACT_KEY_PASS checks=%u hand_oracles=4 all_fields=YES raw_APPENDING=YES provider_copy=YES admission=NO\n",checks);
}
