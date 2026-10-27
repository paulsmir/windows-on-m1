"""Execute the production native-arena helpers with the real ring allocator.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Arena fallback must never
reuse another token's slice, exceed four mappings, publish a closing-device or
unrepresentable WOW64 view, or leak a never-bound mapping because cancellation
reuses a malformed reply header. Failure retains only the documented KMD owner.
"""
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
#include "umd_staging_ring.h"
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
  BOOL Attempted; BYTE *Address; ULONGLONG Token; ADMISSION_UMD_STAGING_RING *Ring;
};
struct ADMISSION_UMD_DEVICE {
  UINT Magic;
  CALLBACKS *KernelCallbacks;
  ADMISSION_UMD_ADAPTER *Adapter;
  struct {HANDLE handle;} RuntimeDevice;
  HANDLE KernelContext;
  BOOL NativeStagingDisabled,ScreenClosing,DrawTerminal;
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
static UINT fallback_values[5],map_values[4],fallback_count,map_receipts;
static HRESULT fallback_status;

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
      q->Bytes==sizeof(*q) && q->Capacity==(128u<<20) && !q->Token && !q->UserVa);
  unsigned index=map_calls++;
  assert(index<4);
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
  ADMISSION_UMD_RING_SLICE slices[4],extra;
  BYTE *address[4],*p=nullptr; ULONGLONG tokens[4],t=0;
  for(unsigned i=0;i<4;++i) {
    assert(AdmissionUmdNativeStagingReserve(d,128u<<20,65536,&slices[i],&address[i],&tokens[i]));
    assert(map_calls==i+1 && slices[i].Offset==0 && slices[i].Bytes==(128u<<20));
    assert((uintptr_t)address[i]==mappings[i].base && tokens[i]==mappings[i].token);
    mappings[i].bound=TRUE;
  }
  assert(!AdmissionUmdNativeStagingReserve(d,65536,65536,&extra,&p,&t));
  assert(map_calls==4 && !cancel_calls);
  assert(!AdmissionUmdNativeStagingRelease(d,77,slices[0].Ticket,0));
  assert(AdmissionUmdNativeStagingRelease(d,tokens[2],slices[2].Ticket,12));
  for(unsigned i=0;i<4;++i)
    assert(d->NativeStaging[i].Ring->Used==(i==2 ? 0u : (128u<<20)));
  assert(AdmissionUmdNativeStagingReserve(d,65536,65536,&extra,&p,&t));
  assert(t==tokens[2] && extra.Offset==0 && extra.Ticket!=slices[2].Ticket && map_calls==4);
  assert(!AdmissionUmdNativeStagingRelease(d,t,slices[2].Ticket,12));
  AdmissionUmdNativeStagingFallback(d,19,0x123456789ULL,E_FAIL);
  assert(fallback_count==1 && fallback_status==E_FAIL);
  assert(fallback_values[0]==19 && fallback_values[1]==0x23456789u && fallback_values[2]==1);
  assert(fallback_values[3]==(3u*(128u<<20)+65536u) && fallback_values[4]==(512u<<20));
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
static void fifo_retention(ADMISSION_UMD_DEVICE *d) {
  ADMISSION_UMD_RING_SLICE a,b,c; BYTE *p; ULONGLONG t,t2;
  assert(AdmissionUmdNativeStagingReserve(d,64u<<20,65536,&a,&p,&t));
  assert(AdmissionUmdNativeStagingReserve(d,64u<<20,65536,&b,&p,&t2) && t==t2);
  assert(AdmissionUmdNativeStagingRelease(d,t,b.Ticket,4));
  assert(d->NativeStaging[0].Ring->Used==(128u<<20));
  assert(AdmissionUmdNativeStagingReserve(d,65536,65536,&c,&p,&t2) && t2!=t);
  assert(AdmissionUmdNativeStagingRelease(d,t,a.Ticket,7));
  assert(!d->NativeStaging[0].Ring->Used && !cancel_calls);
}
int main(int argc,char **argv) {
  assert(argc==2); ADMISSION_UMD_DEVICE d; init(&d);
  ADMISSION_UMD_RING_SLICE s={}; BYTE *p=nullptr; ULONGLONG t=0;
  if(!strcmp(argv[1],"four")) four_arenas(&d);
  else if(!strcmp(argv[1],"transport")) invalid_transport(&d);
  else if(!strcmp(argv[1],"fifo")) fifo_retention(&d);
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
        scenarios = ("four", "transport", "fifo", "disabled", "heap", "reentrant",
                     *(str(i) for i in range(1, 15)))
        with tempfile.TemporaryDirectory(prefix="umd-native-arena-") as path:
            tmp = Path(path)
            src = tmp / "arena.cpp"
            src.write_text(HARNESS.replace("@@HELPERS@@", "\n".join(bodies)))
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
