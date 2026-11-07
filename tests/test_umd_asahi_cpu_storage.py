"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Only an exact live native owner may authenticate cached T13 memory. WC,
foreign/stale/unmapped pointers must never suppress the command CPU shadow.
Storage accounting includes both private copies even when not mapped.
"""
from pathlib import Path
import unittest
from test_g4_shadow_pools import function, build_and_run
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'drivers/apple-agx/render-admission/umd/src/umd_asahi_owner.c'

class CpuStorage(unittest.TestCase):
    def test_actual_owner_query(self):
        source=SRC.read_text()
        funcs='\n'.join(function(source,n) for n in ('identity_match','cpu_storage'))
        prelude=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t APPLE_AGX_U64;typedef unsigned UINT;
typedef struct {void *PrivateMap;APPLE_AGX_U64 Bytes;} AGX_WIN32_ASAHI_CPU_STORAGE;
typedef struct {int Active,Transition,Mapped,HeapStaging,SystemDirect,StagingAllocation;
 void *NativeBo,*NativeBackend,*PrivateStaging,*HeapStagingShadow,*LockedBase;
 APPLE_AGX_U64 NativeBoSerial,Bytes;} ADMISSION_UMD_SCREEN_BUFFER;
typedef struct {int ScreenClosing,ScreenBufferLock;ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[2];} ADMISSION_UMD_DEVICE;
typedef struct {ADMISSION_UMD_DEVICE *Device;void *Backend;} ADMISSION_UMD_ASAHI_OWNER;
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) 2u
#define ZeroMemory(p,n) memset(p,0,n)
static void AcquireSRWLockShared(int *p){++*p;}
static void ReleaseSRWLockShared(int *p){--*p;}
'''
        main=r'''
int main(void) {
 ADMISSION_UMD_DEVICE d={0};ADMISSION_UMD_ASAHI_OWNER c={&d,(void*)5};
 ADMISSION_UMD_SCREEN_BUFFER *b=&d.ScreenBuffers[1];
 b->Active=1;b->NativeBo=(void*)6;b->NativeBackend=c.Backend;b->NativeBoSerial=7;b->Bytes=65536;
 AGX_WIN32_ASAHI_CPU_STORAGE out;
 assert(cpu_storage(&c,(void*)6,7,&out) && !out.Bytes && !out.PrivateMap);
#ifdef APPLE_AGX_GPUVA_WINSYS
 b->PrivateStaging=(void*)8;b->HeapStagingShadow=(void*)9;
 assert(cpu_storage(&c,(void*)6,7,&out) && out.Bytes==131072 && !out.PrivateMap);
 b->Mapped=b->HeapStaging=1;b->LockedBase=b->PrivateStaging;
 assert(cpu_storage(&c,(void*)6,7,&out) && out.PrivateMap==(void*)8);
 for(unsigned i=0;i<5;++i) {
  if(i==0)b->Mapped=0;if(i==1)b->HeapStaging=0;if(i==2)b->LockedBase=(void*)10;
  if(i==3)b->SystemDirect=1;if(i==4)b->StagingAllocation=1;
  assert(cpu_storage(&c,(void*)6,7,&out) && out.Bytes==131072 && !out.PrivateMap);
  b->Mapped=b->HeapStaging=1;b->LockedBase=b->PrivateStaging;b->SystemDirect=b->StagingAllocation=0;
 }
#endif
 for(unsigned i=0;i<7;++i) {
  if(i==0)b->Active=0;if(i==1)b->Transition=1;if(i==2)b->NativeBo=(void*)10;
  if(i==3)b->NativeBackend=(void*)10;if(i==4)b->NativeBoSerial=8;if(i==5)d.ScreenClosing=1;
  out.PrivateMap=(void*)1;out.Bytes=99;
  assert(!cpu_storage(&c,(void*)6,i==6?0:7,&out) && !out.PrivateMap && !out.Bytes);
  b->Active=1;b->Transition=0;b->NativeBo=(void*)6;b->NativeBackend=c.Backend;b->NativeBoSerial=7;d.ScreenClosing=0;
 }
 assert(!cpu_storage(NULL,(void*)6,7,&out) && !out.Bytes && !out.PrivateMap && !d.ScreenBufferLock);
 puts("owner storage: PASS");
}
'''
        for define in ('','#define APPLE_AGX_GPUVA_WINSYS 1\n'):
            build_and_run(self,define+prelude+funcs+main,'owner storage: PASS')
