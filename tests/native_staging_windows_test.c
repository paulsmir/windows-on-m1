/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Execute the production
 * manager: no page reuse before CPU unmap, allocation release and graph
 * retirement; exact owner/PFN authentication; bounded rollback and exit races.
 * Only Windows APIs and the parent's pool accounting are substituted. */
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void VOID;
typedef void *PVOID;
typedef void *HANDLE;
typedef unsigned char UCHAR, BOOLEAN;
typedef uint32_t ULONG;
typedef int32_t LONG, NTSTATUS;
typedef unsigned long long ULONGLONG, *PULONGLONG;
typedef long long LONGLONG;
typedef uintptr_t ULONG_PTR, PFN_NUMBER;
typedef size_t SIZE_T;
typedef unsigned KIRQL;
typedef struct {long long QuadPart;} PHYSICAL_ADDRESS;
typedef struct _ADMISSION_CONTEXT {int Id; ULONGLONG Charge;} ADMISSION_CONTEXT;
typedef struct {int Id, References, Exited;} TEST_PROCESS, *PEPROCESS, *PRKPROCESS;
typedef struct {PEPROCESS Previous;} KAPC_STATE;
typedef struct {pthread_mutex_t Lock; int Initialized;} FAST_MUTEX;
typedef struct {ULONG ByteCount; PFN_NUMBER *Pfn; void *Data; int Mapped;
                PEPROCESS Owner;} MDL, *PMDL;
typedef VOID (*PCREATE_PROCESS_NOTIFY_ROUTINE)(HANDLE, HANDLE, BOOLEAN);
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define PAGE_SIZE 4096u
#define PAGE_SHIFT 12
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)-1)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)-2)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)-3)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)-4)
#define STATUS_ACCESS_DENIED ((NTSTATUS)-5)
#define STATUS_DEVICE_BUSY ((NTSTATUS)-6)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)-7)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)-8)
#define STATUS_INVALID_HANDLE ((NTSTATUS)-9)
#define NT_SUCCESS(s) ((s) >= 0)
#define MAXULONGLONG (~0ULL)
#define MAXULONG UINT32_MAX
#define POOL_FLAG_NON_PAGED 1
#define ADMISSION_POOL_TAG 123u
#define ADMISSION_HVC_PHYSICAL_LIMIT (1ULL << 40)
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define MmWriteCombined 2
#define UserMode 1
#define NormalPagePriority 16
#define MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS 0x20
#define MM_ALLOCATE_FULLY_REQUIRED 4
#define C_ASSERT(x) static_assert(x, #x)
#ifndef __cplusplus
#undef C_ASSERT
#define C_ASSERT(x) _Static_assert(x, #x)
#endif
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define NT_ASSERT(x) assert(x)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define __try if (setjmp(ExceptionFrame) == 0)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1
#define GetExceptionCode() STATUS_INSUFFICIENT_RESOURCES

static __thread PEPROCESS Current;
static __thread unsigned RegistryDepth;
static __thread jmp_buf ExceptionFrame;
static PCREATE_PROCESS_NOTIFY_ROUTINE Notify;
static int RegisterFail, PoolFail, ChargeFail, MdlFail, GeometryFail, MapFail;
static unsigned LiveObjects, LiveMdls, Maps, Unmaps, Attaches, Unregisters;
static PFN_NUMBER NextPfn = 0x10000;
static pthread_mutex_t Gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t GateCond = PTHREAD_COND_INITIALIZER;
static int HoldUnmap, UnmapEntered, ReleaseUnmap, ExitStarted, ExitFinished;
static ADMISSION_CONTEXT Adapter = {1,0}, OtherAdapter = {2,0};
static TEST_PROCESS Owner = {1,0,0}, Other = {2,0,0};
static int GpuOwners[8];
static void (*UnchargeHook)(ADMISSION_CONTEXT *);
static void (*ChargeHook)(void);
static int RequireUnregisteredDereference;

