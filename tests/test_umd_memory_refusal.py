"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Allocation refusals must retain failure-time CPU-copy ownership before rollback
and cache reclamation. Cumulative traffic and final pool totals cannot answer
this. Exercise production receipt and rollback bodies, including >4 GiB totals,
transitions, mapped subsets, bounded output and unchanged allocation behavior. OS query failures must not masquerade as
zero available memory, truncate wide counters, or change caller LastError.
"""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest
from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src'

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <array>
#include <vector>
using UINT=unsigned; using APPLE_AGX_U64=uint64_t; using APPLE_AGX_U32=unsigned;
using HRESULT=int; using BOOL=int; using VOID=void; using BYTE=unsigned char;
using D3DKMT_HANDLE=unsigned;
#define TRUE 1
#define FALSE 0
#define APPLE_AGX_GPUVA_WINSYS 1
#define ARRAYSIZE(a) (sizeof(a)/sizeof(a[0]))
#define ZeroMemory(p,n) memset(p,0,n)
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
#define E_FAIL ((int)0x80004005)
#define E_OUTOFMEMORY ((int)0x8007000e)
#define MEM_RELEASE 1
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) 8u
struct ADMISSION_UMD_SCREEN_BUFFER {
 BOOL Active,Transition,Mapped; UINT KernelAllocation;
 APPLE_AGX_U64 Bytes; BYTE *PrivateStaging,*HeapStagingShadow;
 UINT StagingAllocation; BOOL CopyHeld=FALSE;
};
struct D3DDDICB_DEALLOCATE { UINT NumAllocations; const UINT *HandleList; };
struct CALLBACKS { HRESULT (*pfnDeallocateCb)(void *,D3DDDICB_DEALLOCATE *); };
struct ADMISSION_UMD_DEVICE {
 int ScreenBufferLock; APPLE_AGX_U64 OwnerCookie; UINT MemoryRefusalSnapshots;
 ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[8]; CALLBACKS *KernelCallbacks;
 struct { void *handle; } RuntimeDevice; BOOL DrawTerminal; HRESULT LastScreenError;
};
static void AcquireSRWLockExclusive(int *p) {assert(!*p);*p=1;}
static void ReleaseSRWLockExclusive(int *p) {assert(*p==1);*p=0;}
static ADMISSION_UMD_DEVICE *current;
struct Row { const char *name; HRESULT hr; std::array<UINT,16> v; };
static std::vector<Row> rows;
static void AdmissionUmdDiagnostic(const char *name,HRESULT hr,const UINT *v,UINT n) {
 assert(!current->ScreenBufferLock && n==16); Row r={name,hr,{}};
 memcpy(r.v.data(),v,n*4); rows.push_back(r);
}
static unsigned cleanup; static bool fail_cleanup;
static int VirtualFree(void *,size_t,unsigned) { assert(rows.size()>=6); ++cleanup;return 1; }
static HRESULT deallocate(void *,D3DDDICB_DEALLOCATE *p) {
 assert(rows.size()>=6 && p->NumAllocations==1);++cleanup;return fail_cleanup?E_FAIL:0;
}
using DWORD=unsigned; using SIZE_T=size_t;
struct MEMORYSTATUSEX { DWORD dwLength; uint64_t ullTotalPhys,ullAvailPhys,
 ullTotalPageFile,ullAvailPageFile; };
struct PROCESS_MEMORY_COUNTERS {};
struct PROCESS_MEMORY_COUNTERS_EX { DWORD cb; SIZE_T PrivateUsage,WorkingSetSize,
 PeakWorkingSetSize,PeakPagefileUsage; };
static DWORD last_error=99; static unsigned queries, query_mode;
[[maybe_unused]] static DWORD GetLastError() { return last_error; }
[[maybe_unused]] static void SetLastError(DWORD e) { last_error=e; }
[[maybe_unused]] static void *GetCurrentProcess() { return (void*)7; }
[[maybe_unused]] static BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX *m) {
 assert(!current->ScreenBufferLock && m->dwLength==sizeof(*m)); ++queries;
 m->ullAvailPhys=0x123456789ull; m->ullTotalPhys=0x23456789aull;
 m->ullAvailPageFile=0x3456789abull; m->ullTotalPageFile=0x456789abcull;
 last_error=123;return !(query_mode&1);
}
[[maybe_unused]] static BOOL K32GetProcessMemoryInfo(void *h,PROCESS_MEMORY_COUNTERS *base,DWORD cb) {
 auto *p=(PROCESS_MEMORY_COUNTERS_EX*)base;
 assert(!current->ScreenBufferLock && h==(void*)7 && cb==sizeof(*p) && p->cb==cb);++queries;
 p->PrivateUsage=0x56789abcdull;p->WorkingSetSize=0x6789abcdeull;
 p->PeakWorkingSetSize=0x789abcdefull;p->PeakPagefileUsage=0x89abcdef0ull;
 last_error=456;return !(query_mode&2);
}
@@RECEIPT@@
static int failure(ADMISSION_UMD_DEVICE *device,HRESULT result,UINT canonical,
                   BYTE *privateStaging,UINT failureStage,UINT staging=0,UINT BorrowedStaging=0) {
 auto *slot=&device->ScreenBuffers[7];
 APPLE_AGX_U64 token=0x123456789ull,Bytes=0x8000000;
 UINT ClassId=1,Flags=15; BOOL systemDirect=FALSE;
 @@FAILURE@@
 return 1;
}
static uint64_t wide(const Row &r,unsigned i) {return r.v[i]|(uint64_t(r.v[i+1])<<32);}
int main() {
 ADMISSION_UMD_DEVICE d={}; current=&d;CALLBACKS cb={deallocate};d.KernelCallbacks=&cb;
 d.OwnerCookie=0xabcdef0123456789ull;
 // 3 GiB canonical/private mapped; 2 GiB canonical/private/merge unmapped.
 d.ScreenBuffers[0]={1,0,1,1,0xc0000000,(BYTE*)1,nullptr,0};
 d.ScreenBuffers[1]={1,0,0,2,0x80000000,(BYTE*)2,(BYTE*)3,0};
 // GPU-only canonical contributes no private/merge storage.
 d.ScreenBuffers[2]={1,0,0,3,0x10000,nullptr,nullptr,0};
 d.ScreenBuffers[3]={0,0,1,9,0xffffffff,(BYTE*)4,(BYTE*)5,0}; // inactive
 d.ScreenBuffers[4]={1,1,1,8,0xffffffff,(BYTE*)6,(BYTE*)7,0}; // transitional
 d.ScreenBuffers[5]={1,0,0,7,0,nullptr,nullptr,0}; // retained unknown size
 d.ScreenBuffers[6]=d.ScreenBuffers[0];d.ScreenBuffers[6].CopyHeld=TRUE; // concurrent transfer
 d.ScreenBuffers[7]={1,1,0,0,0,nullptr,nullptr,0}; // current request
 auto before=d;
 assert(!failure(&d,E_OUTOFMEMORY,0,nullptr,1));
 assert(rows.size()==6 && queries==2 && last_error==99 && !cleanup && d.LastScreenError==E_OUTOFMEMORY);
 auto &h=rows[0];assert(!strcmp(h.name,"measure-memory-refusal") && h.hr==E_OUTOFMEMORY);
 assert(h.v[0]==2 && wide(h,1)==d.OwnerCookie && wide(h,3)==0x123456789ull);
 assert(h.v[5]==1 && h.v[6]==1 && h.v[7]==15 && wide(h,8)==0x8000000);
 assert(h.v[10]==1 && h.v[11]==0 && h.v[12]==7 && h.v[13]==2 && h.v[14]==1 && h.v[15]==1);
 // category, slots, bytes, mapped slots/bytes, unmapped slots/bytes
 for(unsigned i=1;i<4;++i) {
  assert(!strcmp(rows[i].name,"measure-memory-storage"));
  assert(rows[i].v[6]==i && rows[i].v[5]==1 && wide(rows[i],1)==d.OwnerCookie);
 }
 assert(rows[1].v[7]==3 && wide(rows[1],8)==0x140010000ull);
 assert(rows[1].v[10]==1 && wide(rows[1],11)==0xc0000000);
 assert(rows[1].v[13]==2 && wide(rows[1],14)==0x80010000);
 assert(rows[2].v[7]==2 && wide(rows[2],8)==0x140000000ull);
 assert(rows[2].v[13]==1 && wide(rows[2],14)==0x80000000);
 assert(rows[3].v[7]==1 && wide(rows[3],8)==0x80000000 && !rows[3].v[10]);
 assert(!memcmp(d.ScreenBuffers,before.ScreenBuffers,7*sizeof(d.ScreenBuffers[0])));
 assert(!d.ScreenBuffers[7].Active && !d.DrawTerminal && !d.ScreenBufferLock);
 // OS samples use full-width values and preserve the allocation caller's error.
 assert(!strcmp(rows[4].name,"measure-memory-system") && rows[4].v[6]==1 && !rows[4].v[7]);
 assert(wide(rows[4],8)==0x123456789ull && wide(rows[4],10)==0x23456789aull);
 assert(wide(rows[4],12)==0x3456789abull && wide(rows[4],14)==0x456789abcull);
 assert(!strcmp(rows[5].name,"measure-memory-process") && rows[5].v[6]==1 && !rows[5].v[7]);
 assert(wide(rows[5],8)==0x56789abcdull && wide(rows[5],10)==0x6789abcdeull);
 assert(wide(rows[5],12)==0x789abcdefull && wide(rows[5],14)==0x89abcdef0ull);
 query_mode=1;
 // Private allocation failure after AllocateCb success: preserve pending
 // ownership before freeing it; borrowed staging is never deallocated.
 d.ScreenBuffers[7]={1,1,0,0,0,nullptr,nullptr,0};
 assert(!failure(&d,E_OUTOFMEMORY,77,nullptr,2,88,88));
 assert(rows[6].v[10]==2 && rows[6].v[11]==(1|4) && cleanup==1);
 assert(!rows[10].v[6] && rows[10].v[7]==123 && rows[11].v[6]==1);
 for(unsigned j=8;j<16;++j) assert(!rows[10].v[j]);
 assert(last_error==99);query_mode=2;
 // Callback failure with a handle plus failed rollback retains the owner.
 fail_cleanup=true;d.ScreenBuffers[7]={1,1,0,0,0,nullptr,nullptr,0};
 assert(!failure(&d,E_FAIL,79,(BYTE*)9,1));
 assert(rows[12].v[11]==3 && cleanup==3 && d.DrawTerminal);
 assert(d.ScreenBuffers[7].KernelAllocation==79 && !d.ScreenBuffers[7].Transition);
 assert(rows[16].v[6]==1 && !rows[17].v[6] && rows[17].v[7]==456);
 for(unsigned j=8;j<16;++j) assert(!rows[17].v[j]);
 assert(last_error==99);query_mode=3;
 // Successful path produces no receipt; fourth failure exhausts budget.
 assert(failure(&d,0,77,(BYTE*)8,2) && rows.size()==18 && queries==6);
 assert(!failure(&d,E_FAIL,0,nullptr,1) && rows.size()==24 && queries==8 && last_error==99);
 assert(!failure(&d,E_FAIL,0,nullptr,1) && rows.size()==24 && queries==8 && last_error==99 && d.MemoryRefusalSnapshots==4);
 for(const auto &r:rows) {printf("%s hr=0x%08x pid=7 tid=8 session=1 qpc=9",r.name,unsigned(r.hr));
  for(auto w:r.v) printf(" %08x",w);puts("");}
}
'''

class MemoryRefusal(unittest.TestCase):
    def test_actual_failure_snapshot_before_rollback(self):
        text = (SRC / 'umd_win32_screen.c').read_text()
        receipt = function(text, 'AdmissionUmdScreenMemoryRefusal')
        self.assertTrue(receipt, 'missing failure-time ownership receipt')
        impl = function(text, 'AdmissionUmdScreenCreateClassBufferImpl')
        start = impl.index('  if (FAILED(result) || !canonical')
        end = impl.index('\n  AcquireSRWLockExclusive', impl.index('    return 0;', start))
        source = HARNESS.replace('@@RECEIPT@@', receipt).replace('@@FAILURE@@', impl[start:end])
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp)/'test.cpp', Path(tmp)/'test'
            src.write_text(source)
            built = subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            run = subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertEqual(len(run.stdout.splitlines()),24)
            decoder = self.decoder()
            decoded = decoder.decode(run.stdout)
            self.assertEqual(len(decoded),4)
            first=decoded[0]
            self.assertEqual(first['owner_cookie'],'0xabcdef0123456789')
            self.assertEqual(first['request']['bytes'],134217728)
            self.assertEqual(first['request']['stage'],'allocate-callback')
            self.assertEqual(first['system']['available_physical_bytes'],4886718345)
            self.assertEqual(first['process']['private_usage_bytes'],0x56789abcd)
            self.assertEqual(decoded[1]['system'],{'valid':False,'error':123})
            self.assertEqual(decoded[2]['process'],{'valid':False,'error':456})
            self.assertFalse(decoded[3]['system']['valid'])
            self.assertFalse(decoded[3]['process']['valid'])
            self.assertEqual(first['transitional_slots_excluded'],2)
            self.assertEqual(first['unknown_size_slots_excluded'],1)
            self.assertEqual(first['copy_held_slots_excluded'],1)
            self.assertEqual(first['storage']['canonical']['bytes'],5368774656)
            self.assertEqual(first['storage']['private_staging']['mapped_bytes'],3221225472)
            self.assertEqual(first['storage']['merge_baseline']['unmapped_bytes'],2147483648)
            self.assertEqual(decoded[1]['request']['stage'],'private-staging')
            self.assertEqual(decoded[2]['request']['pending'],['canonical','private_staging'])
            # Truncation, duplicate rows, bad version/category, inconsistent
            # totals and interleaved identities must not fabricate a full census.
            lines=run.stdout.splitlines()
            def changed(line, field, value):
                copy=lines.copy(); words=copy[line].split();words[-16+field]=value
                copy[line]=' '.join(words);return '\n'.join(copy)
            for bad in ('', '\n'.join(lines[:-1]), run.stdout+lines[0]+'\n',
                        changed(0,0,'00000009'), changed(1,6,'00000009'),
                        changed(1,7,'00000004'), changed(0,10,'00000009'),
                        changed(4,6,'00000002'), changed(4,7,'00000001'),
                        changed(10,8,'00000001'), changed(4,0,'00000001'),
                        '\n'.join([lines[0].rsplit(' ',1)[0]]+lines[1:])):
                with self.subTest(bad=bad[:90]), self.assertRaises(ValueError):
                    decoder.decode(bad)
            # v1 remains readable; no OS samples means unknown, not zero.
            legacy=[]
            for line in lines:
                if line.startswith(('measure-memory-system','measure-memory-process')):
                    continue
                w=line.split();w[-16]='00000001';legacy.append(' '.join(w))
            old=decoder.decode('\n'.join(legacy))
            self.assertIsNone(old[0]['system'])
            self.assertIsNone(old[0]['process'])
            self.assertEqual(old[0]['storage'],first['storage'])
            self.assertEqual(decoder.decode('\n'.join(reversed(lines))),decoded)
            other=run.stdout.replace('pid=7','pid=17')
            self.assertEqual(len(decoder.decode(run.stdout+other)),8)
            script=ROOT/'tools/decode_umd_memory.py'
            log=Path(tmp)/'trace.log';log.write_text(run.stdout)
            cli=subprocess.run(['python3',str(script),str(log)],capture_output=True,text=True)
            self.assertEqual(cli.returncode,0,cli.stderr)

    def decoder(self):
        path=ROOT/'tools/decode_umd_memory.py'
        self.assertTrue(path.exists(),'missing strict offline memory receipt decoder')
        spec=importlib.util.spec_from_file_location('memory_decoder',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        return module


if __name__ == '__main__':
    unittest.main()
