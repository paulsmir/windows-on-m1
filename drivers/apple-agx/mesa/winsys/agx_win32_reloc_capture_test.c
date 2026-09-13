#include "agx_win32_reloc_capture.h"
#include "agx_win32_native_pool_bridge.h"
#include "apple_agx_dynamic_job.h"
#include "agx_pack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  AGX_WIN32_RELOC_ALLOCATION Bo[9];
  unsigned Holds[9], Releases, FailRetain, FailRetainExact;
  unsigned char Data[9][0x10000];
  APPLE_AGX_U64 Placement;
  APPLE_AGX_U64 PreshaderOverride;
} FIXTURE;
static FIXTURE fixture;
static int query(void *ctx,APPLE_AGX_U64 token,AGX_WIN32_RELOC_ALLOCATION *bo) {
  FIXTURE *f=ctx; if(!token || token>9) return 0; *bo=f->Bo[token-1]; return 1;
}
static int retain(void *ctx,APPLE_AGX_U64 token,APPLE_AGX_U64 serial) {
  FIXTURE *f=ctx; if(f->FailRetain || !token || token>9 || f->Bo[token-1].Serial!=serial) return 0;
  ++f->Holds[token-1]; return 1;
}
static int retain_exact(void *ctx,const AGX_WIN32_RELOC_ALLOCATION *expected) {
  FIXTURE *f=ctx;
  if(!expected || f->FailRetainExact || !expected->Token || expected->Token>9 ||
      memcmp(&f->Bo[expected->Token-1],expected,sizeof(*expected))) return 0;
  ++f->Holds[expected->Token-1]; return 1;
}
static void release(void *ctx,APPLE_AGX_U64 token,APPLE_AGX_U64 serial) {
  FIXTURE *f=ctx; assert(token && token<=9 && serial && f->Holds[token-1]);
  --f->Holds[token-1]; ++f->Releases;
}
static int read_object(void *ctx,APPLE_AGX_U64 token,APPLE_AGX_U32 ref,
    APPLE_AGX_U32 role,APPLE_AGX_U64 offset,APPLE_AGX_U32 bytes,void *out) {
  FIXTURE *f=ctx; (void)role;
  if(!token || token>9 || (ref!=token-1 && !(ref==9 && token==5 && offset>=0x4000)) || offset>0x10000 || bytes>0x10000-offset) return 0;
  memcpy(out,f->Data[token-1]+offset,bytes); return 1;
}
static int resolve_object(void *ctx,APPLE_AGX_U64 token,APPLE_AGX_U32 cls,
    APPLE_AGX_U32 ref,APPLE_AGX_U32 role,APPLE_AGX_U64 offset,APPLE_AGX_U32 bytes,
    APPLE_AGX_U64 *out) {
  FIXTURE *f=ctx; (void)cls; (void)role;
  if(!token || token>9 || (ref!=token-1 && !(ref==9 && token==5 && offset>=0x4000)) || bytes!=1 || offset>=0x10000) return 0;
  *out=(f->PreshaderOverride && token==4 && offset==0x80) ?
      f->PreshaderOverride : f->Placement+token*0x10000+offset; return 1;
}
static APPLE_AGX_U64 read_le(const unsigned char *b,unsigned n) {
  APPLE_AGX_U64 v=0; for(unsigned i=0;i<n;++i) v|=(APPLE_AGX_U64)b[i]<<(8*i); return v;
}
static const AGX_WIN32_RELOC_OPERATIONS ops={query,retain,release,retain_exact};
static void setup(void) {
  memset(&fixture,0,sizeof(fixture)); fixture.Placement=0x1100000000ULL;
  for(unsigned i=0;i<9;++i) {
    fixture.Bo[i]=(AGX_WIN32_RELOC_ALLOCATION){77,i+1,100+i,0x10000,7,i,7};
    memset(fixture.Data[i],0xa0+i,sizeof(fixture.Data[i]));
  }
  /* Real pinned Mesa emitters: valid packed control bits with zero address
   * placeholders. Relocation metadata is captured at these known fields. */
  struct AGX_USC_SHADER shader={.loads_varyings=true,.unk_2=3};
  uint32_t encoded[2];
  AGX_USC_SHADER_pack(encoded,&shader); memcpy(fixture.Data[4],encoded,6);
  shader.loads_varyings=false;
  AGX_USC_SHADER_pack(encoded,&shader); memcpy(fixture.Data[4]+8,encoded,6);
  struct AGX_USC_UNIFORM uniform={.start_halfs=24,.size_halfs=48,.buffer=0};
  AGX_USC_UNIFORM_pack(encoded,&uniform); memcpy(fixture.Data[4]+16,encoded,8);
}
static APPLE_AGX_WIN32_DRAW_PAYLOAD capture(AGX_WIN32_RELOC_CAPTURE *c) {
  static const unsigned roles[9]={1,2,6,6,9,7,11,12,10};
  unsigned index;
  for(unsigned i=0;i<9;++i) {
    unsigned access=i==0?2:((i==2 || i==3)?5:1);
    assert(AgxWin32RelocReference(c,i+1,roles[i],access,0,0x4000,&index)==AgxRelocOk);
    assert(index==i);
  }
  assert(AgxWin32RelocField(c,AppleAgxWin32RelocationEncoderAddress,8,0,0,0)==AgxRelocOk);
  assert(AgxWin32RelocField(c,AppleAgxWin32RelocationEncoderAddress,8,8,1,0)==AgxRelocOk);
  assert(AgxWin32RelocField(c,AppleAgxWin32RelocationVdmPipelineOffset32,8,16,4,0)==AgxRelocOk);
  assert(AgxWin32RelocField(c,AppleAgxWin32RelocationUscShaderOffset32,4,0,2,0)==AgxRelocOk);
  assert(AgxWin32RelocField(c,AppleAgxWin32RelocationUscShaderOffset32,4,8,3,0)==AgxRelocOk);
  assert(AgxWin32RelocField(c,AppleAgxWin32RelocationUscBufferAddress40,4,16,5,0)==AgxRelocOk);
  APPLE_AGX_WIN32_DRAW_PAYLOAD d={0};
  d.Format=1; d.SurfaceWidth=16; d.SurfaceHeight=16; d.SurfacePitch=64;
  d.Topology=1; d.VertexCount=3; d.InstanceCount=1;
  d.DestinationReference=0; d.VertexReference=1; d.VertexShaderReference=2;
  d.FragmentShaderReference=3; d.UscPipelineReference=4; d.DescriptorReference=5;
  d.ScissorReference=6; d.DepthBiasReference=7; d.EncoderReference=8;
  d.IndexReference=d.ConstantReference=d.TextureReference=d.VertexRodataReference=
      d.FragmentRodataReference=APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  return d;
}
/* Catches using SHADER/UNIFORM bit layouts for native PRESHADER/table records. */
static void native_usc_fields(void) {
  setup(); AGX_WIN32_RELOC_CAPTURE c={0}; unsigned index,bytes=0;
  assert(AgxWin32RelocBegin(&c,77,7,1,&ops,&fixture)==AgxRelocOk);
  APPLE_AGX_WIN32_DRAW_PAYLOAD d=capture(&c);
  assert(AgxWin32RelocReference(&c,5,AppleAgxWin32RoleUscPipeline,1,
                               0x4000,0x340,&index)==AgxRelocOk && index==9);
  d.Reserved[0]=9;
  assert(AgxWin32RelocField(&c,AppleAgxWin32RelocationVdmPipelineOffset32,
                           8,20,9,0)==AgxRelocOk);
  assert(AgxWin32RelocField(&c,8u,9,2,3,0x80)==AgxRelocOk);
  assert(AgxWin32RelocField(&c,9u,9,10,5,0x100)==AgxRelocOk);
  assert(AgxWin32RelocField(&c,9u,9,18,5,0x180)==AgxRelocOk);
  APPLE_AGX_U64 command[512]={0}; APPLE_AGX_WIN32_COMMAND_VIEW view;
  assert(AgxWin32RelocSealVersion(&c,3u,&d,command,sizeof(command),&bytes)==AgxRelocOk);
  assert(AppleAgxWin32CommandValidate(command,bytes,7,9,&view)==AppleAgxWin32AbiSuccess);
  ADMISSION_WIN32_ALLOCATION_FACT facts[10]={0};
  for(unsigned i=0;i<9;++i) {facts[i].AllocationToken=i+1;facts[i].Bytes=0x10000;}
  facts[9]=facts[4];
  static unsigned char image[0x40000]; APPLE_AGX_DYNAMIC_JOB job;
  for(unsigned placement=0;placement<2;++placement) {
    fixture.Placement=0x1100000000ULL+placement*0x100000;
    for(unsigned count=0;count<128;++count) {
      uint32_t packed[2];
      struct AGX_USC_PRESHADER pre={.code=0x76543210};
      struct AGX_USC_TEXTURE tex={.start=23,.count=count,.buffer=0x12345678};
      struct AGX_USC_SAMPLER sampler={.start=17,.count=count,.buffer=0x23456788};
      AGX_USC_PRESHADER_pack(packed,&pre); memcpy(fixture.Data[4]+0x4002,packed,8);
      AGX_USC_TEXTURE_pack(packed,&tex); memcpy(fixture.Data[4]+0x400a,packed,8);
      AGX_USC_SAMPLER_pack(packed,&sampler); memcpy(fixture.Data[4]+0x4012,packed,8);
      assert(AppleAgxDynamicJobMaterialize(&view,facts,10,0x1100000000ULL,read_object,
          resolve_object,&fixture,image,sizeof(image),&job)==AppleAgxDynamicJobSuccess);
      unsigned found=0;
      for(unsigned i=0;i<job.ObjectCount;++i) if(job.Objects[i].ReferenceIndex==9) {
        unsigned char *out=image+job.Objects[i].StorageOffset; ++found;
        struct AGX_USC_PRESHADER p; struct AGX_USC_TEXTURE t; struct AGX_USC_SAMPLER s;
        /* Native decoder requires uint32 alignment; packed stream does not. */
        memcpy(packed,out+2,8); assert(AGX_USC_PRESHADER_unpack(stderr,(void *)packed,&p));
        memcpy(packed,out+10,8); assert(AGX_USC_TEXTURE_unpack(stderr,(void *)packed,&t));
        memcpy(packed,out+18,8); assert(AGX_USC_SAMPLER_unpack(stderr,(void *)packed,&s));
        assert(p.code==0x40080u+placement*0x100000u);
        assert(t.buffer==0x1100060100ULL+placement*0x100000 && t.start==23 && t.count==count);
        assert(s.buffer==0x1100060180ULL+placement*0x100000 && s.start==17 && s.count==count);
        pre.code=p.code; AGX_USC_PRESHADER_pack(packed,&pre); assert(!memcmp(packed,out+2,8));
        tex.buffer=t.buffer; AGX_USC_TEXTURE_pack(packed,&tex); assert(!memcmp(packed,out+10,8));
        sampler.buffer=s.buffer; AGX_USC_SAMPLER_pack(packed,&sampler); assert(!memcmp(packed,out+18,8));
        assert(out[0]==0xa4 && out[1]==0xa4 && out[26]==0xa4);
      }
      assert(found==1);
      /* Materialization never alters the native source stream. */
      assert(read_le(fixture.Data[4]+0x4002,8)>>32==0x76543210);
    }
  }
  APPLE_AGX_WIN32_COMMAND_HEADER *h=(void *)command;
  APPLE_AGX_WIN32_RELOCATION *r=(void *)view.Relocations;
  for(unsigned version=1;version<=2;++version) {
    h->Version=version;
    ((APPLE_AGX_WIN32_DRAW_PAYLOAD *)view.Draw)->Reserved[0]=(version==1 ? 0 : 9);
    h->ContentHash=AppleAgxWin32CommandHash(command,bytes);
    APPLE_AGX_WIN32_COMMAND_VIEW invalid;
    assert(AppleAgxWin32CommandValidate(command,bytes,7,9,&invalid)==AppleAgxWin32AbiRelocation);
  }
  h->Version=3; ((APPLE_AGX_WIN32_DRAW_PAYLOAD *)view.Draw)->Reserved[0]=9;
  for(unsigned which=7;which<10;++which) {
    APPLE_AGX_WIN32_RELOCATION saved=r[which];
    r[which].WidthBytes=6;
    h->ContentHash=AppleAgxWin32CommandHash(command,bytes);
    APPLE_AGX_WIN32_COMMAND_VIEW invalid;
    assert(AppleAgxWin32CommandValidate(command,bytes,7,9,&invalid)==AppleAgxWin32AbiRelocation);
    r[which]=saved; r[which].TargetReference=0;
    h->ContentHash=AppleAgxWin32CommandHash(command,bytes);
    assert(AppleAgxWin32CommandValidate(command,bytes,7,9,&invalid)==AppleAgxWin32AbiRelocation);
    r[which]=saved;
  }
  h->ContentHash=AppleAgxWin32CommandHash(command,bytes);
  assert(AppleAgxWin32CommandValidate(command,bytes,7,9,&view)==AppleAgxWin32AbiSuccess);
  r[8].TargetOffset=0x104; /* Valid range, invalid native table address alignment. */
  assert(AppleAgxDynamicJobMaterialize(&view,facts,10,0x1100000000ULL,read_object,
      resolve_object,&fixture,image,sizeof(image),&job)==AppleAgxDynamicJobRelocation);
  r[8].TargetOffset=0x100;
  fixture.Placement=0x8000000000ULL;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,10,fixture.Placement,read_object,
      resolve_object,&fixture,image,sizeof(image),&job)==AppleAgxDynamicJobRelocation);
  fixture.Placement=0x1100000000ULL;
  /* Isolate preshader underflow/overflow from earlier shader relocations. */
  fixture.PreshaderOverride=0x10ffffffffULL;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,10,0x1100000000ULL,read_object,
      resolve_object,&fixture,image,sizeof(image),&job)==AppleAgxDynamicJobRelocation);
  fixture.PreshaderOverride=0x1200000000ULL;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,10,0x1100000000ULL,read_object,
      resolve_object,&fixture,image,sizeof(image),&job)==AppleAgxDynamicJobRelocation);
  fixture.PreshaderOverride=0;
  /* Reserved bit63 is not an address bit. No native unpack of this intentionally
   * reserved pattern; assert exact preservation without claiming a legal USC. */
  fixture.Data[4][0x4011]|=0x80;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,10,0x1100000000ULL,read_object,
      resolve_object,&fixture,image,sizeof(image),&job)==AppleAgxDynamicJobSuccess);
  for(unsigned i=0;i<job.ObjectCount;++i) if(job.Objects[i].ReferenceIndex==9)
    assert(image[job.Objects[i].StorageOffset+17]&0x80);
  assert(AgxWin32RelocAbort(&c)==AgxRelocOk);
  for(unsigned i=0;i<9;++i) assert(!fixture.Holds[i]);
  puts("NATIVE USC v3: preshader/texture/sampler two placements, all counts PASS");
}
int main(void) {
  native_usc_fields();
  setup(); AGX_WIN32_RELOC_CAPTURE c={0};
  assert(AgxWin32RelocBegin(&c,77,7,1,&ops,&fixture)==AgxRelocOk);
  APPLE_AGX_WIN32_DRAW_PAYLOAD d=capture(&c);
  assert(AgxWin32RelocField(&c,AppleAgxWin32RelocationEncoderAddress,8,4,0,0)==AgxRelocOverlap);
  assert(AgxWin32RelocField(&c,AppleAgxWin32RelocationEncoderAddress,8,0x3ffc,0,0)==AgxRelocRange);
  APPLE_AGX_U64 command[512]={0}; unsigned bytes=0;
  assert(AgxWin32RelocSeal(&c,&d,command,sizeof(command),&bytes)==AgxRelocOk);
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  assert(AppleAgxWin32CommandValidate(command,bytes,7,9,&view)==AppleAgxWin32AbiSuccess);
  ADMISSION_WIN32_ALLOCATION_FACT facts[9]={0};
  for(unsigned i=0;i<9;++i) {facts[i].AllocationToken=i+1; facts[i].Bytes=0x10000;}
  APPLE_AGX_DYNAMIC_JOB a,b; static unsigned char imageA[0x40000], imageB[0x40000], saved[0x40000];
  assert(AppleAgxDynamicJobMaterialize(&view,facts,9,0x1100000000ULL,read_object,
      resolve_object,&fixture,imageA,sizeof(imageA),&a)==AppleAgxDynamicJobSuccess);
  memcpy(saved,imageA,a.StorageBytes); fixture.Placement+=0x100000;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,9,0x1100000000ULL,read_object,
      resolve_object,&fixture,imageB,sizeof(imageB),&b)==AppleAgxDynamicJobSuccess);
  assert(a.ObjectCount==b.ObjectCount && a.RelocationCount==6);
  for(unsigned i=0;i<6;++i) assert(b.Relocations[i].ResolvedAddress==a.Relocations[i].ResolvedAddress+0x100000);
  assert(memcmp(imageA,saved,a.StorageBytes)==0);
  for(unsigned i=0;i<a.ObjectCount;++i) {
    unsigned ref=a.Objects[i].ReferenceIndex, base=a.Objects[i].StorageOffset;
    for(unsigned j=0;j<a.Objects[i].Bytes;++j) {
      int patched=0;
      for(unsigned k=0;k<6;++k) if(view.Relocations[k].DestinationReference==ref &&
          j>=view.Relocations[k].DestinationOffset &&
          j-view.Relocations[k].DestinationOffset<view.Relocations[k].WidthBytes) patched=1;
      if(!patched) assert(imageA[base+j]==fixture.Data[ref][j] && imageB[base+j]==fixture.Data[ref][j]);
    }
    if(ref==4) {
      assert(read_le(imageA+base,6)>>16==0x30000);
      assert(read_le(imageB+base,6)>>16==0x130000);
      struct AGX_USC_UNIFORM uniformA,uniformB;
      assert(AGX_USC_UNIFORM_unpack(stderr,imageA+base+16,&uniformA));
      assert(AGX_USC_UNIFORM_unpack(stderr,imageB+base+16,&uniformB));
      assert(uniformA.buffer==0x1100060000ULL && uniformB.buffer==0x1100160000ULL);
      assert(uniformA.start_halfs==24 && uniformA.size_halfs==48);
      assert(uniformB.start_halfs==24 && uniformB.size_halfs==48);
      struct AGX_USC_UNIFORM expected={.start_halfs=24,.size_halfs=48,.buffer=0x1100060000ULL};
      uint32_t packed[2]; AGX_USC_UNIFORM_pack(packed,&expected);
      assert(memcmp(packed,imageA+base+16,8)==0);
    }
  }
  for(unsigned count=1;count<=64;++count) {
    struct AGX_USC_UNIFORM native={.start_halfs=24,.size_halfs=count,.buffer=0};
    uint32_t packed[2]; AGX_USC_UNIFORM_pack(packed,&native);
    memcpy(fixture.Data[4]+16,packed,8);
    assert(AppleAgxDynamicJobMaterialize(&view,facts,9,0x1100000000ULL,read_object,
        resolve_object,&fixture,imageB,sizeof(imageB),&b)==AppleAgxDynamicJobSuccess);
    for(unsigned i=0;i<b.ObjectCount;++i) if(b.Objects[i].ReferenceIndex==4) {
      native.buffer=0x1100160000ULL; AGX_USC_UNIFORM_pack(packed,&native);
      assert(memcmp(packed,imageB+b.Objects[i].StorageOffset+16,8)==0);
    }
    assert(memcmp(saved,imageA,a.StorageBytes)==0);
  }
  assert(AgxWin32RelocSubmitted(&c,256)==AgxRelocOk);
  assert(AgxWin32RelocAbort(&c)==AgxRelocState && fixture.Releases==0);
  assert(AgxWin32RelocRetire(&c,77,7,1,255)==AgxRelocStale);
  assert(AgxWin32RelocRetire(&c,78,7,1,256)==AgxRelocStale);
  assert(AgxWin32RelocRetire(&c,77,7,1,256)==AgxRelocOk && fixture.Releases==9);
  assert(AgxWin32RelocRetire(&c,77,7,1,256)!=AgxRelocOk);
  assert(AgxWin32RelocBegin(&c,77,7,1,&ops,&fixture)==AgxRelocStale);
  assert(AgxWin32RelocBegin(&c,77,7,2,&ops,&fixture)==AgxRelocOk);
  d=capture(&c); ++fixture.Bo[2].Serial;
  assert(AgxWin32RelocSeal(&c,&d,command,sizeof(command),&bytes)==AgxRelocStale);
  assert(AgxWin32RelocAbort(&c)==AgxRelocOk && fixture.Releases==18);
  assert(AgxWin32RelocBegin(&c,77,7,3,&ops,&fixture)==AgxRelocOk);
  fixture.Bo[0].Owner=78; unsigned index;
  assert(AgxWin32RelocReference(&c,1,1,2,0,16,&index)==AgxRelocStale);
  fixture.Bo[0].Owner=77; fixture.FailRetain=1;
  assert(AgxWin32RelocReference(&c,1,1,2,0,16,&index)==AgxRelocCallback);
  assert(c.ReferenceCount==0); assert(AgxWin32RelocAbort(&c)==AgxRelocOk);
  fixture.FailRetain=0;
  assert(AgxWin32RelocBegin(&c,77,7,4,&ops,&fixture)==AgxRelocOk);
  d=capture(&c);
  assert(AgxWin32RelocSeal(&c,&d,command,sizeof(command),&bytes)==AgxRelocOk);
  assert(AgxWin32RelocSubmitted(&c,256)==AgxRelocOk);
  assert(AgxWin32RelocRetire(&c,77,7,1,256)==AgxRelocStale);
  assert(AgxWin32RelocRetire(&c,77,7,4,256)==AgxRelocOk);
  assert(AgxWin32RelocBegin(&c,77,7,5,&ops,&fixture)==AgxRelocOk);
  for(unsigned i=0;i<16;++i)
    assert(AgxWin32RelocReference(&c,1,7,1,i*16,16,&index)==AgxRelocOk);
  assert(AgxWin32RelocReference(&c,1,7,1,256,16,&index)==AgxRelocCapacity);
  assert(AgxWin32RelocAbort(&c)==AgxRelocOk);
  assert(AgxWin32RelocBegin(&c,77,7,6,&ops,&fixture)==AgxRelocOk);
  assert(AgxWin32RelocReference(&c,1,7,1,0,0x4000,&index)==AgxRelocOk);
  assert(AgxWin32RelocReference(&c,1,7,1,0x8000,0x4000,&index)==AgxRelocOk);
  for(unsigned i=0;i<64;++i)
    assert(AgxWin32RelocField(&c,1,0,i*8,1,0)==AgxRelocOk);
  assert(AgxWin32RelocField(&c,1,0,512,1,0)==AgxRelocCapacity);
  assert(AgxWin32RelocAbort(&c)==AgxRelocOk);
  assert(AgxWin32RelocBegin(&c,77,7,7,&c.Operations,c.Context)==AgxRelocOk);
  assert(AgxWin32RelocReference(&c,1,1,2,0,32,&index)==AgxRelocOk);
  assert(AgxWin32RelocAbort(&c)==AgxRelocOk);
  /* A native pool may place independently-sized vertex and fragment USC
   * streams in disjoint subranges of one retained BO.  v2 carries both
   * ranges; it must not recreate the v1 compact split. */
  assert(AgxWin32RelocBegin(&c,77,7,8,&c.Operations,c.Context)==AgxRelocOk);
  d=capture(&c);
  assert(AgxWin32RelocReference(&c,5,AppleAgxWin32RoleUscPipeline,1,
                                0x4000,0x340,&index)==AgxRelocOk);
  assert(index==9u);
  d.Reserved[APPLE_AGX_WIN32_DRAW_V2_FRAGMENT_USC_PIPELINE_RESERVED_INDEX]=index;
  assert(AgxWin32RelocField(&c,AppleAgxWin32RelocationVdmPipelineOffset32,
                            8,20,index,0)==AgxRelocOk);
  assert(AgxWin32RelocSealVersion(
      &c, APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_PIPELINES, &d, command,
      sizeof(command), &bytes)==AgxRelocOk);
  assert(AppleAgxWin32CommandValidate(command,bytes,7,9,&view)==
         AppleAgxWin32AbiSuccess);
  assert(view.Header->Version==APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_PIPELINES &&
         view.Draw->UscPipelineReference==4u &&
         view.Draw->Reserved[APPLE_AGX_WIN32_DRAW_V2_FRAGMENT_USC_PIPELINE_RESERVED_INDEX]==9u);
  assert(AgxWin32RelocAbort(&c)==AgxRelocOk);
  {
    AGX_WIN32_NATIVE_POOL_SLICE slice={
        77u,5u,104u,7u,fixture.Data[4],
        fixture.Placement+5u*0x10000u,0x10000u,
        fixture.Data[4]+0x200u,fixture.Placement+5u*0x10000u+0x200u,0x240u};
  assert(AgxWin32RelocBegin(&c,77,7,9,&c.Operations,c.Context)==AgxRelocOk);
    assert(AgxWin32NativePoolReference(&c,&slice,AppleAgxWin32RoleUscPipeline,
                                       AppleAgxWin32AccessRead,&index)==AgxRelocOk);
    assert(index==0u && c.References[index].Offset==0x200u &&
           c.References[index].Bytes==0x240u);
    slice.SliceConstruction++;
    assert(AgxWin32NativePoolReference(&c,&slice,AppleAgxWin32RoleUscPipeline,
                                       AppleAgxWin32AccessRead,&index)==AgxRelocRange);
    assert(AgxWin32RelocAbort(&c)==AgxRelocOk);
  }
  {
    AGX_WIN32_RELOC_ALLOCATION expected=fixture.Bo[4];
    assert(AgxWin32RelocBegin(&c,77,7,10,&c.Operations,c.Context)==AgxRelocOk);
    assert(AgxWin32RelocReferenceExpected(
        &c,&expected,AppleAgxWin32RoleUscPipeline,AppleAgxWin32AccessRead,
        0x200,0x240,&index)==AgxRelocOk);
    assert(index==0u && fixture.Holds[4]==1u);
    assert(AgxWin32RelocAbort(&c)==AgxRelocOk && fixture.Holds[4]==0u);
    ++fixture.Bo[4].Serial;
    assert(AgxWin32RelocBegin(&c,77,7,11,&c.Operations,c.Context)==AgxRelocOk);
    assert(AgxWin32RelocReferenceExpected(
        &c,&expected,AppleAgxWin32RoleUscPipeline,AppleAgxWin32AccessRead,
        0x200,0x240,&index)==AgxRelocStale);
    assert(c.ReferenceCount==0u && fixture.Holds[4]==0u);
    expected=fixture.Bo[4]; fixture.FailRetainExact=1;
    assert(AgxWin32RelocReferenceExpected(
        &c,&expected,AppleAgxWin32RoleUscPipeline,AppleAgxWin32AccessRead,
        0x200,0x240,&index)==AgxRelocCallback);
    assert(c.ReferenceCount==0u && fixture.Holds[4]==0u);
    fixture.FailRetainExact=0;
    assert(AgxWin32RelocAbort(&c)==AgxRelocOk);
  }
  for(unsigned i=0;i<9;++i) assert(fixture.Holds[i]==0);
  puts("CAPTURE -> WIRE -> KMD MATERIALIZER: two placements/lifetime PASS");
  return 0;
}
