"""Execute the production native-arena helpers with the real slice allocator.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Arena fallback must never
reuse another token's slice, exceed four mappings, publish a closing-device or
unrepresentable WOW64 view, or leak a never-bound mapping because cancellation
reuses a malformed reply header. Failure retains only the documented KMD owner.
RC62 (EXP1984): a released slice must be reusable while an older slice of the
same arena is still live; the FIFO ring instead mapped a new arena per wrap.
Arenas grow by demand (4, 8, 16, 32 MiB, within the 64 MiB aperture share),
an arena too small for a request is not mapped for it, and a BO over 16 MiB
never maps an arena: the old code mapped 128 MiB per arena for a 64 KiB BO.
RC64 (EXP1985): slices over 1 MiB use only the 32 MiB arena. First-come
placement let multi-MiB load-time BOs take all 60 MiB, so every later query,
pool, encoder and readback BO (64-512 KiB) fell back to per-submission copy
escapes. Small slices keep 28 MiB that large slices can never take.
rc99 (EXP2031/EXP2033): GravityMark creates its four 512 KiB READBACK heaps
after scene load, with the arenas about 59/60 MiB full; three of four fell
back to private staging, and each frame then completed a submission
synchronously and downloaded 512 KiB through copy escapes (4.1 ms of GPU
idle). A readback slice must find the reserve at the top of the first arena
however full the ordinary placement is, ordinary slices must never take the
reserve, and the kill switch (reserve 0) must restore the RC64 placement.
"""
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx"
SOURCE = DRIVER / "render-admission/umd/src/umd_win32_screen.c"

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include "render_win32_transport.h"
#include "umd_native_arena.h"
using VOID=void; using BOOL=int; using BYTE=unsigned char; using UINT=unsigned;
using ULONGLONG=unsigned long long; using HRESULT=int32_t; using HANDLE=void *;
using SIZE_T=size_t;
#ifdef TEST_WOW64
using ULONG_PTR=uint32_t;
#else
using ULONG_PTR=uintptr_t;
#endif
#define TRUE 1
#define FALSE 0
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)-1)
#define E_OUTOFMEMORY ((HRESULT)0x8007000Eu)
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define HEAP_ZERO_MEMORY 8u
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ADMISSION_UMD_NATIVE_ARENAS 4u
#define ADMISSION_UMD_DEVICE_MAGIC 0x56454455u
#define ADMISSION_UMD_ADAPTER_MAGIC 0x50414455u
struct SRWLOCK {pthread_rwlock_t value;};
struct D3DDDICB_ESCAPE {
  HANDLE hDevice,hContext;
  void *pPrivateDriverData;
  UINT PrivateDriverDataSize;
};
struct CALLBACKS {HRESULT (*pfnEscapeCb)(HANDLE,D3DDDICB_ESCAPE *);};
struct ADMISSION_UMD_ADAPTER {UINT Magic; struct {HANDLE handle;} RuntimeAdapter;};
struct ADMISSION_UMD_NATIVE_ARENA {
  BOOL Attempted; BYTE *Address; ULONGLONG Token; @@RING@@ *Ring;
};
struct ADMISSION_UMD_DEVICE {
  UINT Magic;
  CALLBACKS *KernelCallbacks;
  ADMISSION_UMD_ADAPTER *Adapter;
  struct {HANDLE handle;} RuntimeDevice;
  HANDLE KernelContext;
  BOOL NativeStagingDisabled,ScreenClosing,DrawTerminal;
  UINT NativeReadbackReserve;
  BOOL NativeReadbackIntent;
  SRWLOCK ScreenBufferLock;
  ADMISSION_UMD_NATIVE_ARENA NativeStaging[ADMISSION_UMD_NATIVE_ARENAS];
};
static unsigned lock_depth,heap_live,map_calls,cancel_calls,cancel_success;
static BOOL heap_fail,cancel_fail,reenter;
static int reply_mode;
static ADMISSION_UMD_DEVICE *active;
static ADMISSION_UMD_RING_SLICE inner_slice;
static BYTE *inner_address;
static ULONGLONG inner_token;
static BOOL inner_reserved;
static const HANDLE adapter_handle=(HANDLE)(uintptr_t)0x101;
static const HANDLE device_handle=(HANDLE)(uintptr_t)0x202;
static const HANDLE context_handle=(HANDLE)(uintptr_t)0x303;
struct MAPPING {ULONGLONG token,base; BOOL canceled,bound;};
static MAPPING mappings[8];
static UINT capacities[8];
static const UINT arena_mib[4]={4u,8u,16u,32u};
static UINT fallback_values[5],map_values[4],fallback_count,map_receipts;
static UINT reserve_values[6],reserve_receipts;
static HRESULT fallback_status,reserve_status;

