/* Original test-only actual native layout/constructor adapters. SPDX-License-Identifier: MIT */
unsigned c23_native_shape(const struct pipe_resource *t,const AGX_WIN32_ASAHI_RESOURCE_LAYOUT *raw) {
 struct agx_resource resource={0};if(!agx_resource_layout_init(NULL,t,&raw->Modifier,1,&resource,true))return 0;
 const struct ail_layout *a=&resource.layout;
 if(a->size_B!=raw->SizeBytes || a->layer_stride_B!=raw->LayerStrideBytes)return 0;
 for(unsigned l=0;l<=t->last_level;l++) {
  unsigned w=t->width0>>l;if(!w)w=1;
  if(a->tilesize_el[l].width_el!=1 || a->tilesize_el[l].height_el!=1 || a->level_offsets_B[l]!=raw->LevelOffsets[l])return 0;
  if(t->width0<=3 || t->width0==37 || t->width0==65 || t->width0==16384) {
   unsigned char *tile=malloc(a->size_B),*linear=malloc(w*4);if(!tile||!linear)abort();
   memset(tile,0xa5,a->size_B);for(unsigned x=0;x<w*4;x++)linear[x]=(unsigned char)(x*37u+l*19u);
   ail_tile(tile+a->level_offsets_B[l],linear,a,l,w*4,0,0,w,1);
   if(memcmp(tile+a->level_offsets_B[l],linear,w*4))abort();
   for(uint64_t b=0;b<a->size_B;b++)if((b<a->level_offsets_B[l]||b>=a->level_offsets_B[l]+w*4)&&tile[b]!=0xa5)abort();
   free(tile);free(linear);
  }
 }
 return 1;
}

struct pipe_screen *c23_native_screen(void) {
 static struct agx_screen screen;screen.base.is_format_supported=agx_is_format_supported;
 screen.base.resource_create_with_modifiers=agx_resource_create_with_modifiers;return &screen.base;
}
void c23_native_release(struct pipe_resource *p){struct agx_resource *r=(struct agx_resource*)p;if(r){free(r->bo);free(r);}}
