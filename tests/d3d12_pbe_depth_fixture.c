/* Original CPU-only fixture. Includes unchanged actual source so static packers
 * execute directly; no copied native implementation and no BO/device creation.
 * Proposed/uncompiled; stop any header/link mismatch, no guessed shims. */
#include <assert.h>
#include <stdio.h>
#include "d3d12_pbe_depth_boundary.h"
#include C24_SELECTED_SOURCE
#include "util/u_helpers.h"
static unsigned pbe_checks,pbe_failures;
static void check_pbe(bool condition,const char *why){++pbe_checks;if(!condition){++pbe_failures;printf("PBE_FAIL %s\n",why);}}
static void check_3d(void){
 const enum pipe_format formats[]={PIPE_FORMAT_R32_UINT,PIPE_FORMAT_R8G8B8A8_UNORM};
 for(unsigned f=0;f<2;f++) {
  struct agx_va va={.addr=0x20000000ull};struct agx_bo bo={.va=&va};struct agx_resource tex={0};tex.bo=&bo;
  tex.base.target=PIPE_TEXTURE_3D;tex.base.format=formats[f];tex.base.width0=4;tex.base.height0=4;tex.base.depth0=8;tex.base.array_size=1;tex.base.last_level=1;tex.base.nr_samples=tex.base.nr_storage_samples=1;tex.mipmapped=true;
  tex.layout=(struct ail_layout){.format=formats[f],.width_px=4,.height_px=4,.depth_px=8,.sample_count_sa=1,.levels=2,.tiling=AIL_TILING_GPU,.mipmapped_z=true,.writeable_image=true,.renderable=true};ail_make_miptree(&tex.layout);
  for(unsigned level=0;level<2;level++) {
   /* Input DATA follows Gallium st_atom_image's layered3D selected-mip range.
    * This fixture executes native packers, not the D3D owner implementation. */
   struct pipe_image_view image={.resource=&tex.base,.format=formats[f],
     .access=PIPE_IMAGE_ACCESS_READ_WRITE,.shader_access=PIPE_IMAGE_ACCESS_READ_WRITE};
   image.u.tex.level=level;image.u.tex.first_layer=0;
   image.u.tex.last_layer=u_minify(tex.base.depth0,level)-1;
   struct pipe_sampler_view sampler=util_image_to_sampler_view(&image);struct agx_texture_packed read={0};struct agx_pbe_packed write={0};
   agx_pack_texture(&read,&tex,formats[f],&sampler);agx_batch_upload_pbe(NULL,&write,&image,false,false,false,false);
   agx_unpack(NULL,&read,TEXTURE,t);agx_unpack(NULL,&write,PBE,p);
   check_pbe(t.dimension==AGX_TEXTURE_DIMENSION_3D&&p.dimension==AGX_TEXTURE_DIMENSION_3D,"true3D paired dimensions");
   check_pbe(t.depth==8&&p.layers==8,"both descriptor halves encode base depth");
   check_pbe(t.first_level==level&&p.level==level&&t.width==4&&t.height==4&&p.width==4&&p.height==4,"selected mip separate from base dimensions");
   check_pbe(t.address==va.addr+tex.layout.level_offsets_B[0]&&p.buffer==t.address,"base address no double mip offset");
   check_pbe(p.level_offset_sw==tex.layout.level_offsets_B[level]&&p.layer_stride_sw==tex.layout.layer_stride_B,"software physical offsets/stride bytes");
   agx_batch_upload_pbe(NULL,&write,&image,false,false,true,false);agx_unpack(NULL,&write,PBE,forced);
   check_pbe(forced.dimension==AGX_TEXTURE_DIMENSION_2D_ARRAY&&forced.layers==(8u>>level),"forced2Darray retains selected layer count");
   /* A native single-plane 2D view must not inherit base3D depth. */
   struct pipe_image_view plane=image;plane.u.tex.first_layer=plane.u.tex.last_layer=1;plane.u.tex.single_layer_view=true;plane.u.tex.is_2d_view_of_3d=true;
   agx_batch_upload_pbe(NULL,&write,&plane,false,false,false,false);agx_unpack(NULL,&write,PBE,slice);
   check_pbe(slice.dimension==AGX_TEXTURE_DIMENSION_2D&&slice.layers==1,"single-Z normalized2D preserves one layer");
   check_pbe(slice.buffer==t.address+tex.layout.layer_stride_B,"single-Z plane address preserved");
  }
 }
}
int main(void)
{
 unsigned cases=0;
 struct agx_texture_packed old_read[2];struct agx_pbe_packed old_write[2];
 const enum pipe_format formats[]={PIPE_FORMAT_R32_UINT,PIPE_FORMAT_R8G8B8A8_UNORM};
 for(unsigned f=0;f<2;f++)
 for(unsigned backing_array=0;backing_array<2;backing_array++)
 for(unsigned layers=1;layers<=3;layers+=2) {
  if(!backing_array && layers!=1)continue;
  struct agx_va va={.addr=0x10000000ull}; /* CPU fixture only, never submitted */
  struct agx_bo bo={.va=&va};struct agx_resource tex={0};tex.bo=&bo;
  tex.base.target=backing_array?PIPE_TEXTURE_1D_ARRAY:PIPE_TEXTURE_1D;
  tex.base.format=formats[f];tex.base.width0=37;tex.base.height0=tex.base.depth0=1;
  tex.base.array_size=layers;tex.base.last_level=1;
  tex.base.nr_samples=tex.base.nr_storage_samples=1;tex.mipmapped=true;
  tex.layout=(struct ail_layout){.format=formats[f],.width_px=37,.height_px=1,
    .depth_px=layers,.sample_count_sa=1,.levels=2,.tiling=AIL_TILING_GPU,
    .writeable_image=true,.renderable=true};
  ail_make_miptree(&tex.layout);
  for(unsigned logical_array=0;logical_array<2;logical_array++) {
   if((!backing_array && logical_array) || (!logical_array && layers!=1))continue;
   for(unsigned first=0;first<layers;first++)for(unsigned count=1;count<=layers-first;count++) {
    struct pipe_image_view image={.resource=&tex.base,.format=formats[f],
      .access=PIPE_IMAGE_ACCESS_READ_WRITE,.shader_access=PIPE_IMAGE_ACCESS_READ_WRITE};
    image.u.tex.level=1;image.u.tex.first_layer=first;image.u.tex.last_layer=first+count-1;
    /* Explicit logical view policy. It is NOT inferred from count==1. */
    image.u.tex.single_layer_view=backing_array && !logical_array;
    struct pipe_sampler_view sampler=util_image_to_sampler_view(&image);
    struct agx_texture_packed read={0};struct agx_pbe_packed write={0};
    agx_pack_texture(&read,&tex,formats[f],&sampler);
    agx_batch_upload_pbe(NULL,&write,&image,false,false,false,false);
    agx_unpack(NULL,&read,TEXTURE,t);agx_unpack(NULL,&write,PBE,p);
    unsigned expected=logical_array?AGX_TEXTURE_DIMENSION_2D_ARRAY:AGX_TEXTURE_DIMENSION_2D;
    assert(t.dimension==expected && p.dimension==expected);
    assert(t.depth==count && p.layers==count && t.first_level==1 && p.level==1);
    assert(t.width==37 && t.height==1 && p.width==37 && p.height==1);
    assert(t.address==va.addr+(uint64_t)first*tex.layout.layer_stride_B);
    assert(p.buffer==t.address && p.layer_stride_sw==tex.layout.layer_stride_B);
    if(!backing_array){old_read[f]=read;old_write[f]=write;}
    else if(!logical_array) {
     /* Canonical array backing + explicit nonarray view preserves C23's
      * actual two descriptor halves bit-for-bit, not just their dimensions. */
     assert(!memcmp(&read,&old_read[f],sizeof(read)));
     assert(!memcmp(&write,&old_write[f],sizeof(write)));
    }
    printf("C24_PACK fmt=%u backing_array=%u logical_array=%u total=%u first=%u count=%u dim=%u depth=%u addr=%llx stride=%llu\n",
      formats[f],backing_array,logical_array,layers,first,count,t.dimension,t.depth,
      (unsigned long long)t.address,(unsigned long long)tex.layout.layer_stride_B);++cases;
   }
  }
 }
 check_pbe(cases==18,"all eighteen unchanged non3D controls");check_3d();
 printf("C1436_PBE_BASE_DEPTH checks=%u failures=%u\n",pbe_checks,pbe_failures);
 if(pbe_failures)return 1;
 printf("C24_ACTUAL_DESCRIPTOR_PACK_PASS cases=%u C23_NONARRAY_BYTE_IDENTICAL=YES CPU_ONLY_NO_SUBMISSION\n",cases);
}