static void AcquireSRWLockExclusive(SRWLOCK *l) {
  assert(!lock_depth); assert(!pthread_rwlock_wrlock(&l->value)); ++lock_depth;
}
static void ReleaseSRWLockExclusive(SRWLOCK *l) {
  assert(lock_depth==1); --lock_depth; assert(!pthread_rwlock_unlock(&l->value));
}
static void AcquireSRWLockShared(SRWLOCK *l) {
  assert(!lock_depth); assert(!pthread_rwlock_rdlock(&l->value)); ++lock_depth;
}
static void ReleaseSRWLockShared(SRWLOCK *l) {
  assert(lock_depth==1); --lock_depth; assert(!pthread_rwlock_unlock(&l->value));
}
static HANDLE GetProcessHeap(void) {return (HANDLE)(uintptr_t)1;}
static void *HeapAlloc(HANDLE h,UINT flags,SIZE_T bytes) {
  assert(!lock_depth && h==GetProcessHeap() && flags==HEAP_ZERO_MEMORY);
  if(heap_fail) return nullptr;
  void *p=calloc(1,bytes); assert(p); ++heap_live; return p;
}
static BOOL HeapFree(HANDLE h,UINT flags,void *p) {
  assert(!lock_depth && h==GetProcessHeap() && !flags && p && heap_live);
  --heap_live; free(p); return TRUE;
}
static void AdmissionUmdDiagnostic(const char *name,HRESULT status,
                                    const UINT *v,UINT count) {
  assert(!lock_depth);
  if(!strcmp(name,"measure-native-ring-fallback")) {
    assert(count==5); memcpy(fallback_values,v,sizeof(fallback_values));
    fallback_status=status; ++fallback_count;
  } else if(!strcmp(name,"measure-native-readback-reserve")) {
    assert(count==6); memcpy(reserve_values,v,sizeof(reserve_values));
    reserve_status=status; ++reserve_receipts;
  } else {
    assert(!strcmp(name,"measure-native-ring-map") && count==4);
    memcpy(map_values,v,sizeof(map_values)); ++map_receipts;
  }
}
static BOOL AdmissionUmdNativeStagingReserve(ADMISSION_UMD_DEVICE *,ULONGLONG,
    ULONGLONG,ADMISSION_UMD_RING_SLICE *,BYTE **,ULONGLONG *);

