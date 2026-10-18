#include <initializer_list>
/* Original actual-query/owner/WDK-pattern composition, including NPOT/mips. */
extern "C" unsigned c21_native_bo_calls(void);
extern "C" unsigned c21_native_device_calls(void);
extern "C" unsigned c23_native_shape(const pipe_resource*,const AGX_WIN32_ASAHI_RESOURCE_LAYOUT*);
int main() {
 UINT index=5;D3D12DDI_SWIZZLE_PATTERN_DESC_0003 packed={};
 D3D12DDIARG_GETCAPS caps={D3D12DDICAPS_TYPE_SWIZZLE_PATTERN,&index,&packed,sizeof(packed)};
 assert(VolumeLayoutCaps(&caps)==S_OK && !packed.StackDepthSlices);
 WinSwizzle0004 pattern={};WinSwizzleAddressing addr={};addr.block_bits=addr.byte_bits=addr.channel_bits[0]=2;
 addr.row_stride=4;
 for(unsigned i=0;i<32;i++) {auto b=packed.InterleavePatternSourceBits[i];pattern.primary[i]={b.Valid,b.ChannelIndex,b.SourceBitIndex};assert(!packed.InterleavePatternXORSourceBits[i].Valid&&!packed.InterleavePatternXOR2SourceBits[i].Valid);}
 unsigned failed=0,cases=0,mips=0;
 for(unsigned format: {42u,28u})for(unsigned width=1;width<=16384;width++) {
  unsigned legal=1;for(unsigned w=width;w>1;w>>=1)++legal;
  for(unsigned which=0;which<(legal==1?1u:2u);which++) {
   const unsigned levels=which?legal:1u;
   pipe_resource t={};t.target=PIPE_TEXTURE_1D;t.format=FormatTranslate(format,FALSE);t.width0=width;
   t.height0=t.depth0=t.array_size=1;t.last_level=levels-1;t.nr_samples=t.nr_storage_samples=1;t.usage=PIPE_USAGE_DEFAULT;t.bind=PIPE_BIND_SAMPLER_VIEW;
   const auto saved=t;AGX_WIN32_ASAHI_RESOURCE_LAYOUT raw={};
   assert(AgxWin32AsahiQueryUncompressedResourceLayout(&t,&raw)==1&&!memcmp(&t,&saved,sizeof(t)));
   assert(raw.SparseTableSizeBytes && !(raw.Flags&AGX_WIN32_ASAHI_LAYOUT_COMPRESSED));
   assert(c23_native_shape(&t,&raw));
   // Actual image binding may legalize ordinary resources as writeable images.
   // Prove1D mip offsets and64KiB physical allocation remain compatible.
   pipe_resource writable=t;writable.bind|=PIPE_BIND_SHADER_IMAGE;AGX_WIN32_ASAHI_RESOURCE_LAYOUT image={};
   assert(AgxWin32AsahiQueryUncompressedResourceLayout(&writable,&image)==1);
   assert(image.Flags&AGX_WIN32_ASAHI_LAYOUT_WRITEABLE_IMAGE);
   assert(((image.SizeBytes+65535)&~65535ull)==((raw.SizeBytes+65535)&~65535ull));
   for(unsigned l=0;l<levels;l++)assert(image.LevelOffsets[l]==raw.LevelOffsets[l]);
   const UINT64 expected=(raw.SizeBytes+65535)&~65535ull;
   D3D12DDIARG_CREATERESOURCE_0003 arg={};arg.ResourceType=D3D12DDI_RT_TEXTURE1D;arg.Width=width;arg.Height=arg.DepthOrArraySize=1;
   arg.MipLevels=levels;arg.SampleDesc.Count=1;arg.Format=format;arg.Flags=16;
   const UINT64 got=TextureBytes(&arg);D3D12DDI_RESOURCE_ALLOCATION_INFO allocation_info={};
   CheckResourceAllocationInfo({},&arg,static_cast<D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS>(0),65536,1,&allocation_info);
   bool ok=got==expected && allocation_info.ResourceDataSize==expected && allocation_info.ResourceDataAlignment==65536 && allocation_info.Layout==0x105;
#ifdef C23_CANDIDATE
   assert(allocation_info.MipLevelSwizzleTransition==0xff);
#endif
#ifdef C23_CANDIDATE
   AGX_D3D12_VOLUME_LAYOUT owner={};assert(AgxD3d12QueryTexture1DLayout(format,width,levels,2,&owner)==S_OK);
   assert(owner.Bytes==expected&&owner.NativeBytes==raw.SizeBytes&&owner.DepthStride==raw.LayerStrideBytes);
   D3D12DDI_TEXTURE_LAYOUT resolved=0;assert(LineLayoutInfo(&arg,&owner,&resolved)&&resolved==0x105);
   for(unsigned l=0;l<levels;l++)assert(owner.MipOffsets[l]==raw.LevelOffsets[l]);
   AgxD3d12Heap heap={};heap.bytes=expected;heap.allocation=123;AgxD3d12Resource resource={};resource.heap=&heap;resource.bytes=expected;resource.type=D3D12DDI_RT_TEXTURE1D;
   resource.width=width;resource.height=resource.layers=1;resource.levels=levels;resource.volume_layout=resolved;resource.volume_transition=0xff;
   resource.volume_depth_stride=owner.DepthStride;memcpy(resource.volume_offsets,owner.MipOffsets,sizeof(resource.volume_offsets));
   for(unsigned l=0;l<levels;l++) {D3D12DDI_SUBRESOURCE_INFO info={};CheckSubresourceInfo({}, {&resource},l,&info);
    assert(info.Offset==raw.LevelOffsets[l]&&info.RowStride==4ull*(width>>l?width>>l:1u)&&info.DepthStride==raw.LayerStrideBytes);}
   D3D12DDI_SUBRESOURCE_INFO invalid={};CheckSubresourceInfo({}, {&resource},levels,&invalid);assert(!invalid.Offset&&!invalid.RowStride&&!invalid.DepthStride);
   D3D12DDI_RESOURCE_ALLOCATION_INFO reflected={};D3DKMT_HANDLE allocation=0;CheckExistingResourceAllocationInfo({}, {&resource},&reflected,&allocation);
   assert(reflected.ResourceDataSize==expected&&reflected.Layout==0x105&&reflected.MipLevelSwizzleTransition==0xff&&allocation==123);
#endif
   for(unsigned l=0;l<levels;l++) {
    unsigned w=width>>l;if(!w)w=1;addr.row_stride=4ull*w;addr.base=raw.LevelOffsets[l];addr.depth_stride=raw.LayerStrideBytes;
    assert(addr.base%4==0&&4ull*w<=raw.SizeBytes-addr.base);
    if(l+1<raw.AllocatedLevels)assert(addr.base+4ull*w<=raw.LevelOffsets[l+1]);
    for(unsigned x: {0u,w/2,w-1})for(unsigned byte=0;byte<4;byte++) {
     uint32_t source[4]={x*4+byte,0,0,0};uint64_t at=0;
     assert(win_swizzle_0004(&pattern,&addr,source,&at)==WIN_SWIZZLE_OK&&at==addr.base+4ull*x+byte);
    }++mips;
   }
   if(!ok && failed<5)printf("TEXTURE1D_REAL_RED format=%u width=%u levels=%u native=%llu potential_sparse=%llu owner_bytes=%llu\n",format,width,levels,(unsigned long long)raw.SizeBytes,(unsigned long long)raw.SparseTableSizeBytes,got);
   failed+=!ok;++cases;
  }
 }
 assert(c21_native_bo_calls()==0 && c21_native_device_calls()==0);
 printf("TEXTURE1D_QUERY cases=%u requested_mips=%u failed=%u BO=0 device=0 packed_pattern=0x105 NPOT=YES\n",cases,mips,failed);
 return failed?1:0;
}
