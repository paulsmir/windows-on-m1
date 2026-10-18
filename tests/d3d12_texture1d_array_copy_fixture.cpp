int main(){
 AgxD3d12Heap th={reinterpret_cast<void*>(1),0,65536},bh={reinterpret_cast<void*>(2),0x30000,65536};
 AgxD3d12Resource t={&th,0,65536,D3D12DDI_RT_TEXTURE1D,42,37,1,3,2},b={&bh,0x30000,65536,D3D12DDI_RT_BUFFER,0,65536,1,1,1};
 D3D12DDIARG_BUFFER_PLACEMENT tex={},buf={};tex.BaseAddress.UMD.hResource.pDrvPrivate=&t;buf.BaseAddress.UMD.hResource.pDrvPrivate=&b;buf.BaseAddress.UMD.Offset=512;
 D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT fp={42,18,1,1,256,256};
 D3D12DDIARG_PLACED_RESOURCE tl={D3D12DDI_RL_SELECT_SUBRESOURCE,nullptr},bl={D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED,&fp};
 AgxD3d12CommandList l={};D3D12DDI_HCOMMANDLIST h={&l};
 for(UINT sub=0;sub<6;sub++){
  tex.BaseAddress.UMD.Offset=sub;fp.PhysicalWidth=37>>(sub%2);l={};CopyTextureRegion(h,&tex,tl,0,0,0,&buf,bl,nullptr);
  assert(!l.error&&l.commands==1&&l.recorded.texture_copy.level==sub%2&&l.recorded.texture_copy.layer==sub/2&&l.recorded.texture_copy.width==fp.PhysicalWidth&&l.recorded.texture_copy.height==1&&l.recorded.texture_copy.depth==1&&l.recorded.texture_copy.offset==512);
  l={};CopyTextureRegion(h,&buf,bl,0,0,0,&tex,tl,nullptr);assert(!l.error&&l.commands==1&&l.recorded.texture_copy.to_buffer&&l.recorded.texture_copy.layer==sub/2);
 }
 tex.BaseAddress.UMD.Offset=5;fp.PhysicalWidth=18;D3D12DDI_BOX box={2,0,0,10,1,1};l={};CopyTextureRegion(h,&tex,tl,5,0,0,&buf,bl,&box);
 assert(!l.error&&l.recorded.texture_copy.x==5&&l.recorded.texture_copy.layer==2&&l.recorded.texture_copy.offset==520&&l.recorded.texture_copy.width==8);
 auto other=tex;other.BaseAddress.UMD.Offset=3;l={};CopyTextureRegion(h,&tex,tl,5,0,0,&other,tl,&box);
 assert(!l.error&&l.recorded.region_copy.src_level==1&&l.recorded.region_copy.src_layer==1&&l.recorded.region_copy.dst_layer==2&&l.recorded.region_copy.layers==1);
 for(unsigned bad=0;bad<12;bad++){
  auto foot=fp;auto bb=box;auto tb=tex;auto bp=buf;UINT x=0,y=0,z=0;
  switch(bad){case 0:tb.BaseAddress.UMD.Offset=6;break;case 1:x=~0u;break;case 2:y=1;break;case 3:z=1;break;case 4:bb.Left=-1;break;case 5:bb.Bottom=2;break;case 6:bb.Back=2;break;case 7:foot.Pitch=1;break;case 8:bp.BaseAddress.UMD.Offset=65535;break;case 9:foot.Format=28;break;case 10:foot.PhysicalDepth=2;break;case 11:tb.BaseAddress.UMD.Offset=~0ull;break;}
  auto fl=bl;fl.pLayout=&foot;l={};CopyTextureRegion(h,&tb,tl,x,y,z,&bp,fl,&bb);assert(l.error&&!l.commands);
 }
 l={};ResourceCopy(h,{&t},{&t});assert(!l.error&&l.commands==1&&l.recorded.region_copy.whole_levels==2&&l.recorded.region_copy.layers==3&&!l.recorded.region_copy.volume);
 puts("PASS C24 actualDDI copy6 subresources bothdirections/selectedlayer2/mip1/NPOT/textureVA0/nonzeroorigins and12refusals");
}