static HRESULT escape(HANDLE adapter,D3DDDICB_ESCAPE *request) {
  assert(!lock_depth && adapter==adapter_handle && request->hDevice==device_handle &&
      request->hContext==context_handle &&
      request->PrivateDriverDataSize==sizeof(ADMISSION_NATIVE_STAGING_REQUEST));
  auto *q=(ADMISSION_NATIVE_STAGING_REQUEST *)request->pPrivateDriverData;
  if(q->Capacity==0) {
    ++cancel_calls;
    if(cancel_fail || q->Magic!=ADMISSION_NATIVE_STAGING_MAGIC || q->Version!=1 ||
        q->Bytes!=sizeof(*q) || q->UserVa) return E_FAIL;
    for(unsigned i=0;i<map_calls;++i)
      if(mappings[i].token==q->Token && !mappings[i].canceled && !mappings[i].bound) {
        mappings[i].canceled=TRUE; ++cancel_success; return S_OK;
      }
    return E_FAIL;
  }
  assert(q->Magic==ADMISSION_NATIVE_STAGING_MAGIC && q->Version==1 &&
      q->Bytes==sizeof(*q) && q->Capacity && !(q->Capacity & 65535u) &&
      q->Capacity<=(128u<<20) && !q->Token && !q->UserVa);
  unsigned index=map_calls++;
  assert(index<4);
  capacities[index]=q->Capacity;
  if(reply_mode==1) return E_FAIL;
  /* These are OS-returned user VAs, never dereferenced by these helpers. The
   * WOW64 variant exercises the actual ULONG_PTR boundary on a 64-bit host. */
  mappings[index]={0x100000001ULL+index,0x10000000ULL+0x10000000ULL*index,FALSE,FALSE};
  q->Token=mappings[index].token; q->UserVa=mappings[index].base;
  switch(reply_mode) {
    case 2:q->Magic^=1;break;
    case 3:q->Version=2;break;
    case 4:q->Bytes=0;break;
    case 5:q->Capacity-=65536;break;
    case 6:q->Token=0;break;
    case 7:q->UserVa=0;break;
    case 8:q->UserVa+=4096;break;
    case 9:q->UserVa=0xffffffffffff0000ULL;break;
    case 10:q->UserVa=0x100000000ULL;break;
    case 11:q->UserVa=0xfff00000ULL;break;
    case 12:
      AcquireSRWLockExclusive(&active->ScreenBufferLock);
      active->ScreenClosing=TRUE;
      ReleaseSRWLockExclusive(&active->ScreenBufferLock);break;
    case 13:
      AcquireSRWLockExclusive(&active->ScreenBufferLock);
      active->DrawTerminal=TRUE;
      ReleaseSRWLockExclusive(&active->ScreenBufferLock);break;
    default:break;
  }
  if(reenter && index==0)
    inner_reserved=AdmissionUmdNativeStagingReserve(active,65536,65536,
        &inner_slice,&inner_address,&inner_token);
  return S_OK;
}

@@HELPERS@@

