/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * The EXP2116 startup path can accept live additive mode while diagnostics
 * omit that fact. Run real G3 startup/readers/client/retained dispatch/broker.
 * Only Windows services and mailbox allocation are modeled. */
#include <wchar.h>
#include "apple_agx_gpuva_broker_v5_client.h"
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef unsigned char UCHAR;
typedef int BOOLEAN, NTSTATUS;
typedef void VOID, *HANDLE;
typedef struct {const wchar_t *text;} UNICODE_STRING;
typedef struct {ULONG TitleIndex,Type,DataLength;UCHAR Data[1];}
    KEY_VALUE_PARTIAL_INFORMATION,*PKEY_VALUE_PARTIAL_INFORMATION;
typedef struct {void *PhysicalDeviceObject,*GpuvaG3State;
    ULONG LiveAdditivePaging;struct {ULONG PagingNode;} ObjectAdapter;}
    ADMISSION_CONTEXT;
typedef struct {ADMISSION_CONTEXT *Adapter;int Lock,JobEvent,Processes;
    APPLE_AGX_GPUVA_V5_CLIENT Client;} ADMISSION_G3_STATE;
#define PASSIVE_LEVEL 0
#define TRUE 1
#define FALSE 0
#define STATUS_INVALID_DEVICE_STATE -1
#define STATUS_INSUFFICIENT_RESOURCES -2
#define STATUS_DEVICE_HARDWARE_ERROR -3
#define STATUS_NOT_SUPPORTED -4
#define STATUS_SUCCESS 0
#define POOL_FLAG_NON_PAGED 0
#define ADMISSION_POOL_TAG 0
#define NotificationEvent 0
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_QUERY_VALUE 1
#define KEY_SET_VALUE 2
#define REG_DWORD 4
#define REG_BINARY 3
#define KeyValuePartialInformation 2
#define NT_SUCCESS(s) ((s)>=0)
#define RtlZeroMemory(p,n) memset(p,0,n)
static struct fixture *active_fixture;
static unsigned mode,registry_value,registry_type,registry_length,queries,attached;
static int irql,query_result;
static ULONGLONG receipt[12];
static unsigned writes,opens,closes;
static int KeGetCurrentIrql(void) {return irql;}
static int IoOpenDeviceRegistryKey(void *pdo,int kind,int access,HANDLE *key) {
    assert(pdo && kind==1 && (access==1 || access==2));*key=pdo;++opens;return 0;
}
static void RtlInitUnicodeString(UNICODE_STRING *s,const wchar_t *text) {s->text=text;}
static NTSTATUS ZwQueryValueKey(HANDLE key,UNICODE_STRING *name,int which,
    PKEY_VALUE_PARTIAL_INFORMATION v,size_t cap,ULONG *bytes) {
    assert(key && which==2 && cap>=16);
    if (!wcscmp(name->text,L"Wom1SysmemGpuMap")) return -1;
    assert(!wcscmp(name->text,L"Wom1LiveAdditivePaging"));
    v->Type=registry_type;v->DataLength=registry_length;
    memcpy(v->Data,&registry_value,4);*bytes=16;return query_result;
}
static NTSTATUS ZwSetValueKey(HANDLE key,UNICODE_STRING *name,ULONG title,
    ULONG type,const void *data,ULONG bytes) {
    assert(key && !title && type==REG_BINARY && bytes==sizeof(receipt));
    assert(!wcscmp(name->text,L"Wom1LiveAdditiveState"));
    memcpy(receipt,data,bytes);++writes;return 0;
}
static void ZwClose(HANDLE key) {assert(key);++closes;}
static void *ExAllocatePool2(int flags,size_t bytes,int tag) {
    (void)flags;(void)tag;return malloc(bytes);
}
static void ExFreePoolWithTag(void *p,int tag) {(void)tag;free(p);}
static void ExInitializeFastMutex(int *p) {*p=1;}
static void KeInitializeEvent(int *p,int type,int value) {(void)type;*p=value;}
static void InitializeListHead(int *p) {*p=1;}
static void execute_with_fault(void *opaque,const AGX_GPUVA_V5_REQUEST *q,
    AGX_GPUVA_V5_RESPONSE *r) {
    gpuva_execute(opaque,q,r);
    if (q->Command!=AGX_GPUVA_V5_QUERY_CAPS) return;
    ++queries;
    if (mode==1) {r->Status=1;r->Reserved[0]=r->Reserved[1]=0;} /* old broker */
    if (mode==2) r->Reserved[0]=0;
    if (mode==3) r->Reserved[1]=2;
    if (mode==4) r->Flags=1;
    if (mode==5) ++r->Epoch;
    if (mode==6) --r->Receipt;
}
static bool w64(void *opaque,unsigned offset,unsigned long long value) {
    uint64_t word=value;
    return hv_agx_gpuva_v5_mmio(&gpuva_v5_wire,offset-AGX_GPUVA_V5_OFFSET,
        &word,true,3,execute_with_fault,opaque);
}
static bool r64(void *opaque,unsigned offset,unsigned long long *value) {
    uint64_t word=0;
    bool ok=hv_agx_gpuva_v5_mmio(&gpuva_v5_wire,offset-AGX_GPUVA_V5_OFFSET,
        &word,false,3,execute_with_fault,opaque);
    if (ok) *value=word;
    return ok;
}
static bool w32(void *opaque,unsigned offset,unsigned value) {
    uint64_t word=value;
    return hv_agx_gpuva_v5_mmio(&gpuva_v5_wire,offset-AGX_GPUVA_V5_OFFSET,
        &word,true,2,execute_with_fault,opaque);
}
static void barrier(void *opaque) {(void)opaque;}
static int AdmissionGpuvaV5ClientOpen(ADMISSION_CONTEXT *context,
    APPLE_AGX_GPUVA_V5_CLIENT *client) {
    APPLE_AGX_GPUVA_V5_IO io={active_fixture,w64,r64,w32,barrier,NULL};
    (void)context;return AppleAgxGpuvaV5ClientInit(client,&io);
}
static void AdmissionG3AttachMailbox(ADMISSION_G3_STATE *state) {
    assert(state->Client.Epoch==7);++attached;
}
/* ACTUAL_KMD_START */
int main(void) {
    for (unsigned scenario=0;scenario<13;++scenario) {
        ADMISSION_CONTEXT a={0};
        a.PhysicalDeviceObject=&a;
        mode=scenario<7?scenario:0;
        registry_value=scenario==7?0:scenario==8?2:1;
        registry_type=scenario==9?3:4;
        registry_length=scenario==10?1:scenario==11?8:4;
        query_result=scenario==12?-1:0;
        bool requested=scenario<7;
        bool enabled=scenario==0;
        active_fixture=new_fixture();
        memset(&gpuva_v5_wire,0,sizeof(gpuva_v5_wire));
        memset(receipt,0xa5,sizeof(receipt));
        queries=attached=writes=opens=closes=0;
        assert(AdmissionGpuvaG3Start(&a)==0);
        ADMISSION_G3_STATE *s=a.GpuvaG3State;
        assert(a.LiveAdditivePaging==enabled && a.ObjectAdapter.PagingNode==enabled);
        assert(queries==(unsigned)requested && attached==1);
        assert(writes==1); /* RED: no activation receipt in EXP2116. */
        assert(receipt[0]==1 && receipt[1]==96 && receipt[2]==requested);
        assert(receipt[3]==requested && receipt[4]==(requested && mode<5));
        assert(receipt[9]==enabled && receipt[10]==enabled && receipt[11]==7);
        if (requested && mode<5) {
            assert(receipt[5]==(mode==1?1u:0u));
            assert(receipt[6]==(mode==4?1u:0u));
            assert(receipt[7]==(mode==1 || mode==2?0u:2u));
            assert(receipt[8]==(mode==1?0u:mode==3?2u:1u));
        } else {
            assert(receipt[5]==0xffffffffu && receipt[6]==0 && !receipt[7] && !receipt[8]);
        }
        assert(opens==closes);
        if (enabled) {
            /* Same client/epoch continues into process creation/negotiation. */
            AGX_GPUVA_V5_REQUEST q={0};AGX_GPUVA_V5_RESPONSE r={0};
            q.Command=AGX_GPUVA_V5_CREATE;q.ProcessId=1;q.ProcessGeneration=1;q.TableIpa=P_ROOT;
            assert(AppleAgxGpuvaV5ClientCall(&s->Client,&q,&r) && !r.Status);
            memset(&q,0,sizeof(q));q.Command=AGX_GPUVA_V5_NEGOTIATE_LIVE;
            q.ProcessId=1;q.ProcessGeneration=1;q.Token=2;q.AllocationGeneration=1;
            assert(AppleAgxGpuvaV5ClientCall(&s->Client,&q,&r) && !r.Status && !r.Flags);
            assert(active_fixture->broker.processes[0].live_additive);
        }
        free(s);free(active_fixture);
    }
    puts("real startup activation receipt PASS");return 0;
}
