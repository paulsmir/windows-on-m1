"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Input-only paging rings lose the first failure. Preserve each slot's first
status/identity under concurrent writers, count all failures, and decode only
published records. Instrument every registered NTSTATUS callback.
"""
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / 'drivers/apple-agx/render-admission/include'
SRC = ROOT / 'drivers/apple-agx/render-admission/src'

class StatusReceipt(unittest.TestCase):
    def test_first_failure_and_concurrent_counts(self):
        self.assertTrue((INC / 'render_ddi_status.h').exists(), 'missing bounded failure recorder')
        code = r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <thread>
#include <vector>
typedef uint32_t ULONG; typedef uint64_t ULONGLONG; typedef int32_t LONG;
typedef int64_t LONG64; typedef int32_t NTSTATUS;
#define NT_SUCCESS(s) ((s)>=0)
#define InterlockedIncrement64(p) __sync_add_and_fetch(p,1)
#define InterlockedCompareExchange(p,v,c) __sync_val_compare_and_swap(p,c,v)
#define InterlockedExchange(p,v) __sync_lock_test_and_set(p,v)
#include "render_ddi_status.h"
int main(){
 ADMISSION_DDI_STATUS_SLOT s={};
 AdmissionDdiStatusAccumulate(&s,0,1,2,3,4);
 assert(s.Count==0 && s.Ready==0);
 AdmissionDdiStatusAccumulate(&s,(NTSTATUS)0xc000000d,10,20,30,11);
 std::vector<std::thread> threads;
 for(int i=0;i<8;++i) threads.emplace_back([&](){for(int j=0;j<10000;++j)
  AdmissionDdiStatusAccumulate(&s,(NTSTATUS)0xc000009a,99,98,97,9);});
 for(auto &t:threads)t.join();
 assert(s.Count==80001 && s.Ready==2);
 assert(s.Qpc==10 && s.Allocation==20 && s.Object==30);
 assert(s.Operation==11 && s.Status==0xc000000d);
 assert(sizeof(s)==48);
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'t.cpp').write_text(code)
            subprocess.run(['clang++','-std=c++17','-pthread','-fsanitize=address,undefined','-I'+str(INC),str(p/'t.cpp'),'-o',str(p/'t')],check=True,capture_output=True)
            subprocess.run([str(p/'t')],check=True,capture_output=True)

    def test_decoder_rejects_truncation_and_retains_status(self):
        p=ROOT/'tools/decode_ddi_status.py'
        self.assertTrue(p.exists(), 'missing status decoder')
        spec=importlib.util.spec_from_file_location('decoder',p)
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        import struct
        data=struct.pack('<IIIIQQ',1,32+128*48,128,48,1234,24000000)
        data+=struct.pack('<qQQQIIII',7,100,0xabc,0xdef,11,0xc000000d,2,0)
        data+=bytes(127*48)
        r=mod.decode(data)
        self.assertEqual(r['failures'][0]['status'],'0xc000000d')
        self.assertEqual(r['failures'][0]['count'],7)
        self.assertEqual(r['failures'][0]['allocation'],'0xabc')
        with self.assertRaises(ValueError):mod.decode(data[:-1])

    def test_registration_coverage(self):
        p=INC/'render_ddi_status_wrappers.h'
        self.assertTrue(p.exists(), 'missing return-boundary wrappers')
        wrapped=set(re.findall(r'ADMISSION_STATUS_WRAP\(\d+,\s*(\w+)',p.read_text()))
        driver=(SRC/'driver.c').read_text()
        registered=set(re.findall(r'initialization\.Dxgk\w+\s*=\s*(\w+);',driver))
        no_status={'AdmissionDdiInterruptRoutine','AdmissionDdiDpcRoutine','AdmissionDdiResetDevice','AdmissionDdiUnload','AdmissionDdiControlEtwLogging','AdmissionDdiSetStablePowerState','AdmissionDdiSetRootPageTable'}
        self.assertEqual(registered-no_status,wrapped)
        self.assertIn('AdmissionInstallStatusReceipts(&initialization)',driver)
        checked=(SRC/'gpuva_g3_paging_windows.c').read_text()
        start=checked.index('NTSTATUS AdmissionGpuvaG3BuildPagingBufferChecked(')
        self.assertLess(checked.index('AdmissionRecordPagingInternalStatus(',start),checked.index('AdmissionG3PagingStatusAllowed(status',start))

    def test_wrappers_preserve_calls_and_return_values(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A diagnostic wrapper must invoke its original callback exactly once,
        preserve the returned NTSTATUS, and capture inputs before mutation.
        Compile all production wrappers with a host ABI shim (not a WDK build).
        """
        text=(INC/'render_ddi_status_wrappers.h').read_text()
        entries=re.findall(r'ADMISSION_STATUS_WRAP\((\d+),\s*(\w+),\s*\(([^)]+)\)',text)
        types=set()
        for _,_,sig in entries:
            for arg in sig.split(','):
                types.update(re.findall(r'\b[A-Z][A-Z0-9_]+\b',arg))
        code='#include <stdint.h>\n#include <stddef.h>\n#include <assert.h>\n'
        code+='typedef int32_t NTSTATUS; typedef uint64_t ULONGLONG; typedef uintptr_t ULONG_PTR;\n'
        for t in sorted(types | {'BOOLEAN','ULONG','ADMISSION_CONTEXT'}):
            if t=='CONST':continue
            if t.startswith('DXGKARG_') or t=='ADMISSION_CONTEXT':
                code+=f'typedef struct {{void *hAllocation; unsigned Operation;}} {t};\n'
            elif t.startswith('P') or t in ('HANDLE','LPCGUID'):
                code+=f'typedef void *{t};\n'
            else:code+=f'typedef unsigned {t};\n'
        code+='#define CONST const\n#define MAXULONG 0xffffffffu\n#define NT_SUCCESS(s) ((s)>=0)\n'
        code+='static unsigned calls, records; static NTSTATUS result;\n'
        code+='static BOOLEAN AdmissionRecordDdiStatus(ULONG id,NTSTATUS s,ULONG op,ULONGLONG a,ULONGLONG o){if(!NT_SUCCESS(s))++records;return 0;}\n'
        code+='static void AdmissionQueueDdiStatus(ADMISSION_CONTEXT *c){}\n'
        code+='static HANDLE AdmissionPagingStatusAllocation(const DXGKARG_BUILDPAGINGBUFFER *a){return a?a->hAllocation:NULL;}\n'
        fields=dict(re.findall(r'Init->(\w+) = (\w+)Status;',text))
        for _,name,sig in entries:
            code+=f'static NTSTATUS {name}({sig}){{++calls;return result;}}\n'
        code+='typedef struct {\n'
        for _,name,sig in entries:
            field=next(k for k,v in fields.items() if v==name)
            code+=f'NTSTATUS (*{field})({sig});\n'
        code+='} DRIVER_INITIALIZATION_DATA;\n#include "render_ddi_status_wrappers.h"\n'
        code+='int main(void){DRIVER_INITIALIZATION_DATA d={0};AdmissionInstallStatusReceipts(&d);\n'
        code+='DXGKARG_BUILDPAGINGBUFFER a={0};a.Operation=11;\n'
        code+='result=(NTSTATUS)0xc000000d;assert(d.DxgkDdiBuildPagingBuffer(NULL,&a)==result);assert(calls==1 && records==2);\n'
        code+='result=0;assert(d.DxgkDdiSubmitCommandVirtual(NULL,NULL)==0);assert(calls==2 && records==2);}\n'
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'t.c').write_text(code)
            r=subprocess.run(['clang','-std=c11','-Werror=implicit-function-declaration','-Werror=incompatible-function-pointer-types','-I'+str(INC),str(p/'t.c'),'-o',str(p/'t')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            subprocess.run([str(p/'t')],check=True)

    def test_registry_snapshot_has_one_serial_writer(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A stale profile snapshot must not overwrite a newer first failure
        published by the worker after terminal removal stops future refreshes.
        All publishers must queue the same serial worker, including startup.
        """
        from test_paging_ddi_no_registry_io import function
        text=(SRC/'receipts.c').read_text()
        for name in ('AdmissionRecordPagingProfile','AdmissionPagingReceiptsStart'):
            body=function(text,name)
            self.assertNotIn('AdmissionWriteDdiStatuses(',body)
            self.assertIn('AdmissionQueueDdiStatus(',body)
        worker=function(text,'AdmissionPagingReceiptWorker')
        self.assertIn('AdmissionWriteDdiStatuses(context)',worker)