static CALLBACKS callbacks={escape};
static ADMISSION_UMD_ADAPTER adapter={ADMISSION_UMD_ADAPTER_MAGIC,{adapter_handle}};
static void init(ADMISSION_UMD_DEVICE *d) {
  memset(d,0,sizeof(*d));
  d->Magic=ADMISSION_UMD_DEVICE_MAGIC; d->Adapter=&adapter; d->KernelCallbacks=&callbacks;
  d->RuntimeDevice.handle=device_handle; d->KernelContext=context_handle;
  assert(!pthread_rwlock_init(&d->ScreenBufferLock.value,nullptr)); active=d;
}
static void finish(ADMISSION_UMD_DEVICE *d) {
  for(unsigned i=0;i<4;++i)
    if(d->NativeStaging[i].Ring) HeapFree(GetProcessHeap(),0,d->NativeStaging[i].Ring);
  assert(!heap_live && !lock_depth);
  assert(!pthread_rwlock_destroy(&d->ScreenBufferLock.value));
}
static void four_arenas(ADMISSION_UMD_DEVICE *d) {
  /* Small slices fill the three small arenas; large ones use only the last. */
  ADMISSION_UMD_RING_SLICE small[28],large[2],extra;
  BYTE *p=nullptr; ULONGLONG small_token[28],large_token[2],t=0;
  for(unsigned i=0;i<28;++i) {
    const unsigned arena=i<4u ? 0u : i<12u ? 1u : 2u;
    const unsigned first=arena==0u ? 0u : arena==1u ? 4u : 12u;
    assert(AdmissionUmdNativeStagingReserve(d,1u<<20,65536,&small[i],&p,&small_token[i]));
    assert(map_calls==arena+1 && capacities[arena]==(arena_mib[arena]<<20));
    assert(small_token[i]==mappings[arena].token && small[i].Offset==(i-first)<<20);
    assert((uintptr_t)p==mappings[arena].base+small[i].Offset);
    mappings[arena].bound=TRUE;
  }
  for(unsigned i=0;i<2;++i) {
    assert(AdmissionUmdNativeStagingReserve(d,16u<<20,65536,&large[i],&p,&large_token[i]));
    assert(map_calls==4 && capacities[3]==(32u<<20) && large_token[i]==mappings[3].token);
    assert(large[i].Offset==i*(16u<<20));
  }
  mappings[3].bound=TRUE;
  assert(!AdmissionUmdNativeStagingReserve(d,65536,65536,&extra,&p,&t));
  assert(map_calls==4 && !cancel_calls);
  assert(!AdmissionUmdNativeStagingRelease(d,77,small[0].Ticket,0));
  for(unsigned i=12;i<28;++i)
    assert(AdmissionUmdNativeStagingRelease(d,small_token[i],small[i].Ticket,12));
  for(unsigned i=0;i<4;++i)
    assert(d->NativeStaging[i].Ring->Used==(i==2 ? 0u : (arena_mib[i]<<20)));
  /* A large slice never takes the released small arena. */
  assert(!AdmissionUmdNativeStagingReserve(d,2u<<20,65536,&extra,&p,&t));
  assert(AdmissionUmdNativeStagingReserve(d,65536,65536,&extra,&p,&t));
  assert(t==mappings[2].token && extra.Offset==0 && extra.Ticket!=small[12].Ticket && map_calls==4);
  assert(!AdmissionUmdNativeStagingRelease(d,t,small[12].Ticket,12));
  AdmissionUmdNativeStagingFallback(d,19,0x123456789ULL,E_FAIL);
  assert(fallback_count==1 && fallback_status==E_FAIL);
  assert(fallback_values[0]==19 && fallback_values[1]==0x23456789u && fallback_values[2]==1);
  assert(fallback_values[3]==((4u+8u+32u)<<20)+65536u && fallback_values[4]==(60u<<20));
  assert(fallback_values[4]<=ADMISSION_NATIVE_STAGING_BUDGET_BYTES);
  assert(!cancel_calls); /* empty or full live arenas retain their KMD owner */
}
static void invalid_transport(ADMISSION_UMD_DEVICE *d) {
  ADMISSION_UMD_RING_SLICE s; BYTE *p=nullptr; ULONGLONG t=0;
  auto reserve=[&](){return AdmissionUmdNativeStagingReserve(d,65536,65536,&s,&p,&t);};
  d->Adapter=nullptr; assert(!reserve()); d->Adapter=&adapter;
  adapter.Magic=0; assert(!reserve()); adapter.Magic=ADMISSION_UMD_ADAPTER_MAGIC;
  adapter.RuntimeAdapter.handle=nullptr; assert(!reserve()); adapter.RuntimeAdapter.handle=adapter_handle;
  d->RuntimeDevice.handle=nullptr; assert(!reserve()); d->RuntimeDevice.handle=device_handle;
  d->KernelContext=nullptr; assert(!reserve()); d->KernelContext=context_handle;
  d->KernelCallbacks=nullptr; assert(!reserve()); d->KernelCallbacks=&callbacks;
  callbacks.pfnEscapeCb=nullptr; assert(!reserve()); callbacks.pfnEscapeCb=escape;
  assert(!map_calls && !heap_live);
}
/* A live older slice must not pin space released after it. */
static void out_of_order_reuse(ADMISSION_UMD_DEVICE *d) {
  ADMISSION_UMD_RING_SLICE a,b,c; BYTE *p; ULONGLONG t,t2;
  assert(AdmissionUmdNativeStagingReserve(d,1u<<20,65536,&a,&p,&t));
  assert(AdmissionUmdNativeStagingReserve(d,1u<<20,65536,&b,&p,&t2) && t==t2);
  assert(AdmissionUmdNativeStagingRelease(d,t,b.Ticket,4));
  assert(d->NativeStaging[0].Ring->Used==(1u<<20));
  assert(AdmissionUmdNativeStagingReserve(d,65536,65536,&c,&p,&t2));
  assert(t2==t && c.Offset==(1u<<20) && map_calls==1);
  assert((uintptr_t)p==mappings[0].base+(1u<<20));
  assert(!AdmissionUmdNativeStagingRelease(d,t,b.Ticket,4)); /* stale ticket */
  assert(AdmissionUmdNativeStagingRelease(d,t,a.Ticket,7));
  assert(AdmissionUmdNativeStagingRelease(d,t,c.Ticket,8));
  assert(!d->NativeStaging[0].Ring->Used && !cancel_calls);
}
/* Steady state: one long-lived BO plus per-frame staging churn stays in the
 * first arena and every reservation is a distinct, non-overlapping slice. */
