int main(){
 AgxD3d12Heap th={reinterpret_cast<void*>(1),0x10000,65536},bh={reinterpret_cast<void*>(2),0x30000,65536};
 AgxD3d12Resource t={&th,0x10000,65536,D3D12DDI_RT_TEXTURE1D,42,37,1,1,2},b={&bh,0x30000,65536,D3D12DDI_RT_BUFFER,0,65536,1,1,1};
 D3D12DDIARG_BUFFER_PLACEMENT tex={},buf={};tex.BaseAddress.UMD.hResource.pDrvPrivate=&t;tex.BaseAddress.UMD.Offset=1;buf.BaseAddress.UMD.hResource.pDrvPrivate=&b;buf.BaseAddress.UMD.Offset=512;
 D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT fp={42,18,1,1,256,256};
 D3D12DDIARG_PLACED_RESOURCE tl={D3D12DDI_RL_SELECT_SUBRESOURCE,nullptr},bl={D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED,&fp};
 AgxD3d12CommandList l={};D3D12DDI_HCOMMANDLIST h={&l};
 CopyTextureRegion(h,&tex,tl,0,0,0,&buf,bl,nullptr);assert(!l.error&&l.commands==1&&l.recorded.texture_copy.level==1&&l.recorded.texture_copy.width==18&&l.recorded.texture_copy.height==1&&l.recorded.texture_copy.depth==1&&l.recorded.texture_copy.offset==512);
 l={};CopyTextureRegion(h,&buf,bl,0,0,0,&tex,tl,nullptr);assert(!l.error&&l.commands==1&&l.recorded.texture_copy.to_buffer);
 D3D12DDI_BOX box={2,0,0,10,1,1};l={};CopyTextureRegion(h,&tex,tl,5,0,0,&buf,bl,&box);assert(!l.error&&l.recorded.texture_copy.x==5&&l.recorded.texture_copy.width==8&&l.recorded.texture_copy.offset==520);
 for(unsigned bad=0;bad<10;bad++){
  auto temp=t;auto foot=fp;auto bb=box;auto tb=tex;auto bp=buf;UINT x=0,y=0,z=0;
  switch(bad){case 0:tb.BaseAddress.UMD.Offset=2;break;case 1:x=~0u;break;case 2:y=1;break;case 3:z=1;break;case 4:bb.Left=-1;break;case 5:bb.Bottom=2;break;case 6:bb.Back=2;break;case 7:foot.Pitch=1;break;case 8:bp.BaseAddress.UMD.Offset=65535;break;case 9:foot.Format=28;break;}
  auto fl=bl;fl.pLayout=&foot;l={};CopyTextureRegion(h,&tb,tl,x,y,z,&bp,fl,&bb);assert(l.error&&!l.commands);
 }
 auto same=tex;l={};CopyTextureRegion(h,&tex,tl,1,0,0,&same,tl,&box);assert(!l.error&&l.commands==1&&l.recorded.kind==kCommandTextureRegionCopy&&l.recorded.region_copy.layers==1);
 t.type=D3D12DDI_RT_TEXTURE2D;l={};CopyTextureRegion(h,&tex,tl,0,0,0,&same,tl,nullptr);/* legacy2D remains separate */
 // C29: a BC1 (71) 2D texture: buffer offsets count block rows and 8-byte blocks
 AgxD3d12Resource bt={&th,0x10000,65536,D3D12DDI_RT_TEXTURE2D,71,8,8,1,3};
 D3D12DDIARG_BUFFER_PLACEMENT btex={};btex.BaseAddress.UMD.hResource.pDrvPrivate=&bt;
 D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT bfp={71,8,8,1,256,512};auto bfl=bl;bfl.pLayout=&bfp;
 D3D12DDI_BOX quarter={4,4,0,8,8,1};
 l={};CopyTextureRegion(h,&buf,bfl,4,4,0,&btex,tl,&quarter);
 assert(!l.error&&l.commands==1&&l.recorded.texture_copy.to_buffer&&l.recorded.texture_copy.offset==512+256+8&&l.recorded.texture_copy.x==4&&l.recorded.texture_copy.y==4);
 l={};CopyTextureRegion(h,&btex,tl,0,0,0,&buf,bfl,&quarter);
 assert(!l.error&&l.commands==1&&!l.recorded.texture_copy.to_buffer&&l.recorded.texture_copy.offset==512+256+8&&l.recorded.texture_copy.width==4);
 l={};CopyTextureRegion(h,&buf,bfl,2,0,0,&btex,tl,&quarter);assert(l.error&&!l.commands);
 puts("PASS C23 actualDDI1D copy selectedmip/NPOT/pitches/nonzeroorigins and10refusals; C29 BC1 block offsets");
}
