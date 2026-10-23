/* Original oracle; actual native query is linked from native-query.c, no stand-in. */
extern "C" unsigned c21_native_bo_calls(void);
extern "C" unsigned c21_native_device_calls(void);
int main() {
 struct Case {unsigned format,side,depth,levels,bind;UINT64 expected,sparse,mip1;};
 const Case cases[]={{42,4,8,2,2,131072,16384,128},{28,4,8,2,2,131072,16384,128},
  {28,32,4,2,3,65536,8192,4096},{42,4,8,1,2,131072,16384,0},
  {28,32,4,1,3,65536,8192,0}};
 unsigned failed=0;
 for(const auto &c:cases) {
  pipe_resource t={};assert(texture3d_template(c.format,c.side,c.side,c.depth,c.levels,c.bind,&t));
  assert(t.flags==0 && !(t.bind&PIPE_BIND_SHADER_IMAGE));const pipe_resource saved=t;
  AGX_WIN32_ASAHI_RESOURCE_LAYOUT native={};assert(AgxWin32AsahiQueryUncompressedResourceLayout(&t,&native)==1);
  assert(!memcmp(&t,&saved,sizeof(t)));assert(native.SizeBytes==c.expected&&native.LayerStrideBytes==16384);
  assert(native.SparseTableSizeBytes==c.sparse && native.SparseTableSizeBytes>0);
  assert(!(native.Flags&AGX_WIN32_ASAHI_LAYOUT_COMPRESSED)&&!native.MetadataOffsetBytes&&!native.CompressionLayerStrideBytes);
  AGX_D3D12_VOLUME_LAYOUT result={};HRESULT hr=AgxD3d12QueryTexture3DLayout(c.format,c.side,c.side,c.depth,c.levels,c.bind,&result);
  bool ok=hr==S_OK&&result.Bytes==c.expected&&result.NativeBytes==native.SizeBytes&&result.DepthStride==native.LayerStrideBytes;
  for(unsigned m=0;m<c.levels;m++)ok=ok&&result.MipOffsets[m]==native.LevelOffsets[m];
  if(c.levels==2)assert(native.LevelOffsets[1]==c.mip1);
  printf("NATIVE_OWNER format=%u side=%u depth=%u mips=%u bind=%u native=%llu potential_sparse=%llu owner_hr=%08x result=%llu %s\n",c.format,c.side,c.depth,c.levels,c.bind,(unsigned long long)native.SizeBytes,(unsigned long long)native.SparseTableSizeBytes,static_cast<unsigned>(hr),result.Bytes,ok?"PASS":"FAIL");
  failed+=!ok;
 }
 AGX_D3D12_VOLUME_LAYOUT bad={};
 /* C21G: larger and non-square volumes pass the native query with their
  * per-level tiles (the registry's input); depth-stencil binds and sides
  * beyond D3D12's 2048 stay refused */
 AGX_D3D12_VOLUME_LAYOUT good={};
 assert(AgxD3d12QueryTexture3DLayout(42,64,64,8,2,2,&good)==S_OK&&good.Bytes&&good.ElementLog2==2&&
        good.TileWidth[0]==64&&good.TileHeight[0]==64&&good.TileWidth[1]==32&&good.StrideEl[1]==32&&good.HeightEl[1]==32);
 assert(AgxD3d12QueryTexture3DLayout(42,4,8,8,2,2,&good)==S_OK&&good.Bytes&&good.TileWidth[0]==4&&good.TileHeight[0]==4&&
        good.StrideEl[0]==4&&good.HeightEl[0]==8);
 assert(AgxD3d12QueryTexture3DLayout(42,4,4,8,2,4,&bad)==E_NOTIMPL&&!bad.Bytes);
 assert(AgxD3d12QueryTexture3DLayout(42,4096,4,8,1,2,&bad)==E_NOTIMPL&&!bad.Bytes);
 assert(c21_native_bo_calls()==0 && c21_native_device_calls()==0);
 printf("NATIVE_OWNER checks=5 failed=%u BO=0 device=0 no_actual_sparse_template=YES\n",failed);
 return failed?1:0;
}