static void steady_churn(ADMISSION_UMD_DEVICE *d) {
  ADMISSION_UMD_RING_SLICE keep,s[3]; BYTE *p; ULONGLONG t,tk;
  assert(AdmissionUmdNativeStagingReserve(d,65536,65536,&keep,&p,&tk));
  for(unsigned frame=0;frame<300;++frame) {
    for(unsigned i=0;i<3;++i) {
      assert(AdmissionUmdNativeStagingReserve(d,(1u<<20)-i*65536u,65536,&s[i],&p,&t));
      assert(t==tk && s[i].Offset>=65536u);
      for(unsigned j=0;j<i;++j)
        assert(s[i].Offset>=s[j].Offset+s[j].Bytes || s[j].Offset>=s[i].Offset+s[i].Bytes);
    }
    for(unsigned i=0;i<3;++i)
      assert(AdmissionUmdNativeStagingRelease(d,t,s[(i+frame)%3].Ticket,frame+1ull));
  }
  assert(map_calls==1 && capacities[0]==(4u<<20) && d->NativeStaging[0].Ring->Used==65536u);
  assert(AdmissionUmdNativeStagingRelease(d,tk,keep.Ticket,0));
  assert(!d->NativeStaging[0].Ring->Used && !cancel_calls);
}
/* Arenas grow by demand and never for a BO the native path must not hold. */
static void demand_growth(ADMISSION_UMD_DEVICE *d) {
  ADMISSION_UMD_RING_SLICE s,big,mid,x,small; BYTE *p; ULONGLONG t,tb,tm,ts;
  assert(AdmissionUmdNativeStagingReserve(d,65536,65536,&s,&p,&t));
  assert(map_calls==1 && capacities[0]==(4u<<20));
  /* RC64: 16 MiB maps only the large arena, never the 8 or 16 MiB one. */
  assert(AdmissionUmdNativeStagingReserve(d,16u<<20,65536,&big,&p,&tb));
  assert(map_calls==2 && capacities[1]==(32u<<20) && tb!=t);
  assert(!d->NativeStaging[1].Attempted && !d->NativeStaging[2].Attempted && d->NativeStaging[3].Ring);
  /* Over the per-BO limit (EXP1984: 128 MiB heaps): no new mapping. */
  assert(!AdmissionUmdNativeStagingReserve(d,(16u<<20)+65536u,65536,&x,&p,&t));
  assert(!AdmissionUmdNativeStagingReserve(d,128u<<20,65536,&x,&p,&t));
  assert(!AdmissionUmdNativeStagingReserve(d,65536,32u<<20,&x,&p,&t));
  assert(map_calls==2);
  /* A later 8 MiB request shares the large arena; still no small mapping. */
  assert(AdmissionUmdNativeStagingReserve(d,8u<<20,65536,&mid,&p,&tm));
  assert(map_calls==2 && tm==tb && mid.Offset==(16u<<20) && !d->NativeStaging[1].Attempted);
  /* A 1 MiB slice is small: the first small arena still has room. */
  assert(AdmissionUmdNativeStagingReserve(d,1u<<20,65536,&small,&p,&ts));
  assert(map_calls==2 && ts==t && small.Offset==65536u);
  unsigned long long mapped=0;
  for(unsigned i=0;i<map_calls;++i) mapped+=capacities[i];
  assert(mapped==(36u<<20) && mapped<=ADMISSION_NATIVE_STAGING_BUDGET_BYTES);
  assert(AdmissionUmdNativeStagingRelease(d,tb,big.Ticket,0));
  assert(AdmissionUmdNativeStagingRelease(d,tm,mid.Ticket,0));
  assert(AdmissionUmdNativeStagingRelease(d,ts,small.Ticket,0));
  assert(!cancel_calls);
}
/* RC64 (EXP1985): load-time large BOs must not starve the small per-batch,
 * query and readback BOs of a scene that starts after them. */
