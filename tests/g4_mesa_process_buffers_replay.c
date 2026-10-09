#include "apple_agx_g3_private_abi.h"
#include "apple_agx_g3_private_storage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* OS callback boundary only. The producer body and kernel storage constructor
 * are real; no ordinary Allocate/Lock operation is available in this fixture. */
typedef struct {
  struct { struct { int (*PrivateEscape)(void *,APPLE_AGX_G3_PRIVATE_REQUEST *); } Ops; void *Context; } Gpuva;
} AGX_WIN32_ASAHI_BACKEND;
typedef struct { APPLE_AGX_G4_PRIVATE_LEASE Lease; } AGX_G4_BATCH;
typedef struct {
  APPLE_AGX_G3_PRIVATE_POOL Pool;
  APPLE_AGX_G3_PRIVATE_MANAGER Manager;
  APPLE_AGX_G3_PRIVATE_SCENE Scenes[3];
  unsigned Count, Calls;
  unsigned char *Cpu;
} KERNEL;
static int escape(void *opaque, APPLE_AGX_G3_PRIVATE_REQUEST *q) {
  KERNEL *k=opaque; APPLE_AGX_G4_NATIVE_RENDER r={0};
  assert(q->Magic==APPLE_AGX_G3_PRIVATE_MAGIC && q->Version==1 && q->Bytes==sizeof(*q));
  assert(q->Operation==APPLE_AGX_G3_PRIVATE_ACQUIRE && k->Count<3);
  ++k->Calls;r.WidthPx=q->Width;r.HeightPx=q->Height;r.Layers=q->Layers;
  r.UtileWidthPx=q->UtileWidth;r.UtileHeightPx=q->UtileHeight;r.Samples=q->Samples;
  APPLE_AGX_G3_PRIVATE_SCENE *s=&k->Scenes[k->Count];
  if(!AppleAgxG3PrivatePrepare(&k->Pool,17,k->Cpu,1ULL<<36,&r,&k->Manager,s)) return 0;
  ++k->Count;q->ManagerId=17;q->ManagerGeneration=k->Manager.Generation;
  q->SceneId=q->SceneGeneration=s->Generation;memcpy(q->Ranges,s->Ranges,sizeof(q->Ranges));
  return 1;
}
#include "g4_mesa_process_buffers_function.inc"
int main(void) {
  KERNEL k={0};k.Cpu=malloc(16u<<20);assert(k.Cpu);memset(k.Cpu,0xa5,16u<<20);
  AGX_WIN32_ASAHI_BACKEND b={.Gpuva={.Ops={escape},.Context=&k}};
  AGX_G4_BATCH one={0},two={0},three={0};
  APPLE_AGX_G4_NATIVE_RENDER r={0};APPLE_AGX_G4_PROCESS_RANGE a[9],c[9];
  r.WidthPx=2560;r.HeightPx=1600;r.UtileWidthPx=r.UtileHeightPx=16;r.Layers=r.Samples=1;
  assert(prepare_process_buffers(&b,&one,&r,a));
  unsigned *pages=(unsigned *)(k.Cpu+k.Manager.Extents[0].Offset);
  unsigned *blocks=(unsigned *)(k.Cpu+k.Manager.Extents[1].Offset);
  for(unsigned i=0;i<32;++i) {
    assert(blocks[2*i]==(a[2].Va>>15)+4*i);
    for(unsigned j=0;j<4;++j) assert(pages[4*i+j]==(a[2].Va>>15)+4*i+j);
  }
  pages[0]=0xdecafbad;
  assert(prepare_process_buffers(&b,&two,&r,c));
  assert(pages[0]==0xdecafbad && one.Lease.ManagerGeneration==two.Lease.ManagerGeneration);
  for(unsigned i=0;i<3;++i) assert(a[i].Va==c[i].Va);
  for(unsigned i=3;i<9;++i) assert(a[i].Va!=c[i].Va);
  assert(!prepare_process_buffers(&b,&three,&r,c));
  assert(!three.Lease.SceneId && k.Calls==3);
  assert(!prepare_process_buffers(&b,&one,&r,c));
  free(k.Cpu);puts("g4_mesa_process_buffers_replay: PASS");return 0;
}