static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG expected) {
  __atomic_compare_exchange_n(p,&expected,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
  return expected;
}
static LONG InterlockedExchange(volatile LONG *p,LONG v) {
  return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST);
}
static void ExInitializeFastMutex(FAST_MUTEX *m) {
  if(m->Initialized) assert(!pthread_mutex_destroy(&m->Lock));
  assert(!pthread_mutex_init(&m->Lock,NULL));
  m->Initialized=1;
}
static void ExAcquireFastMutex(FAST_MUTEX *m) {
  assert(!RegistryDepth); assert(!pthread_mutex_lock(&m->Lock)); ++RegistryDepth;
}
static void ExReleaseFastMutex(FAST_MUTEX *m) {
  assert(RegistryDepth==1); --RegistryDepth; assert(!pthread_mutex_unlock(&m->Lock));
}
static KIRQL KeGetCurrentIrql(void) {return RegistryDepth ? APC_LEVEL : PASSIVE_LEVEL;}
static PEPROCESS PsGetCurrentProcess(void) {return Current;}
static void ObReferenceObject(PEPROCESS p) {
  assert(!RegistryDepth); __atomic_add_fetch(&p->References,1,__ATOMIC_SEQ_CST);
}
static void ObDereferenceObject(PEPROCESS p) {
  if(RequireUnregisteredDereference) assert(!Notify);
  assert(!RegistryDepth); assert(__atomic_sub_fetch(&p->References,1,__ATOMIC_SEQ_CST)>=0);
}
static NTSTATUS PsSetCreateProcessNotifyRoutine(PCREATE_PROCESS_NOTIFY_ROUTINE n,
                                               BOOLEAN remove) {
  assert(!RegistryDepth);
  if(remove) {assert(Notify==n); Notify=NULL; ++Unregisters; return STATUS_SUCCESS;}
  if(RegisterFail) return STATUS_INSUFFICIENT_RESOURCES;
  assert(!Notify); Notify=n; return STATUS_SUCCESS;
}
static void *ExAllocatePool2(ULONG flags,SIZE_T bytes,ULONG tag) {
  assert(!RegistryDepth && flags==POOL_FLAG_NON_PAGED && tag==ADMISSION_POOL_TAG);
  if(PoolFail && --PoolFail==0) return NULL;
  void *p=calloc(1,bytes); assert(p); ++LiveObjects; return p;
}
static void ExFreePoolWithTag(void *p,ULONG tag) {
  assert(!RegistryDepth && p && tag==ADMISSION_POOL_TAG && LiveObjects);
  --LiveObjects; free(p);
}
static void ExFreePool(void *p) {assert(!RegistryDepth); free(p);}
static PMDL MmAllocatePagesForMdlEx(PHYSICAL_ADDRESS low,PHYSICAL_ADDRESS high,
    PHYSICAL_ADDRESS skip,SIZE_T bytes,int cache,ULONG flags) {
  assert(!RegistryDepth && low.QuadPart==0 &&
      high.QuadPart==(long long)(ADMISSION_HVC_PHYSICAL_LIMIT-1) &&
      skip.QuadPart==65536 && cache==MmWriteCombined &&
      flags==(MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS|MM_ALLOCATE_FULLY_REQUIRED));
  if(MdlFail) return NULL;
  PMDL m=(PMDL)calloc(1,sizeof(*m)); assert(m);
  m->ByteCount=(ULONG)bytes; m->Data=calloc(1,bytes);
  m->Pfn=(PFN_NUMBER *)malloc(bytes/PAGE_SIZE*sizeof(PFN_NUMBER));
  assert(m->Data && m->Pfn);
  for(SIZE_T i=0;i<bytes/PAGE_SIZE;++i) m->Pfn[i]=NextPfn+(i/16)*32+i%16;
  NextPfn+=bytes/PAGE_SIZE*2;
  if(GeometryFail==1) m->ByteCount-=PAGE_SIZE;
  if(GeometryFail==2) ++m->Pfn[0];
  if(GeometryFail==3) ++m->Pfn[9];
  if(GeometryFail==4) m->Pfn[0]=ADMISSION_HVC_PHYSICAL_LIMIT>>PAGE_SHIFT;
  ++LiveMdls; return m;
}
static ULONG MmGetMdlByteCount(PMDL m) {return m->ByteCount;}
static PFN_NUMBER *MmGetMdlPfnArray(PMDL m) {return m->Pfn;}
static void *MmMapLockedPagesSpecifyCache(PMDL m,int mode,int cache,void *wanted,
                                        ULONG bugcheck,ULONG priority) {
  assert(RegistryDepth==1 && Current && !Current->Exited && !m->Mapped &&
      mode==UserMode && cache==MmWriteCombined && !wanted && !bugcheck &&
      priority==NormalPagePriority);
  if(MapFail==1) return NULL;
  if(MapFail==2) longjmp(ExceptionFrame,1);
  for(ULONG i=0;i<m->ByteCount;++i) assert(((unsigned char *)m->Data)[i]==0);
  m->Mapped=1; m->Owner=Current; ++Maps; return m->Data;
}
static void KeStackAttachProcess(PRKPROCESS p,KAPC_STATE *state) {
  assert(RegistryDepth==1 && !p->Exited); state->Previous=Current; Current=p; ++Attaches;
}
static void KeUnstackDetachProcess(KAPC_STATE *state) {
  assert(RegistryDepth==1); Current=state->Previous;
}
static void MmUnmapLockedPages(PVOID va,PMDL m) {
  assert(RegistryDepth==1 && m->Mapped && va==m->Data && Current==m->Owner && !Current->Exited);
  pthread_mutex_lock(&Gate);
  if(HoldUnmap) {
    UnmapEntered=1; pthread_cond_broadcast(&GateCond);
    while(!ReleaseUnmap) pthread_cond_wait(&GateCond,&Gate);
  }
  pthread_mutex_unlock(&Gate);
  m->Mapped=0; ++Unmaps;
}
static void MmFreePagesFromMdl(PMDL m) {
  assert(!RegistryDepth && !m->Mapped && LiveMdls); --LiveMdls;
  free(m->Data); free(m->Pfn); m->Data=NULL; m->Pfn=NULL;
}
#ifdef __cplusplus
extern "C" {
#endif
BOOLEAN AdmissionMemoryPoolChargeNative(ADMISSION_CONTEXT *a,ULONGLONG bytes) {
  assert(!RegistryDepth); if(ChargeFail) return FALSE;
  a->Charge+=bytes; if(ChargeHook) ChargeHook(); return TRUE;
}
VOID AdmissionMemoryPoolUnchargeNative(ADMISSION_CONTEXT *a,ULONGLONG bytes) {
  assert(!RegistryDepth && a->Charge>=bytes);
  if(UnchargeHook) UnchargeHook(a);
  a->Charge-=bytes;
}
#ifdef __cplusplus
}
#endif

