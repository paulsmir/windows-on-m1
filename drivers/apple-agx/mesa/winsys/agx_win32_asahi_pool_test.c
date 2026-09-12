#include "agx_device.h"
#include "pool.h"
#include "agx_win32_asahi_bo.h"
#include "agx_win32_asahi_capture.h"
#include <stdio.h>
#include <string.h>

/* Calls the original native pool implementation; only Windows runtime callbacks
 * are controlled by the enclosing UMD owner test. No draw packet is fabricated. */
unsigned AgxWin32AsahiPoolTest(AGX_WIN32_SCREEN *screen,
    const AGX_WIN32_ASAHI_OWNER_OPS *ops,void *owner,
    AGX_WIN32_ASAHI_BACKEND *backend,void (*holds)(void *,int)) {
  struct agx_device native={0};
  struct agx_pool pool;
  struct agx_bo *first=NULL,*second=NULL;
  AGX_WIN32_RELOC_ALLOCATION identity;
  AGX_WIN32_ASAHI_CAPTURE capture={0};
  APPLE_AGX_U32 reference;
  unsigned errors=0;
#define CHECK_NATIVE(x) do { if(!(x)) { ++errors; fprintf(stderr,"NATIVE_POOL line=%u %s\n",(unsigned)__LINE__,#x); } } while(0)
  CHECK_NATIVE(AgxWin32AsahiAttach(backend,&native,screen,ops,owner,0x1100000000ULL));
  agx_pool_init(&pool,&native,"Windows native pipeline",AGX_BO_LOW_VA,false);
  struct agx_ptr a=agx_pool_alloc_aligned_with_bo(&pool,64,64,&first);
  struct agx_ptr b=agx_pool_alloc_aligned_with_bo(&pool,128,64,&second);
  CHECK_NATIVE(first && first==second && first->dev==&native);
  CHECK_NATIVE(a.cpu && b.cpu && (char *)b.cpu-(char *)a.cpu==64);
  CHECK_NATIVE(b.gpu-a.gpu==64 && first->va->addr==a.gpu);
  if(a.cpu) memset(a.cpu,0x37,64);
  CHECK_NATIVE(AgxWin32AsahiIdentity(backend,first,&identity));
  CHECK_NATIVE(identity.Owner && identity.Token && identity.Serial &&
               identity.Generation==screen->Generation && identity.Bytes==0x40000 &&
               identity.AllocationIndex==~0u);
  CHECK_NATIVE(!AgxWin32AsahiIdentity(backend,(struct agx_bo *)(uintptr_t)1,&identity));
  CHECK_NATIVE(AgxWin32AsahiIdentity(backend,first,&identity));
  CHECK_NATIVE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,1)==AgxRelocOk);
  CHECK_NATIVE(AgxWin32AsahiCaptureReference(&capture,first,AppleAgxWin32RoleUscPipeline,
      AppleAgxWin32AccessRead,0,64,&reference)==AgxRelocOk && reference==0);
  CHECK_NATIVE(AgxWin32AsahiCaptureReference(&capture,second,AppleAgxWin32RoleUscPipeline,
      AppleAgxWin32AccessRead,64,128,&reference)==AgxRelocOk && reference==1);
  CHECK_NATIVE(capture.Count==1 && capture.Capture.ReferenceCount==2 &&
      capture.Capture.Allocations[0].Token==identity.Token &&
      capture.Capture.Allocations[1].Serial==identity.Serial &&
      capture.Capture.References[0].AllocationIndex==0 &&
      capture.Capture.References[1].AllocationIndex==0);
  holds(owner,1);
  agx_pool_cleanup(&pool);
  CHECK_NATIVE(first->refcnt==2);
  CHECK_NATIVE(AgxWin32RelocAbort(&capture.Capture)==AgxRelocOk);
  CHECK_NATIVE(backend->LiveBos==1 && !AgxWin32AsahiCollect(backend));
  CHECK_NATIVE(!AgxWin32AsahiIdentity(backend,first,&identity));
  CHECK_NATIVE(!AgxWin32AsahiDetach(backend));
  holds(owner,0);
  holds(owner,4);
  CHECK_NATIVE(!AgxWin32AsahiCollect(backend) && backend->LiveBos==1);
  CHECK_NATIVE(AgxWin32AsahiCollect(backend));
  CHECK_NATIVE(backend->LiveBos==0 && AgxWin32AsahiDetach(backend));
  CHECK_NATIVE(AgxWin32AsahiAttach(backend,&native,screen,ops,owner,0x1100000000ULL));
  holds(owner,2);
  agx_pool_init(&pool,&native,"Windows allocation failure",AGX_BO_LOW_VA,false);
  first=(struct agx_bo *)(uintptr_t)1;
  a=agx_pool_alloc_aligned_with_bo(&pool,64,64,&first);
  CHECK_NATIVE(!a.cpu && !a.gpu && !first && backend->Failed);
  agx_pool_cleanup(&pool);
  holds(owner,3);
  CHECK_NATIVE(AgxWin32AsahiDetach(backend));
  printf("NATIVE_POOL_WINDOWS_OWNER: errors=%u\n",errors);
  return errors;
}
