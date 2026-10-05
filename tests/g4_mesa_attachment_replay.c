#include "apple_agx_g4_submit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "g4_mesa_attachment_shim.h"
#include "g4_mesa_attachment_functions.inc"

static int mapped(void *ctx,unsigned long long va,unsigned bytes,int write) {
  (void)ctx;(void)write;return va && bytes;
}
/* R142: ordinal counts all access kinds, not render structure fields. */
static int reject_vdm(void *ctx,unsigned long long va,unsigned bytes,int write,
    APPLE_AGX_G4_ACCESS_KIND kind,unsigned ordinal) {
  (void)ctx;
  if(kind!=AppleAgxG4AccessRender) return 1;
  assert(ordinal==11 && bytes==1 && !write && va==0x900000);
  return 0;
}
static APPLE_AGX_G4_PARSE_RESULT parse(AGX_G4_PRIVATE *p,unsigned capacity,
    unsigned bytes,APPLE_AGX_G4_SUBMIT_VIEW *v) {
  return AppleAgxG4ParseSubmit(p,capacity,sizeof(p->Header)+bytes,
      p->Header.V2.Base.CommandVa,bytes,mapped,NULL,v);
}
static void envelope(unsigned width,unsigned height,unsigned long long va) {
  AGX_G4_PRIVATE p={0};
  struct agx_resource color={.bo=&color,.va=0x800000,
      .layout={.size_B=(uint64_t)width*height*4+4096,.level_offsets_B={4096}}};
  struct agx_batch b={.key={.nr_cbufs=1,.cbufs={{.texture=&color}}}};
  APPLE_AGX_G4_NATIVE_RENDER r={.WidthPx=width,.HeightPx=height,
      .Layers=1,.Samples=1,.SampleSizeBytes=8,.UtileWidthPx=16,
      .UtileHeightPx=16,.Flags=1u<<2,.VdmCtrlStreamBase=0x900000};
  APPLE_AGX_G4_NATIVE_HEADER h={.Type=APPLE_AGX_G4_RENDER,.Size=sizeof(r)};
  APPLE_AGX_G4_PROCESS_RANGE ranges[9]={0};
  unsigned required[9];assert(AppleAgxG4ProcessRequiredBytes(&r,required));
  unsigned long long next=0x2000000;
  for(unsigned i=0;i<9;++i) {
    ranges[i]=(APPLE_AGX_G4_PROCESS_RANGE){next,required[i],0};next+=required[i];
  }
  APPLE_AGX_G4_PRIVATE_LEASE lease={5,1,7,2};
  assert(append_attachments(&b,&p));
  assert(append_native(&p,&h,sizeof(h)) && append_native(&p,&r,sizeof(r)));
  unsigned bytes=p.Header.V2.Base.CommandBytes;
  assert(bytes==280 && sizeof(p.Header)==200);
  assert(AppleAgxG4ComposeHeaderV3(&p.Header,&r,va,bytes,1,ranges,&lease));
  APPLE_AGX_G4_SUBMIT_VIEW view={0};
  unsigned capacity=331776; /* EXP855D capacity, distinct from 480 UMD bytes. */
  int result=parse(&p,capacity,bytes,&view);
  fprintf(stderr,"R141 VA=%llx DMA=%u UMD=%zu capacity=%u parse=%d\n",va,bytes,sizeof(p.Header)+bytes,capacity,result);
  assert(result==AppleAgxG4ParseOk);
  assert(view.AttachmentCount==1 && view.Attachments[0].Pointer==color.va);
  assert(view.Attachments[0].Size==(uint64_t)width*height*4);
  assert(!view.Attachments[0].Pad && !view.Attachments[0].Flags);
  APPLE_AGX_G4_FAILURE failure={0};
  assert(AppleAgxG4ParseSubmitEx(&p,capacity,sizeof(p.Header)+bytes,
      va,bytes,reject_vdm,NULL,&view,&failure)==AppleAgxG4ParseUnmapped);
  assert(failure.Kind==AppleAgxG4AccessRender && failure.Ordinal==11 &&
      failure.Va==r.VdmCtrlStreamBase && failure.Bytes==1 && !failure.Write);
  /* The old field-index reading would incorrectly name Stencil.CompBase. */
  assert(failure.Va!=r.Stencil.CompBase);
  AGX_G4_PRIVATE bad=p;
  /* Each bad field is independently refused, not hidden by a different fault. */
  ((APPLE_AGX_G4_NATIVE_HEADER *)bad.Native)->VdmBarrier=0;
  assert(parse(&bad,capacity,bytes,&view)==AppleAgxG4ParseInvalid);
  bad=p;((APPLE_AGX_G4_NATIVE_HEADER *)bad.Native)->CdmBarrier=0;
  assert(parse(&bad,capacity,bytes,&view)==AppleAgxG4ParseInvalid);
  bad=p;((APPLE_AGX_G4_ATTACHMENT *)(bad.Native+8))->Pad=1;
  assert(parse(&bad,capacity,bytes,&view)==AppleAgxG4ParseInvalid);
  bad=p;((APPLE_AGX_G4_ATTACHMENT *)(bad.Native+8))->Flags=1;
  assert(parse(&bad,capacity,bytes,&view)==AppleAgxG4ParseInvalid);
  bad=p;bad.Header.Lease.SceneGeneration=0;
  assert(parse(&bad,capacity,bytes,&view)==AppleAgxG4ParseInvalid);
  bad=p;bad.Header.V2.Process[0].Reserved=1;
  assert(parse(&bad,capacity,bytes,&view)==AppleAgxG4ParseInvalid);
  assert(parse(&p,479,bytes,&view)==AppleAgxG4ParseInvalid);
}
int main(void) {
  envelope(2560,1600,0x3b0000);envelope(137,59,0x650000);
  /* Shared serializer also initializes depth/stencil MBZ fields. */
  AGX_G4_PRIVATE p={0};struct agx_resource s={.bo=&s,.va=0x80000,.layout={.size_B=8192}};
  struct agx_resource d={.bo=&d,.va=0x70000,.layout={.size_B=16384},.separate_stencil=&s};
  struct agx_batch b={.key={.zsbuf={.texture=&d}}};
  assert(append_attachments(&b,&p));
  APPLE_AGX_G4_NATIVE_HEADER *h=(void *)p.Native;
  APPLE_AGX_G4_ATTACHMENT *a=(void *)(p.Native+sizeof(*h));
  assert(h->Size==48 && h->VdmBarrier==0xffff && h->CdmBarrier==0xffff);
  assert(a[0].Pointer==d.va && a[1].Pointer==s.va);
  assert(!a[0].Pad && !a[0].Flags && !a[1].Pad && !a[1].Flags);
  memset(&p,0,sizeof(p));memset(&b,0,sizeof(b));
  assert(append_attachments(&b,&p) && !p.Header.V2.Base.CommandBytes);
  puts("R141 production attachment serialization: PASS");return 0;
}