static void small_reserved(ADMISSION_UMD_DEVICE *d) {
  ADMISSION_UMD_RING_SLICE large[4],small[128],x; BYTE *p; ULONGLONG t,tl[4],tsm[128];
  unsigned large_count=0,small_count=0;
  /* Scene load: 7-9 MiB staging BOs (EXP1985 f36-f55) until refused. */
  while(AdmissionUmdNativeStagingReserve(d,(7u<<20)+(1u<<18),65536,&large[large_count],&p,&tl[large_count]))
    ++large_count;
  assert(large_count==4 && map_calls==1 && capacities[0]==(32u<<20));
  assert(!AdmissionUmdNativeStagingReserve(d,4u<<20,65536,&x,&p,&t));
  assert(map_calls==1);
  /* Scene start: 64 KiB query, 256 KiB pool and 512 KiB encoder BOs. */
  static const unsigned sizes[3]={65536u,262144u,524288u};
  unsigned long long small_bytes=0;
  while(small_bytes+sizes[small_count%3]<=(28ull<<20)) {
    assert(AdmissionUmdNativeStagingReserve(d,sizes[small_count%3],65536,&small[small_count],&p,
                                            &tsm[small_count]));
    small_bytes+=sizes[small_count%3];
    ++small_count;
  }
  assert(map_calls==4 && small_bytes>(27ull<<20));
  for(unsigned i=0;i<small_count;++i) assert(AdmissionUmdNativeStagingRelease(d,tsm[i],small[i].Ticket,0));
  for(unsigned i=0;i<large_count;++i) assert(AdmissionUmdNativeStagingRelease(d,tl[i],large[i].Ticket,0));
  /* With the large arena free again, a small slice still prefers its own. */
  assert(AdmissionUmdNativeStagingReserve(d,65536,65536,&x,&p,&t) && t==mappings[1].token);
  assert(AdmissionUmdNativeStagingRelease(d,t,x.Ticket,0) && !cancel_calls);
}
/* rc99: the GravityMark order. Scene load fills every ordinary byte with
 * 512 KiB slices; then the four READBACK heaps arrive. */
