#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <pthread.h>
#include "render_live_paging_wait.h"
typedef void VOID;typedef void *PVOID;typedef void *PDEVICE_OBJECT;typedef void *PIO_WORKITEM;
typedef unsigned ULONG;typedef int LONG,BOOLEAN,KIRQL;typedef unsigned long long ULONGLONG;
typedef const wchar_t *PCWSTR;typedef pthread_mutex_t KSPIN_LOCK;typedef int KEVENT;
#define _In_
#define _In_opt_
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define IO_NO_INCREMENT 0
#define DelayedWorkQueue 1
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define RtlCopyMemory(d,s,n) memcpy(d,s,n)
typedef void IO_WORKITEM_ROUTINE(PDEVICE_OBJECT,void *);
/* The worker's other receipt payloads are not inputs to this test. */
typedef struct {ULONGLONG unused[4];} ADMISSION_G3_FLUSH_RECEIPT,
  ADMISSION_G3_PAGING_FAILURE, ADMISSION_G3_SYSTEM_SHAPE,ADMISSION_G3_BROKER_FAILURES,
  ADMISSION_OMITTED_LEAVES,ADMISSION_NATIVE_PTE_REFUSAL,ADMISSION_G3_PAGING_WAIT_RECEIPT,
  ADMISSION_POOL_ALLOC_PROBE_RECEIPT;
#define ADMISSION_G3_SYSTEM_SHAPE_COUNT 8
 typedef struct {struct {ULONG CommitFailures,ReleaseFailures,Unaligned,RefUnderflows;} Stats;} ADMISSION_POOL_DEMAND_RECEIPT;
typedef struct {void *PagingReceipts,*PhysicalDeviceObject;volatile LONG G3PagingWaitDirty;
 ADMISSION_G3_PAGING_WAIT_RECEIPT G3PagingWait;} ADMISSION_CONTEXT;
static void KeAcquireSpinLock(KSPIN_LOCK *l,KIRQL *i){*i=0;assert(!pthread_mutex_lock(l));}
static void KeReleaseSpinLock(KSPIN_LOCK *l,KIRQL i){(void)i;assert(!pthread_mutex_unlock(l));}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG n,LONG o){return __sync_val_compare_and_swap(p,o,n);}
static LONG InterlockedExchange(volatile LONG *p,LONG n){return __sync_lock_test_and_set(p,n);}
static void KeClearEvent(KEVENT *e){*e=0;}
static void KeSetEvent(KEVENT *e,LONG i,BOOLEAN w){(void)i;(void)w;*e=1;}
static unsigned queues,writes;static void (*queued_worker)(PDEVICE_OBJECT,void *);
static void IoQueueWorkItem(PIO_WORKITEM w,void (*f)(PDEVICE_OBJECT,void *),int q,void *c){(void)w;(void)q;(void)c;++queues;queued_worker=f;}
static void AdmissionWriteDdiStatuses(ADMISSION_CONTEXT *a){(void)a;abort();}
