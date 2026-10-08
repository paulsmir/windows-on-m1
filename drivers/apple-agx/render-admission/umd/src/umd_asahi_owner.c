#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
extern "C" {
#include "umd_internal.h"
#include "umd_asahi_owner.h"
}

static int enter(void *context,AGX_WIN32_SCREEN *screen) {
  ADMISSION_UMD_ASAHI_OWNER *c=(ADMISSION_UMD_ASAHI_OWNER *)context;
  if(!c || !c->Device || !c->Backend || c->Device->Magic!=ADMISSION_UMD_DEVICE_MAGIC) return 0;
  ADMISSION_UMD_DEVICE *d=c->Device;
  if(screen!=&d->Screen || screen->Context!=d || !screen->Active ||
     screen->Generation!=d->Win32Generation) return 0;
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  int result=!d->ScreenClosing && !d->DrawTerminal && d->NativeBackendCount!=MAXUINT32;
  if(result) ++d->NativeBackendCount;
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
}
static void leave(void *context) {
  ADMISSION_UMD_ASAHI_OWNER *c=(ADMISSION_UMD_ASAHI_OWNER *)context;
  AcquireSRWLockExclusive(&c->Device->ScreenBufferLock);
  if(c->Device->NativeBackendCount) --c->Device->NativeBackendCount;
  ReleaseSRWLockExclusive(&c->Device->ScreenBufferLock);
}

static int associate(void *context,APPLE_AGX_U64 token,const void *key,
    APPLE_AGX_U64 serial,AGX_WIN32_ASAHI_MAP_RELEASE releaseMap) {
  ADMISSION_UMD_ASAHI_OWNER *c=(ADMISSION_UMD_ASAHI_OWNER *)context;
  int result=0;
  if(!c || !c->Device || !c->Backend || !key || !serial || !releaseMap) return 0;
  ADMISSION_UMD_DEVICE *d=c->Device;
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  if(!d->ScreenClosing) {
    BOOL duplicate=FALSE;
    for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(d);++i)
      if(d->ScreenBuffers[i].Active && d->ScreenBuffers[i].NativeBo==key) duplicate=TRUE;
    for(UINT i=0;!duplicate && i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(d);++i) {
      ADMISSION_UMD_SCREEN_BUFFER *b=&d->ScreenBuffers[i];
      if(b->Active && b->Token==token && !b->Transition && !b->NativeBo && !b->SubmissionHolds) {
        b->NativeBo=key; b->NativeBoSerial=serial; b->NativeBackend=c->Backend;
        b->NativeMapRelease=releaseMap; result=1; break;
      }
    }
  }
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
}
static int detach(void *context,APPLE_AGX_U64 token,const void *key,APPLE_AGX_U64 serial) {
  ADMISSION_UMD_ASAHI_OWNER *c=(ADMISSION_UMD_ASAHI_OWNER *)context;
  ADMISSION_UMD_DEVICE *d=c->Device;
  int result=0;
  AcquireSRWLockExclusive(&d->ScreenBufferLock);
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(d);++i) {
    ADMISSION_UMD_SCREEN_BUFFER *b=&d->ScreenBuffers[i];
    if(b->Active && b->Token==token && b->NativeBo==key && b->NativeBoSerial==serial &&
       b->NativeBackend==c->Backend && !b->Transition && !b->SubmissionHolds && !b->SourceHolds) {
      b->NativeBo=NULL; b->NativeBoSerial=0; b->NativeBackend=NULL;
      b->NativeMapRelease=NULL; result=1; break;
    }
  }
  ReleaseSRWLockExclusive(&d->ScreenBufferLock);
  return result;
}
static int identity(void *context,const void *key,APPLE_AGX_U64 serial,AGX_WIN32_RELOC_ALLOCATION *out) {
  ADMISSION_UMD_ASAHI_OWNER *c=(ADMISSION_UMD_ASAHI_OWNER *)context;
  ADMISSION_UMD_DEVICE *d=c->Device;
  int result=0;
  if(!key || !out) return 0;
  AcquireSRWLockShared(&d->ScreenBufferLock);
  for(UINT i=0;!d->ScreenClosing && i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(d);++i) {
    ADMISSION_UMD_SCREEN_BUFFER *b=&d->ScreenBuffers[i];
    if(b->Active && b->NativeBo==key && b->NativeBackend==c->Backend && !b->Transition &&
       (!serial || b->NativeBoSerial==serial)) {
      ZeroMemory(out,sizeof(*out)); out->Owner=d->OwnerCookie; out->Token=b->Token;
      out->Serial=b->Serial; out->Generation=d->Win32Generation; out->Bytes=b->Bytes;
      out->AllocationIndex=~0u;
      if(b->Flags&AppleAgxWin32BufferGpuRead) out->Access|=AppleAgxWin32AccessRead;
      if(b->Flags&AppleAgxWin32BufferGpuWrite) out->Access|=AppleAgxWin32AccessWrite;
      if(b->ClassId==AgxWin32BufferClassShader) out->Access|=AppleAgxWin32AccessExecute;
      result=1; break;
    }
  }
  ReleaseSRWLockShared(&d->ScreenBufferLock);
  return result;
}
static const void *next_bo(void *context,APPLE_AGX_U32 *cursor) {
  ADMISSION_UMD_ASAHI_OWNER *c=(ADMISSION_UMD_ASAHI_OWNER *)context;
  ADMISSION_UMD_DEVICE *d=c->Device;
  const void *key=NULL;
  AcquireSRWLockShared(&d->ScreenBufferLock);
  while(*cursor<ADMISSION_UMD_SCREEN_BUFFER_SCAN(d)) {
    ADMISSION_UMD_SCREEN_BUFFER *b=&d->ScreenBuffers[(*cursor)++];
    if(b->Active && b->NativeBackend==c->Backend && b->NativeBo) { key=b->NativeBo; break; }
  }
  ReleaseSRWLockShared(&d->ScreenBufferLock);
  return key;
}
void AdmissionUmdAsahiOwnerOperations(AGX_WIN32_ASAHI_OWNER_OPS *ops) {
  ops->Enter=enter; ops->Leave=leave;
  ops->Associate=associate; ops->Detach=detach; ops->Identity=identity; ops->NextBo=next_bo;
}
