/* Original observer/oracle, actual constructor and AIL functions above. BO factory is CPU counting stand-in. */
void c24_check_native(struct pipe_resource*p,const AGX_WIN32_ASAHI_RESOURCE_LAYOUT*q){
 struct agx_resource*r=(struct agx_resource*)p;const struct ail_layout*a=&r->layout;
 assert(p->target==PIPE_TEXTURE_1D_ARRAY&&a->writeable_image&&a->page_aligned_layers&&!a->mipmapped_z&&!a->compressed);
 assert(a->size_B==q->SizeBytes&&a->layer_stride_B==q->LayerStrideBytes&&a->size_B==a->layer_stride_B*p->array_size);
 assert(!a->metadata_offset_B&&!a->compression_layer_stride_B&&(r->base.bind&PIPE_BIND_SHADER_IMAGE));
 assert(r->bo&&r->bo->size==a->size_B&&r->bo->rounded_size==((a->size_B+65535)&~65535ull));
 for(unsigned m=0;m<=p->last_level;m++)assert(a->level_offsets_B[m]==q->LevelOffsets[m]&&a->tilesize_el[m].width_el==1&&a->tilesize_el[m].height_el==1);
 if(p->width0==37&&p->array_size<=3){
  unsigned char*tile=malloc(a->size_B),*expected=malloc(a->size_B);assert(tile&&expected);memset(tile,0xa5,a->size_B);memset(expected,0xa5,a->size_B);
  for(unsigned s=0;s<p->array_size;s++)for(unsigned m=0;m<=p->last_level;m++){
   unsigned w=p->width0>>m;if(!w)w=1;uint32_t*linear=malloc(w*4),*back=calloc(w,4);assert(linear&&back);
   for(unsigned x=0;x<w;x++)linear[x]=0x24000000u+(s<<20)+(m<<16)+x;
   uint64_t off=s*a->layer_stride_B+a->level_offsets_B[m];memcpy(expected+off,linear,w*4);
   ail_tile(tile+off,linear,a,m,w*4,0,0,w,1);ail_detile(tile+off,back,a,m,w*4,0,0,w,1);
   assert(!memcmp(linear,back,w*4));free(linear);free(back);
  }
  assert(!memcmp(tile,expected,a->size_B));free(tile);free(expected);
 }
}