static unsigned fill_ordinary(ADMISSION_UMD_DEVICE *d,ADMISSION_UMD_RING_SLICE *s,ULONGLONG *t,
                              unsigned limit) {
  unsigned n=0; BYTE *p=nullptr;
  while(n<limit && AdmissionUmdNativeStagingReserve(d,512u<<10,65536,&s[n],&p,&t[n])) {
    if(t[n]==mappings[0].token) assert((uintptr_t)p==mappings[0].base+s[n].Offset);
    ++n;
  }
  assert(n<limit && map_calls==4);
  return n;
}
static void readback_reserve(ADMISSION_UMD_DEVICE *d) {
  d->NativeReadbackReserve=2u<<20;
  ADMISSION_UMD_RING_SLICE s[160],r[5],x; ULONGLONG t[160],tr[5],tx; BYTE *p=nullptr;
  /* A readback heap created while ordinary space is free uses that space. */
  d->NativeReadbackIntent=TRUE;
  assert(AdmissionUmdNativeStagingReserve(d,512u<<10,65536,&x,&p,&tx));
  assert(tx==mappings[0].token && x.Offset==0 && !reserve_receipts);
  d->NativeReadbackIntent=FALSE;
  assert(AdmissionUmdNativeStagingRelease(d,tx,x.Ticket,0));
  const unsigned n=fill_ordinary(d,s,t,160);
  /* 2 + 8 + 16 + 32 MiB of ordinary space; nothing in the reserve. */
  assert(n==116);
  for(unsigned i=0;i<n;++i)
    if(t[i]==mappings[0].token) assert(s[i].Offset+s[i].Bytes<=(2u<<20));
  assert(d->NativeStaging[0].Ring->Used==(2u<<20));
  assert(!AdmissionUmdNativeStagingReserve(d,65536,65536,&x,&p,&tx));
  /* Four readback heaps take the reserve; a fifth is refused. */
  d->NativeReadbackIntent=TRUE;
  for(unsigned i=0;i<4;++i) {
    assert(AdmissionUmdNativeStagingReserve(d,512u<<10,65536,&r[i],&p,&tr[i]));
    assert(tr[i]==mappings[0].token && r[i].Offset==(2u<<20)+i*(512u<<10));
    assert((uintptr_t)p==mappings[0].base+r[i].Offset);
    assert(reserve_receipts==i+1 && reserve_status==S_OK && reserve_values[0]==1u &&
           reserve_values[1]==1u && reserve_values[2]==(512u<<10) &&
           reserve_values[3]==r[i].Offset && reserve_values[5]==(2u<<20));
  }
  assert(!AdmissionUmdNativeStagingReserve(d,512u<<10,65536,&r[4],&p,&tr[4]));
  assert(reserve_receipts==5 && reserve_status==E_OUTOFMEMORY && !reserve_values[1] &&
         reserve_values[4]==(4u<<20));
  /* Over 1 MiB a readback never reaches the first arena's reserve. */
  assert(!AdmissionUmdNativeStagingReserve(d,2u<<20,65536,&x,&p,&tx) && reserve_receipts==5);
  d->NativeReadbackIntent=FALSE;
  /* A released reserve slice is not ordinary space. */
  assert(AdmissionUmdNativeStagingRelease(d,tr[1],r[1].Ticket,3));
  assert(!AdmissionUmdNativeStagingReserve(d,512u<<10,65536,&x,&p,&tx));
  d->NativeReadbackIntent=TRUE;
  assert(AdmissionUmdNativeStagingReserve(d,512u<<10,65536,&r[1],&p,&tr[1]));
  assert(r[1].Offset==(2u<<20)+(512u<<10));
  d->NativeReadbackIntent=FALSE;
  for(unsigned i=0;i<4;++i) assert(AdmissionUmdNativeStagingRelease(d,tr[i],r[i].Ticket,4));
  for(unsigned i=0;i<n;++i) assert(AdmissionUmdNativeStagingRelease(d,t[i],s[i].Ticket,4));
  for(unsigned i=0;i<4;++i) assert(!d->NativeStaging[i].Ring->Used);
  assert(map_calls==4 && !cancel_calls);
}
/* Kill switch: reserve 0 is the RC64 placement, the intent changes nothing. */
static void readback_reserve_off(ADMISSION_UMD_DEVICE *d) {
  ADMISSION_UMD_RING_SLICE s[160],x; ULONGLONG t[160],tx; BYTE *p=nullptr;
  const unsigned n=fill_ordinary(d,s,t,160);
  assert(n==120 && d->NativeStaging[0].Ring->Used==(4u<<20));
  d->NativeReadbackIntent=TRUE;
  assert(!AdmissionUmdNativeStagingReserve(d,512u<<10,65536,&x,&p,&tx));
  assert(!reserve_receipts);
  for(unsigned i=0;i<n;++i) assert(AdmissionUmdNativeStagingRelease(d,t[i],s[i].Ticket,1));
  assert(map_calls==4 && !cancel_calls);
}
int main(int argc,char **argv) {
  assert(argc==2); ADMISSION_UMD_DEVICE d; init(&d);
  ADMISSION_UMD_RING_SLICE s={}; BYTE *p=nullptr; ULONGLONG t=0;
  if(!strcmp(argv[1],"readback")) readback_reserve(&d);
  else if(!strcmp(argv[1],"readback-off")) readback_reserve_off(&d);
  else if(!strcmp(argv[1],"four")) four_arenas(&d);
  else if(!strcmp(argv[1],"transport")) invalid_transport(&d);
  else if(!strcmp(argv[1],"reuse")) out_of_order_reuse(&d);
  else if(!strcmp(argv[1],"churn")) steady_churn(&d);
  else if(!strcmp(argv[1],"demand")) demand_growth(&d);
  else if(!strcmp(argv[1],"small")) small_reserved(&d);
  else if(!strcmp(argv[1],"disabled")) {
    d.NativeStagingDisabled=TRUE;
    assert(!AdmissionUmdNativeStagingReserve(&d,65536,65536,&s,&p,&t));
    assert(!map_calls && !heap_live && !d.NativeStaging[0].Attempted);
  } else if(!strcmp(argv[1],"heap")) {
    heap_fail=TRUE;
    assert(!AdmissionUmdNativeStagingReserve(&d,65536,65536,&s,&p,&t));
    assert(!map_calls && !heap_live);
  } else if(!strcmp(argv[1],"reentrant")) {
    reenter=TRUE;
    assert(AdmissionUmdNativeStagingReserve(&d,65536,65536,&s,&p,&t));
    assert(inner_reserved && t!=inner_token && p!=inner_address && map_calls==2);
    assert(AdmissionUmdNativeStagingRelease(&d,t,s.Ticket,0));
    assert(AdmissionUmdNativeStagingRelease(&d,inner_token,inner_slice.Ticket,0));
  } else {
    reply_mode=atoi(argv[1]);
    if(reply_mode==14) {reply_mode=8;cancel_fail=TRUE;}
    assert(reply_mode>0);
    BOOL ok=AdmissionUmdNativeStagingReserve(&d,65536,65536,&s,&p,&t);
#ifndef TEST_WOW64
    if(reply_mode==10 || reply_mode==11) {
      assert(ok && map_calls==1 && !cancel_calls);
      finish(&d); return 0;
    }
#endif
    assert(!ok && map_calls==1 && !heap_live && !d.NativeStaging[0].Ring);
    assert(map_receipts==1 && map_values[3]==((reply_mode==12 || reply_mode==13) ? 1u : 0u));
    if(reply_mode!=12 && reply_mode!=13) {
      assert(fallback_count==1 && fallback_values[0]==3u);
      assert(fallback_status==E_FAIL && !d.DrawTerminal && !d.ScreenClosing);
    }
    if(reply_mode==1 || reply_mode==6) assert(!cancel_calls);
    else assert(cancel_calls==1 && cancel_success==(cancel_fail ? 0u : 1u));
  }
  finish(&d); puts("native arena PASS"); return 0;
}
'''


class NativeStagingArena(unittest.TestCase):
    def test_actual_helpers_cpp_and_wow64_pointer_contract(self):
        text = SOURCE.read_text()
        names = ("AdmissionUmdNativeStagingFallback", "AdmissionUmdNativeStagingReserve",
                 "AdmissionUmdNativeStagingRelease")
        bodies = [function(text, name) for name in names]
        self.assertTrue(all(bodies), "production native arena helper missing")
        scenarios = ("readback", "readback-off", "four", "transport", "reuse", "churn", "demand", "small", "disabled", "heap", "reentrant",
                     *(str(i) for i in range(1, 15)))
        with tempfile.TemporaryDirectory(prefix="umd-native-arena-") as path:
            tmp = Path(path)
            src = tmp / "arena.cpp"
            internal = (DRIVER / "render-admission/umd/src/umd_internal.h").read_text()
            ring = re.search(r"\b(ADMISSION_UMD_[A-Z_]+) \*Ring;\s*\} ADMISSION_UMD_NATIVE_ARENA;",
                             internal)
            self.assertTrue(ring, "native arena slice type missing")
            src.write_text(HARNESS.replace("@@HELPERS@@", "\n".join(bodies))
                           .replace("@@RING@@", ring.group(1)))
            for wow64 in (False, True):
                exe = tmp / ("wow64" if wow64 else "native")
                flags = ["-DTEST_WOW64", "-Wno-int-to-pointer-cast"] if wow64 else []
                build = subprocess.run([
                    "clang++", "-x", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-pthread", *flags,
                    "-I", str(DRIVER / "render-admission/include"),
                    "-I", str(DRIVER / "render-admission/umd/include"),
                    "-I", str(DRIVER / "shared/include"), str(src), "-o", str(exe),
                ], capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                for scenario in scenarios:
                    with self.subTest(wow64=wow64, scenario=scenario):
                        run = subprocess.run([str(exe), scenario], capture_output=True,
                                             text=True, timeout=10)
                        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
