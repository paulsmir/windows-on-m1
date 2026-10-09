
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_fixed_panel.h"
#include "apple_agx_vsync.h"
#include <assert.h>
#include <limits.h>
typedef unsigned long long ULONGLONG;typedef long long LONGLONG;typedef unsigned ULONG;typedef void*PVOID;typedef void*HANDLE;typedef unsigned KIRQL;typedef int KSPIN_LOCK;
typedef struct{long long QuadPart;}LARGE_INTEGER;
typedef struct{int Armed;}KTIMER;
typedef struct{int Queued;}KDPC;
static ULONGLONG fakeNow;static unsigned fakeIrql;static unsigned freed,flushed,timer_sets;
#define PASSIVE_LEVEL 0
#define MAXLONGLONG LLONG_MAX
#define ADMISSION_SCANOUT_TAG 1
#define ADMISSION_SCANOUT_TIMEOUT_MS 2000ULL
#define STATUS_DEVICE_BUSY -3
#define STATUS_INVALID_PARAMETER -4
#define STATUS_INVALID_DEVICE_STATE -7
#define STATUS_UNSUCCESSFUL -5
#define STATUS_DEVICE_HARDWARE_ERROR -6
#define NT_SUCCESS(x) ((x)>=0)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define RtlCopyMemory(p,s,n) memcpy(p,s,n)
static ULONGLONG KeQueryInterruptTimePrecise(void*p){assert(p);*(ULONGLONG*)p=fakeNow;return fakeNow;}
static unsigned KeGetCurrentIrql(void){return fakeIrql;}
static void KeAcquireSpinLock(KSPIN_LOCK*p,KIRQL*o){(void)p;*o=fakeIrql;fakeIrql=2;}
static void KeReleaseSpinLock(KSPIN_LOCK*p,KIRQL o){(void)p;fakeIrql=o;}
static int KeSetTimer(KTIMER*t,LARGE_INTEGER d,KDPC*p){(void)d;(void)p;assert(fakeIrql<=2);t->Armed=1;timer_sets++;return 1;}
static int KeCancelTimer(KTIMER*t){t->Armed=0;return 1;}
static int KeRemoveQueueDpc(KDPC*d){d->Queued=0;return 1;}
static void KeFlushQueuedDpcs(void){flushed=1;}
static void ExFreePoolWithTag(void*p,unsigned tag){(void)p;(void)tag;assert(flushed);freed=1;}
/* EXP1091: AdmissionScanoutQueryTimeline snapshots into nonpaged pool; no scenario queries. */
#define POOL_FLAG_NON_PAGED 0x40ULL
#define STATUS_INSUFFICIENT_RESOURCES -8
static void*ExAllocatePool2(ULONGLONG f,unsigned long n,unsigned tag){(void)f;(void)n;(void)tag;assert(!"unexpected allocation");return NULL;}
typedef int32_t LONG; typedef int64_t LONG64; typedef int BOOLEAN; typedef int NTSTATUS; typedef void VOID;
#define TRUE 1
#define FALSE 0
#define STATUS_DEVICE_NOT_READY -1
#define STATUS_SUCCESS 0
#define _Use_decl_annotations_
#define DXGK_INTERRUPT_CRTC_VSYNC 1
#define DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2 10
typedef struct {unsigned LayerIndex;unsigned long long PresentId;struct{unsigned Value;}Flags;} DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO2;
#define RtlZeroMemory(p,n) memset(p,0,n)
static LONG InterlockedCompareExchange(volatile LONG*p,LONG v,LONG c){LONG o=*p;if(o==c)*p=v;return o;}
static LONG64 InterlockedCompareExchange64(volatile LONG64*p,LONG64 v,LONG64 c){LONG64 o=*p;if(o==c)*p=v;return o;}
static LONG InterlockedExchange(volatile LONG*p,LONG v){LONG o=*p;*p=v;return o;}
static LONG64 InterlockedExchange64(volatile LONG64*p,LONG64 v){LONG64 o=*p;*p=v;return o;}
static void InterlockedIncrement(volatile LONG*p){++*p;}
typedef struct {int InterruptType;struct{unsigned VidPnTargetId;struct{LONG64 QuadPart;} PhysicalAddress;}CrtcVsync;struct{unsigned VidPnTargetId,PhysicalAdapterMask,MultiPlaneOverlayVsyncInfoCount;DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO2*pMultiPlaneOverlayVsyncInfo;unsigned long long GpuFrequency,GpuClockCounter;}CrtcVsyncWithMultiPlaneOverlay2;} DXGKARGCB_NOTIFY_INTERRUPT_DATA;
typedef struct {void*ScanoutRuntime;int InterfaceValid;struct{void*DeviceHandle;void(*DxgkCbNotifyInterrupt)(void*,DXGKARGCB_NOTIFY_INTERRUPT_DATA*);int(*DxgkCbQueueDpc)(void*);void(*DxgkCbNotifyDpc)(void*);NTSTATUS(*DxgkCbSynchronizeExecution)(void*,BOOLEAN(*)(void*),void*,ULONG,BOOLEAN*);}Interface;volatile LONG LastInterruptStatus,InterruptCount,InterruptAckCount,SchedulerDpcPending,RenderDpcFence,DpcCount,SchedulerInitialized,SchedulerFaulted;} ADMISSION_CONTEXT;
static unsigned notifications,dpcs,notified_dpcs;static LONG64 last_address;static int last_type;static unsigned long long last_present_id;
static void notify(void*c,DXGKARGCB_NOTIFY_INTERRUPT_DATA*d){(void)c;notifications++;last_type=d->InterruptType;if(d->InterruptType==DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2){assert(d->CrtcVsyncWithMultiPlaneOverlay2.MultiPlaneOverlayVsyncInfoCount==1&&d->CrtcVsyncWithMultiPlaneOverlay2.pMultiPlaneOverlayVsyncInfo->LayerIndex==0);last_present_id=d->CrtcVsyncWithMultiPlaneOverlay2.pMultiPlaneOverlayVsyncInfo->PresentId;}else last_address=d->CrtcVsync.PhysicalAddress.QuadPart;}
static void notify_dpc(void*c){(void)c;notified_dpcs++;}
static int queue(void*c){(void)c;dpcs++;return 1;}
static unsigned irq;static APPLE_AGX_SCANOUT_U64 latch;
static APPLE_AGX_SCANOUT_BOOL read32(void*c,unsigned o,unsigned*v){(void)c;if(o!=APPLE_AGX_SCANOUT_MMIO_OFFSET+APPLE_AGX_SCANOUT_REG_IRQ_STATUS)return 0;*v=irq;return 1;}
static APPLE_AGX_SCANOUT_BOOL read64(void*c,unsigned o,APPLE_AGX_SCANOUT_U64*v){(void)c;if(o!=APPLE_AGX_SCANOUT_MMIO_OFFSET+APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE)return 0;*v=latch;return 1;}
static APPLE_AGX_SCANOUT_BOOL write32(void*c,unsigned o,unsigned v){(void)c;if(o==APPLE_AGX_SCANOUT_MMIO_OFFSET+APPLE_AGX_SCANOUT_REG_IRQ_ENABLE)return 1;if(o!=APPLE_AGX_SCANOUT_MMIO_OFFSET+APPLE_AGX_SCANOUT_REG_IRQ_STATUS)return 0;irq&=~v;return 1;}

