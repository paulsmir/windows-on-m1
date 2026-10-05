#include "agx_win32_native_bo.h"
#include <assert.h>
#include <string.h>

typedef struct { AGX_WIN32_DEVICE_INFO Info; unsigned Creates, Maps, Unmaps, Destroys; unsigned char Data[0x4000]; } FIXTURE;
static int query(void *p, AGX_WIN32_DEVICE_INFO *i) { *i=((FIXTURE*)p)->Info; return 1; }
static int create_class(void *p, unsigned c, unsigned long long b, unsigned long long a, unsigned f, unsigned long long *t) { FIXTURE *x=p; assert(c==AgxWin32BufferClassShader && b==0x4000 && a==0x4000 && f==(AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead)); ++x->Creates; *t=7; return 1; }
static int create(void *p,unsigned long long b,unsigned f,unsigned long long*t){(void)p;(void)b;(void)f;(void)t;return 0;}
static int map(void*p,unsigned long long t,unsigned long long o,unsigned long long b,unsigned a,void**v){FIXTURE*x=p;assert(t==7&&o==0&&b==0x4000&&a==AppleAgxWin32BufferCpuWrite);++x->Maps;*v=x->Data;return 1;}
static int unmap(void*p,unsigned long long t){FIXTURE*x=p;assert(t==7);++x->Unmaps;return 1;}
static int destroy(void*p,unsigned long long t){FIXTURE*x=p;assert(t==7);++x->Destroys;return 1;}
static int submit(void*p,const AGX_WIN32_CLEAR_REQUEST*r,unsigned*f){(void)p;(void)r;(void)f;return 0;}
static int fence(void*p,unsigned f,unsigned t){(void)p;(void)f;(void)t;return 1;}
static int retire(void*p,unsigned f){(void)p;(void)f;return 1;}
int main(void) { FIXTURE x; AGX_WIN32_SCREEN s; AGX_WIN32_NATIVE_BO b={0}; void *p=NULL; memset(&x,0,sizeof(x)); x.Info.Magic=AGX_WIN32_DEVICE_INFO_MAGIC;x.Info.Version=AGX_WIN32_DEVICE_INFO_VERSION;x.Info.Bytes=sizeof(x.Info);x.Info.BootGeneration=1;x.Info.GpuGeneration=13;x.Info.GpuVariant=AgxWin32GpuG13G;x.Info.PageBytes=0x4000;x.Info.ClassCount=3; for(unsigned i=0;i<3;i++){x.Info.Classes[i].ClassId=i+1;x.Info.Classes[i].MinimumAlignment=0x4000;x.Info.Classes[i].MaximumBytes=0x100000;x.Info.Classes[i].Flags=(i==1)?(AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead):(AppleAgxWin32BufferCpuRead|AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead|AppleAgxWin32BufferGpuWrite);} AGX_WIN32_WINSYS_OPERATIONS t={create,map,unmap,destroy,submit,fence,retire,NULL}; AGX_WIN32_SCREEN_OPERATIONS o={query,create_class}; assert(AgxWin32ScreenInitialize(&s,&x,1,&t,&o)==AgxWin32ScreenSuccess);assert(AgxWin32NativeBoCreate(&s,AgxWin32BufferClassShader,0x4000,0x4000,AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead,&b)==AgxWin32NativeBoSuccess);assert(AgxWin32NativeBoMap(&s,&b,AppleAgxWin32BufferCpuWrite,&p)==AgxWin32NativeBoSuccess&&p==x.Data);assert(AgxWin32NativeBoMap(&s,&b,AppleAgxWin32BufferCpuWrite,&p)==AgxWin32NativeBoState);assert(AgxWin32NativeBoDestroy(&s,&b)==AgxWin32NativeBoState);assert(AgxWin32NativeBoUnmap(&s,&b)==AgxWin32NativeBoSuccess);assert(AgxWin32NativeBoDestroy(&s,&b)==AgxWin32NativeBoSuccess);assert(x.Creates==1&&x.Maps==1&&x.Unmaps==1&&x.Destroys==1);return 0; }
