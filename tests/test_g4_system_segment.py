"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
An unsupported system-segment request must fail before runtime/scanout owners
start, not enter the G3 failure unwind after display acquisition. Execute the
real StartDevice prefix through its first runtime-start boundary; later startup
and hardware are deliberately outside this host fixture. Registry APIs and
firmware/arm callbacks are substitutes, not claims of Windows execution.
"""
import unittest
from test_apple_agx_pool_headroom import function
import test_g4_pool_residency_limit as policy
RENDER = policy.RENDER
from test_g4_sysmem_broker import run


class SystemSegment(unittest.TestCase):
    def test_only_dword_one_requests_system_segment(self):
        policy.PoolResidencyLimit.check_registry_reader(
            self, 'AdmissionSystemSegmentRequested', 'Wom1SystemSegment',
            source=(RENDER / 'src/lifecycle.c').read_text())

    def test_refusal_precedes_runtime_ownership(self):
        start = function((RENDER / 'src/lifecycle.c').read_text(),
                         'AdmissionDdiStartDevice')
        boundary = 'status = AdmissionInterruptStart(context);'
        # Keep every real statement/branch before and including the first
        # runtime start. A sentinel ends the fixture at that explicit boundary.
        prefix = start[:start.index(boundary) + len(boundary)]
        run(FIXTURE + prefix + '\n(void)post_display_route; return status;\n}\n' + CASES)


FIXTURE = r'''
#include <assert.h>
#include <string.h>
#include <wchar.h>
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(s) ((s)>=0)
#define STATUS_PENDING 1
#define STATUS_INVALID_PARAMETER -1
#define STATUS_NOT_SUPPORTED -2
#define STATUS_DEVICE_NOT_READY -3
#define STATUS_INVALID_DEVICE_STATE -4
#define PASSIVE_LEVEL 0
#define AdmissionReceiptStartEntered 1
#define AdmissionStartEntered 1
#define AdmissionReceiptStartDeviceInfo 2
#define AdmissionStartDeviceInfo 2
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef int NTSTATUS, BOOLEAN, APPLE_AGX_POST_DISPLAY_ROUTE;
typedef void *PVOID;
typedef unsigned long ULONG, *PULONG;
typedef struct {int unused;} DXGK_START_INFO, *PDXGK_START_INFO;
typedef struct {void *DeviceHandle; NTSTATUS (*DxgkCbGetDeviceInformation)(void *, int *);}
    DXGKRNL_INTERFACE, *PDXGKRNL_INTERFACE;
typedef struct {void *PhysicalDeviceObject; DXGK_START_INFO StartInfo;
 DXGKRNL_INTERFACE Interface; int InterfaceValid, DeviceInformation, Requested;
} ADMISSION_CONTEXT;
static unsigned starts, reads, arms, queried;
static int firmware=1, armed=1, info_status, last_status, irql;
static int KeGetCurrentIrql(void) {return irql;}
static int AdmissionSystemSegmentRequested(ADMISSION_CONTEXT *c) {
 assert(!starts && arms && !irql); ++reads;return c->Requested;
}
static NTSTATUS get_info(void *h,int *i) {(void)h;*i=7;++queried;return info_status;}
static void AdmissionRecordDevice(void *p,int stage,int status) {(void)p;(void)stage;(void)status;}
static void AdmissionRecordStartStage(ADMISSION_CONTEXT *c,int stage,int status) {
 (void)c;(void)stage;last_status=status;
}
static void AdmissionRecordTranslatedResources(ADMISSION_CONTEXT *c) {(void)c;}
static int AdmissionG3FirmwareResourcesPresent(ADMISSION_CONTEXT *c) {(void)c;return firmware;}
static int AdmissionConsumeGpuvaArm(ADMISSION_CONTEXT *c,const wchar_t *name,int report) {
 (void)c;assert(!wcscmp(name,L"G3Armed") && report);++arms;return armed;
}
static NTSTATUS AdmissionInterruptStart(ADMISSION_CONTEXT *c) {(void)c;++starts;return 123;}
'''

CASES = r'''
int main(void) {
 ADMISSION_CONTEXT c={0};DXGK_START_INFO start={0};
 DXGKRNL_INTERFACE i={NULL,get_info};ULONG sources=99,children=99;
 (void)AdmissionSystemSegmentRequested;(void)KeGetCurrentIrql;
 c.Requested=1;
 assert(AdmissionDdiStartDevice(&c,&start,&i,&sources,&children)==STATUS_NOT_SUPPORTED);
 assert(!sources && !children && arms==1 && reads==1 && !starts);
 assert(last_status==STATUS_NOT_SUPPORTED && queried==1);
 /* A fresh start after disabling the request reaches the unchanged boundary. */
 c.Requested=0;
 assert(AdmissionDdiStartDevice(&c,&start,&i,&sources,&children)==123);
 assert(starts==1 && arms==2 && reads==2);
 starts=0; c.Requested=1;
 assert(AdmissionDdiStartDevice(NULL,&start,&i,&sources,&children)==STATUS_INVALID_PARAMETER);
 assert(AdmissionDdiStartDevice(&c,NULL,&i,&sources,&children)==STATUS_INVALID_PARAMETER);
 assert(AdmissionDdiStartDevice(&c,&start,NULL,&sources,&children)==STATUS_INVALID_PARAMETER);
 assert(AdmissionDdiStartDevice(&c,&start,&i,NULL,&children)==STATUS_INVALID_PARAMETER);
 assert(AdmissionDdiStartDevice(&c,&start,&i,&sources,NULL)==STATUS_INVALID_PARAMETER);
 assert(reads==2 && arms==2 && !starts);
 firmware=0;
 assert(AdmissionDdiStartDevice(&c,&start,&i,&sources,&children)==STATUS_DEVICE_NOT_READY);
 assert(reads==2 && arms==2 && !starts);
 firmware=1;armed=0;
 assert(AdmissionDdiStartDevice(&c,&start,&i,&sources,&children)==STATUS_NOT_SUPPORTED);
 assert(reads==2 && arms==3 && !starts);
 armed=1;info_status=-8;
 assert(AdmissionDdiStartDevice(&c,&start,&i,&sources,&children)==-8);
 assert(reads==2 && arms==3 && !starts);
 info_status=0;irql=1;
 assert(AdmissionDdiStartDevice(&c,&start,&i,&sources,&children)==STATUS_INVALID_DEVICE_STATE);
 assert(reads==2 && arms==3 && !starts);
 return 0;
}
'''