static NTSTATUS sync_exec(void*d,BOOLEAN(*f)(void*),void*c,ULONG n,BOOLEAN*out){unsigned o=fakeIrql;(void)d;(void)n;fakeIrql=3;*out=f(c);fakeIrql=o;return 0;}

static ADMISSION_SCANOUT_RUNTIME *AdmissionScanoutGet(ADMISSION_CONTEXT*c){return c?c->ScanoutRuntime:0;}
static void setup(ADMISSION_CONTEXT*c,ADMISSION_SCANOUT_RUNTIME*r){memset(c,0,sizeof(*c));memset(r,0,sizeof(*r));c->ScanoutRuntime=r;c->InterfaceValid=1;c->Interface.DxgkCbSynchronizeExecution=sync_exec;c->Interface.DxgkCbNotifyDpc=notify_dpc;r->Timeline.Running=1;r->Timeline.Enabled=1;r->VsyncNotifyEnabled=1;fakeNow=10000000;fakeIrql=0;freed=flushed=timer_sets=0;c->Interface.DxgkCbNotifyInterrupt=notify;c->Interface.DxgkCbQueueDpc=queue;r->Adapter=c;r->IrqEnabled=1;r->Panel.Started=1;r->Panel.Scanout.Qualified=1;r->Panel.Scanout.AbiVersion=2;r->Panel.Scanout.Capabilities=APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES;r->Panel.Scanout.Io.Read32=read32;r->Panel.Scanout.Io.Read64=read64;r->Panel.Scanout.Io.Write32=write32;notifications=dpcs=0;last_address=0;irq=0;}
static void pending(ADMISSION_SCANOUT_RUNTIME*r,unsigned seq,LONG64 address){r->PendingValid=1;r->PresentGate=1;r->PendingSequence=seq;r->PendingPhysicalAddress=address;r->Panel.Scanout.PresentPending=1;r->Panel.Scanout.PendingPresentSequence=seq;irq=APPLE_AGX_SCANOUT_IRQ_LATCHED;latch=seq;}

static APPLE_AGX_SCANOUT_U64 AdmissionScanoutNow(void*p){(void)p;return fakeNow/10000;}

static void AdmissionPagingDpc(ADMISSION_CONTEXT*c){(void)c;}
static void AdmissionSchedulerDpc(ADMISSION_CONTEXT*c){(void)c;}
static void AdmissionRenderCorrelationDpcWindows(ADMISSION_CONTEXT*c,ULONG f,ULONGLONG t){(void)c;(void)f;(void)t;}
static ULONGLONG KeQueryInterruptTime(void){return fakeNow;}

typedef struct {ULONG NodeOrdinal,EngineOrdinal,LastAbortedFenceId;} DXGKARG_RESETENGINE;
static NTSTATUS reset_status;
static NTSTATUS AdmissionDdiResetEngine(HANDLE c,DXGKARG_RESETENGINE*a){(void)a;ADMISSION_SCANOUT_RUNTIME*r=((ADMISSION_CONTEXT*)c)->ScanoutRuntime;assert(!r->Timeline.Running&&!r->VsyncTimer.Armed&&flushed);return reset_status;}