#include "native_staging_windows.c"

static void uncharge_registered(ADMISSION_CONTEXT *a) {
  /* A concurrent Stop must still see the owner until pool access is over. */
  assert(!AdmissionNativeStagingAdapterIdle(a));
}
static void close_preparing(void) {
  assert(!AdmissionNativeStagingAdapterIdle(&Adapter));
  assert(!AdmissionNativeStagingProcessIdle(&Adapter,&GpuOwners[1]));
  AdmissionNativeStagingUnmapProcess(&Adapter,&GpuOwners[1]);
  AdmissionNativeStagingDestroyProcess(&Adapter,&GpuOwners[1]);
  assert(!AdmissionNativeStagingAdapterIdle(&Adapter));
}

static void clean(int n) {
  AdmissionNativeStagingUnmapProcess(&Adapter,&GpuOwners[n]);
  assert(AdmissionNativeStagingProcessIdle(&Adapter,&GpuOwners[n]));
  AdmissionNativeStagingDestroyProcess(&Adapter,&GpuOwners[n]);
}
static void map_pair(ULONGLONG *token,ULONGLONG *va,int n) {
  assert(NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[n],131072,token,va)));
  assert(*token && *va);
}
static void ownership(void) {
  ULONGLONG t,v,t2,v2; PVOID binding=NULL, refused=NULL;
  map_pair(&t,&v,0); map_pair(&t2,&v2,0);
  assert(t2>t && v2!=v && Adapter.Charge==262144);
  Notify(NULL,(HANDLE)(uintptr_t)Owner.Id,TRUE);
  assert(!Unmaps);
  assert(!AdmissionNativeStagingAdapterIdle(&Adapter));
  assert(AdmissionNativeStagingAdapterIdle(&OtherAdapter));
  assert(!AdmissionNativeStagingBind(&OtherAdapter,t,0,65536,&binding));
  Current=&Other;
  assert(!AdmissionNativeStagingBind(&Adapter,t,0,65536,&binding));
  Current=&Owner;
  assert(!AdmissionNativeStagingBind(&Adapter,t,~0ULL-65535,65536,&binding));
  assert(!AdmissionNativeStagingBind(&Adapter,t,65536,131072,&binding));
  assert(!AdmissionNativeStagingBind(&Adapter,t,0,~0ULL-65535,&binding));
  assert(!AdmissionNativeStagingBind(&Adapter,t,1,65536,&binding));
  assert(!AdmissionNativeStagingBind(&Adapter,t,0,4096,&binding));
  assert(AdmissionNativeStagingBind(&Adapter,t,65536,65536,&binding));
  assert(AdmissionNativeStagingOwns(binding,&Adapter,&GpuOwners[0]));
  assert(!AdmissionNativeStagingOwns(binding,&OtherAdapter,&GpuOwners[0]));
  assert(!AdmissionNativeStagingOwns(binding,&Adapter,&GpuOwners[1]));
  Current=&Other;
  assert(!AdmissionNativeStagingOwns(binding,&Adapter,&GpuOwners[0]));
  Current=&Owner;
  /* The first ring started at PFN 0x10000. Its second 64KiB chunk is at
   * PFN 0x10020 (the mock deliberately leaves an inter-chunk hole). */
  assert(AdmissionNativeStagingPage(binding,0,0x10020000ULL));
  assert(AdmissionNativeStagingPage(binding,4096,0x10021000ULL));
  assert(!AdmissionNativeStagingPage(binding,0,0x10010000ULL));
  assert(!AdmissionNativeStagingPage(binding,65536,0x10040000ULL));
  assert(!AdmissionNativeStagingPage(binding,~0ULL,0x10020000ULL));
  assert(!AdmissionNativeStagingPage(binding,1,0x10020001ULL));
  assert(!AdmissionNativeStagingProcessIdle(&Adapter,&GpuOwners[0]));
  AdmissionNativeStagingDestroyProcess(&Adapter,&GpuOwners[0]);
  assert(LiveMdls==2 && Adapter.Charge==262144);
  Notify(NULL,(HANDLE)(uintptr_t)Owner.Id,FALSE);
  assert(Unmaps==2 && LiveMdls==2);
  assert(!AdmissionNativeStagingBind(&Adapter,t,0,65536,&refused));
  assert(AdmissionNativeStagingPage(binding,4096,0x10021000ULL));
  AdmissionNativeStagingDestroyProcess(&Adapter,&GpuOwners[0]);
  assert(LiveMdls==2); /* WDDM still owns binding */
  AdmissionNativeStagingUnbind(binding);
  clean(0); assert(!LiveMdls && !Adapter.Charge && !Owner.References);
  assert(AdmissionNativeStagingAdapterIdle(&Adapter));
}
static void failures(void) {
  ULONGLONG t=99,v=99; PVOID b=NULL;
  assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[1],0,&t,&v)));
  assert(!t && !v);
  assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[1],4096,&t,&v)));
  assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[1],(128u<<20)+65536,&t,&v)));
  for(int mode=0;mode<9;++mode) {
    PoolFail=mode==0; ChargeFail=mode==1; MdlFail=mode==2;
    GeometryFail=(mode>=3 && mode<=6) ? mode-2 : 0;
    MapFail=mode>=7 ? mode-6 : 0;
    assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[1],65536,&t,&v)));
    assert(!t && !v && !LiveMdls && !LiveObjects && !Adapter.Charge && !Owner.References);
  }
  PoolFail=ChargeFail=MdlFail=GeometryFail=MapFail=0;
  ChargeHook=close_preparing;
  assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[1],65536,&t,&v)));
  ChargeHook=NULL;
  assert(!LiveObjects && !LiveMdls && !Adapter.Charge && !Owner.References);
  map_pair(&t,&v,1); PoolFail=1;
  assert(!AdmissionNativeStagingBind(&Adapter,t,0,65536,&b));
  PoolFail=0; clean(1); assert(!LiveObjects);
}
static void limits(void) {
  ULONGLONG t[5],v[5];
  for(int i=0;i<4;++i) map_pair(&t[i],&v[i],i);
  assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[4],65536,&t[4],&v[4])));
  AdmissionNativeStagingUnmapProcess(&Adapter,&GpuOwners[0]);
  assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[0],65536,&t[4],&v[4])));
  for(int i=0;i<4;++i) clean(i);
  assert(!LiveObjects && !LiveMdls && !Owner.References);
}
static void cancel_unexposed_to_gpu(void) {
  ULONGLONG t,v; PVOID binding=NULL;
  map_pair(&t,&v,3);
  Current=&Other;
  assert(AdmissionNativeStagingCancel(&Adapter,&GpuOwners[3],t)==STATUS_ACCESS_DENIED);
  Current=&Owner;
  assert(NT_SUCCESS(AdmissionNativeStagingCancel(&Adapter,&GpuOwners[3],t)));
  assert(!LiveMdls && !Adapter.Charge && !Owner.References);
  assert(!NT_SUCCESS(AdmissionNativeStagingCancel(&Adapter,&GpuOwners[3],t)));
  map_pair(&t,&v,3);
  assert(AdmissionNativeStagingBind(&Adapter,t,0,65536,&binding));
  assert(AdmissionNativeStagingCancel(&Adapter,&GpuOwners[3],t)==STATUS_DEVICE_BUSY);
  AdmissionNativeStagingUnbind(binding);
  /* A past binding may have reached a GPU graph. Zero current refs is not
   * evidence that the graph retired, so only DestroyProcess can free it. */
  assert(AdmissionNativeStagingCancel(&Adapter,&GpuOwners[3],t)==STATUS_DEVICE_BUSY);
  clean(3);
}
static void *foreign_unmap(void *unused) {
  (void)unused; Current=&Other;
  AdmissionNativeStagingUnmapProcess(&Adapter,&GpuOwners[6]); return NULL;
}
static void *exit_owner(void *unused) {
  (void)unused; Current=&Owner;
  pthread_mutex_lock(&Gate); ExitStarted=1; pthread_cond_broadcast(&GateCond);
  pthread_mutex_unlock(&Gate);
  Notify(NULL,(HANDLE)(uintptr_t)Owner.Id,FALSE);
  pthread_mutex_lock(&Gate); Owner.Exited=1; ExitFinished=1;
  pthread_cond_broadcast(&GateCond); pthread_mutex_unlock(&Gate); return NULL;
}
static void races(void) {
  for(int exitFirst=0;exitFirst<2;++exitFirst) {
    ULONGLONG t,v; pthread_t a,b;
    unsigned unmaps=Unmaps, attaches=Attaches;
    Current=&Owner; Owner.Exited=0; map_pair(&t,&v,6);
    HoldUnmap=1; UnmapEntered=ReleaseUnmap=ExitStarted=ExitFinished=0;
    assert(!pthread_create(&a,NULL,exitFirst ? exit_owner : foreign_unmap,NULL));
    pthread_mutex_lock(&Gate);
    while(!UnmapEntered) pthread_cond_wait(&GateCond,&Gate);
    pthread_mutex_unlock(&Gate);
    assert(!pthread_create(&b,NULL,exitFirst ? foreign_unmap : exit_owner,NULL));
    pthread_mutex_lock(&Gate);
    while(!ExitStarted) pthread_cond_wait(&GateCond,&Gate);
    assert(!ExitFinished && LiveMdls==1);
    ReleaseUnmap=1; pthread_cond_broadcast(&GateCond); pthread_mutex_unlock(&Gate);
    assert(!pthread_join(a,NULL)); assert(!pthread_join(b,NULL)); HoldUnmap=0;
    assert(Unmaps==unmaps+1 && Attaches==attaches+(exitFirst ? 0u : 1u));
    Current=&Other; clean(6); assert(!LiveMdls && !Owner.References);
  }
  Owner.Exited=0; Current=&Owner;
}
int main(int argc,char **argv) {
  ULONGLONG t,v;
  assert(argc==2);
  Current=&Owner; RegisterFail=1;
  assert(!NT_SUCCESS(AdmissionNativeStagingInitialize()) && !Notify);
  RegisterFail=0; assert(NT_SUCCESS(AdmissionNativeStagingInitialize()));
  UnchargeHook=uncharge_registered;
  if(!strcmp(argv[1],"unload-live")) {
    PVOID binding=NULL;
    map_pair(&t,&v,7);
    assert(AdmissionNativeStagingBind(&Adapter,t,0,65536,&binding));
    NATIVE_RING *retained=((NATIVE_BINDING *)binding)->Ring;
    unsigned removed=Unregisters;
    Current=&Other; RequireUnregisteredDereference=1;
    AdmissionNativeStagingUninitialize();
    assert(!Notify && Unregisters==removed+1 && Unmaps==Maps);
    assert(!Owner.References && LiveMdls==1 && LiveObjects==2 && Adapter.Charge==131072);
    assert(!AdmissionNativeStagingAdapterIdle(&Adapter));
    assert(!NT_SUCCESS(AdmissionNativeStagingInitialize()));
    assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[1],65536,&t,&v)));
    AdmissionNativeStagingDestroyProcess(&Adapter,&GpuOwners[7]);
    AdmissionNativeStagingUnbind(binding);
    assert(LiveMdls==1 && Adapter.Charge==131072);
    AdmissionNativeStagingUninitialize(); assert(Unregisters==removed+1);
    /* Fixture-only OS-reboot reclamation, AFTER asserting that the driver
     * retained uncertain physical backing and made no pool callout. */
    MmFreePagesFromMdl(retained->Mdl); ExFreePool(retained->Mdl);
    ExFreePoolWithTag(binding,ADMISSION_POOL_TAG);
    ExFreePoolWithTag(retained,ADMISSION_POOL_TAG); Adapter.Charge=0;
    puts("native staging lifetime PASS"); return 0;
  }
  ownership(); failures(); limits(); cancel_unexposed_to_gpu(); races();
  map_pair(&t,&v,7); unsigned before=Unregisters;
  clean(7);
  AdmissionNativeStagingUninitialize(); assert(Unregisters==before+1 && !Notify);
  assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[1],65536,&t,&v)));
  assert(!LiveObjects && !LiveMdls && !Adapter.Charge && !OtherAdapter.Charge);
  assert(Maps==Unmaps);
  assert(NT_SUCCESS(AdmissionNativeStagingInitialize()));
  NativeNextToken=MAXULONGLONG;
  t=v=99;
  assert(!NT_SUCCESS(AdmissionNativeStagingMap(&Adapter,&GpuOwners[0],65536,&t,&v)));
  assert(!t && !v && !LiveObjects && !LiveMdls && !Owner.References);
  AdmissionNativeStagingUninitialize(); assert(!Notify);
  puts("native staging lifetime PASS"); return 0;
}
