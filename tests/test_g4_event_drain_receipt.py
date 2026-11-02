"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Event-drain failures must export their raw message in the normal G3 build,
not compile to a SubmitQualification-only no-op. Execute the actual writer;
invalid size/version/IRQL must still refuse registry access, and either key
must work when the other is unavailable. Registry APIs are host substitutes.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RA = ROOT / 'drivers/apple-agx/render-admission'


def preprocess(text, qualified):
    text = re.sub(r'^\s*#include[^\n]*', '', text, flags=re.M)
    cmd = ['clang', '-E', '-P', '-x', 'c', '-DAPPLE_AGX_GPUVA_G3_QUALIFICATION=1']
    if qualified:
        cmd += ['-DAPPLE_AGX_SUBMIT_QUALIFICATION=1']
    return subprocess.run(cmd + ['-'], input=text, text=True,
                          capture_output=True, check=True).stdout


class EventDrainReceipt(unittest.TestCase):
    def test_normal_and_qualified_exports(self):
        for qualified in (False, True):
            with self.subTest(qualified=qualified):
                header = preprocess((RA/'include/render_admission.h').read_text() +
                    '\nvoid probe(void) { AdmissionRecordEventDrain(ctx, rec); }\n', qualified)
                self.assertTrue('AdmissionRecordEventDrain(ctx, rec);' in header,
                              'event-drain call compiled to no-op')
                src = preprocess((RA/'src/receipts.c').read_text(), qualified)
                self.assertIn('VOID AdmissionRecordEventDrain(', src)
                start = src.index('static VOID AdmissionWriteEventDrain(')
                end = src.index('\n}', src.index('VOID AdmissionRecordEventDrain(', start))+2
                body = src[start:end]
                raw = (RA/'include/render_admission.h').read_text()
                layout = raw[raw.index('#define ADMISSION_EVENT_DRAIN_RECEIPT_VERSION'):raw.index('#define ADMISSION_QUEUE_FAULT_SNAPSHOT_VERSION')]
                code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
typedef uint32_t ULONG; typedef unsigned char UCHAR; typedef void VOID;
typedef void *HANDLE; typedef int OBJECT_ATTRIBUTES, UNICODE_STRING;
typedef struct { void *PhysicalDeviceObject; } ADMISSION_CONTEXT;
#define _Use_decl_annotations_
#define APPLE_AGX_G13_EVENT_MESSAGE_SIZE 56u
#define PASSIVE_LEVEL 0
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_SET_VALUE 2
#define OBJ_CASE_INSENSITIVE 4
#define OBJ_KERNEL_HANDLE 8
#define NT_SUCCESS(s) ((s)>=0)
static int irql, device_fail, service_fail, opens, writes, closes;
static unsigned char saved[96];
static int KeGetCurrentIrql(void) { return irql; }
static int IoOpenDeviceRegistryKey(void *p,int a,int b,HANDLE *key) {
  (void)p;(void)a;(void)b; ++opens; *key=(void *)1;return device_fail?-1:0;
}
static int ZwOpenKey(HANDLE *key,int a,OBJECT_ATTRIBUTES *b) {
  (void)a;(void)b;++opens;*key=(void *)2;return service_fail?-1:0;
}
static void ZwClose(HANDLE key) { (void)key;++closes; }
#define RtlInitUnicodeString(a,b) ((void)(a),(void)(b))
#define InitializeObjectAttributes(a,b,c,d,e) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e))
static void WriteBinary(HANDLE key,const wchar_t *name,const void *data,ULONG size) {
  assert(key && wcscmp(name,L"Wom1EventDrainReceipt")==0 && size==96);
  memcpy(saved,data,size);++writes;
}
''' + layout + body + r'''
int main(void) {
  ADMISSION_CONTEXT ctx={(void *)3}; ADMISSION_EVENT_DRAIN_RECEIPT r={0};
  _Static_assert(sizeof(r)==96,"wire ABI");
  r.Version=1;r.Bytes=sizeof(r);r.Fence=467;r.PollGuard=2;r.DrainGuard=8;
  r.IngestGuard=1;r.RuntimeResult=1;r.MessageValid=1;memset(r.Message,0xa5,56);
  for(int d=0;d<2;++d) for(int s=0;s<2;++s) {
    device_fail=d;service_fail=s;opens=writes=closes=0;
    AdmissionRecordEventDrain(&ctx,&r);
    assert(opens==2 && writes==2-d-s && closes==writes);
    if(writes) assert(memcmp(saved,&r,sizeof(r))==0);
  }
  opens=0;
  AdmissionRecordEventDrain(NULL,&r);AdmissionRecordEventDrain(&ctx,NULL);
  r.Version=2;AdmissionRecordEventDrain(&ctx,&r);r.Version=1;
  --r.Bytes;AdmissionRecordEventDrain(&ctx,&r);++r.Bytes;
  irql=2;AdmissionRecordEventDrain(&ctx,&r);assert(!opens);
  irql=0;ctx.PhysicalDeviceObject=NULL;service_fail=0;writes=closes=0;
  AdmissionRecordEventDrain(&ctx,&r);assert(opens==1 && writes==1 && closes==1);
}
'''
                with tempfile.TemporaryDirectory() as td:
                    p=Path(td);(p/'t.c').write_text(code)
                    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                        '-Wconversion','-Wshorten-64-to-32','-ftrivial-auto-var-init=pattern',
                        '-fsanitize=address,undefined',str(p/'t.c'),'-o',str(p/'t')],check=True)
                    subprocess.run([str(p/'t')],check=True)
