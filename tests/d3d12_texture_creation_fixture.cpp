/* Original invariant fixture, not a native layout/allocation implementation.
 * Existing query/screen boundaries are controlled. Actual production owner
 * constructors and actual buffer-only GPUVA helper are composed verbatim. */
int main() {
 unsigned checks=0,failures=0;
 auto check=[&](bool ok,const char *name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}};
 pipe_screen screen={};screen.is_format_supported=supported;screen.resource_create_with_modifiers=create;
 AGX_D3D12_OWNER owner={&screen};AGX_D3D12_BUFFER line={},volume={};
 HRESULT lh=AgxD3d12OwnerCreateTexture1D(&owner,42,4,2,AGX_D3D12_BIND_SAMPLER,65536,&line);
 check(lh==S_OK && line.Resource==&created_resource && line.GpuVa==0 && line.Bytes==65536,"1D native resource succeeds with texture GPUVA0");
 HRESULT vh=AgxD3d12OwnerCreateTexture3D(&owner,42,4,4,8,2,AGX_D3D12_BIND_SAMPLER,131072,&volume);
 check(vh==S_OK && volume.Resource==&created_resource && volume.GpuVa==0 && volume.Bytes==131072,"3D native resource succeeds with texture GPUVA0");
 check(release_calls==0,"successful native textures retained");
 check(map_calls==0,"actual buffer helper never maps nonbuffer");
 check(AgxWin32AsahiBufferGpuAddress(nullptr)==0,"null buffer helper still zero");
 struct MockBo bo={0x12340000};struct agx_resource buffer={};buffer.base.target=PIPE_BUFFER;buffer.bo=&bo;
 check(AgxWin32AsahiBufferGpuAddress(&buffer.base)==bo.gpu && map_calls==1,"buffer-only positive control unchanged");
 fail_create=true;AGX_D3D12_BUFFER failed={};
 check(AgxD3d12OwnerCreateTexture1D(&owner,42,4,2,AGX_D3D12_BIND_SAMPLER,65536,&failed)==E_OUTOFMEMORY && !failed.Resource,"1D actual factory failure still OOM");
 check(AgxD3d12OwnerCreateTexture3D(&owner,42,4,4,8,2,AGX_D3D12_BIND_SAMPLER,131072,&failed)==E_OUTOFMEMORY && !failed.Resource,"3D actual factory failure still OOM");
 fail_create=false;unsigned before=create_calls;query_ok=0;
 check(AgxD3d12OwnerCreateTexture1D(&owner,42,4,2,AGX_D3D12_BIND_SAMPLER,65536,&failed)==E_NOTIMPL && !failed.Resource,"1D layout refusal retained");
 check(AgxD3d12OwnerCreateTexture3D(&owner,42,4,4,8,2,AGX_D3D12_BIND_SAMPLER,131072,&failed)==E_NOTIMPL && !failed.Resource,"3D layout refusal retained");
 check(create_calls==before,"query refusal does not allocate");
 printf("TEXTURE_CREATION checks=%u failures=%u line_hr=%08x volume_hr=%08x RESOURCE_NOT_BUFFER_VA=YES\n",checks,failures,(unsigned)lh,(unsigned)vh);
 return failures?1:0;
}
