"""T13 actual-source replay of persistent heap coherence.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? CPU-visible D3D12 heaps
must never acquire scattered aperture backing; persistent CPU stores must reach
canonical storage before consuming submissions, and GPU writes must reach the
same pointer before completion, without erasing disjoint CPU stores.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src'

PRELUDE = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
using BYTE=unsigned char; using UINT=unsigned; using ULONG_PTR=uintptr_t;
using DWORD=unsigned; using SIZE_T=size_t; using PVOID=void *; using BOOL=int;
using ULONGLONG=unsigned long long;
#define TRUE 1
#define FALSE 0
#define MEM_COMMIT 1
#define MEM_RESERVE 2
#define PAGE_READWRITE 4
#define HEAP_ZERO_MEMORY 8
#define WRITE_WATCH_FLAG_RESET 1
#define APPLE_AGX_G3_COPY_CAPACITY 65536
#define APPLE_AGX_G3_COPY_UPLOAD 2
#define APPLE_AGX_G3_COPY_DOWNLOAD 3
#define CopyMemory memcpy
struct SYSTEM_INFO { DWORD dwPageSize; };
static void GetSystemInfo(SYSTEM_INFO *s) { s->dwPageSize=4096; }
static void *GetProcessHeap() { return nullptr; }
static bool alloc_fail;
static void *HeapAlloc(void *, unsigned, size_t n) { return alloc_fail?nullptr:calloc(1,n); }
static void HeapFree(void *, unsigned, void *p) { free(p); }
static void *VirtualAlloc(void *,size_t n,unsigned,unsigned) { return alloc_fail?nullptr:calloc(1,n); }
struct SYNC { bool Valid; };
static void AdmissionUmdStagingInvalidate(SYNC *s) {s->Valid=false;}
struct ADMISSION_UMD_SCREEN_BUFFER {
 BYTE *PrivateStaging, *HeapStagingShadow; ULONGLONG Bytes; BOOL WriteWatch, GpuWritten;
 SYNC Sync;
};
struct ADMISSION_UMD_DEVICE { ULONGLONG HeapStagingDirtyBytes,HeapStagingUploadBytes,HeapStagingDownloadBytes; };
struct APPLE_AGX_G3_COPY_REQUEST { UINT Operation,TransferBytes; ULONGLONG Offset; BYTE Data[65536]; };
static BYTE *base; static size_t length; static std::vector<size_t> dirty;
static std::vector<std::pair<size_t,size_t>> uploads,downloads;
static std::vector<BYTE> canonical;
static bool watch_fail,transfer_fail; static unsigned queries,fail_after;
static void write_byte(size_t at,BYTE value) {base[at]=value;dirty.push_back(at&~size_t(4095));}
static UINT GetWriteWatch(DWORD flags,PVOID ptr,SIZE_T bytes,PVOID *pages,ULONG_PTR *count,DWORD *granularity) {
 assert(ptr==base && bytes==length); ++queries;
 if(watch_fail)return 1;
 std::sort(dirty.begin(),dirty.end());dirty.erase(std::unique(dirty.begin(),dirty.end()),dirty.end());
 assert(*count>=dirty.size());*count=dirty.size();*granularity=4096;
 // Deliberately reversed: the OS does not promise sorted addresses.
 for(size_t i=0;i<dirty.size();++i)pages[i]=base+dirty[dirty.size()-i-1];
 if(flags&WRITE_WATCH_FLAG_RESET)dirty.clear();
 return 0;
}
static int copy_escape(ADMISSION_UMD_DEVICE *,APPLE_AGX_G3_COPY_REQUEST *p) {
 if(transfer_fail || (fail_after && uploads.size()==fail_after))return 0;
 assert(p->TransferBytes && p->TransferBytes<=65536 && p->Offset+p->TransferBytes<=length);
 if(p->Operation==APPLE_AGX_G3_COPY_UPLOAD) {
  uploads.push_back({p->Offset,p->TransferBytes});memcpy(canonical.data()+p->Offset,p->Data,p->TransferBytes);
 } else {
  downloads.push_back({p->Offset,p->TransferBytes});memcpy(p->Data,canonical.data()+p->Offset,p->TransferBytes);
 }
 return 1;
}
@@FUNCTIONS@@
int main() {
 length=3*65536+123; base=(BYTE*)calloc(1,length); canonical.assign(length,0x77);
 ADMISSION_UMD_DEVICE d={};ADMISSION_UMD_SCREEN_BUFFER s={base,nullptr,length,TRUE,FALSE,{false}};
 APPLE_AGX_G3_COPY_REQUEST p={};UINT transfers=0;ULONGLONG moved=0;
 // New/replaced backing has no proven contents: initialize once.
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved));
 assert(moved==length && canonical[0]==0 && canonical.back()==0 && s.Sync.Valid);
 uploads.clear();moved=0;transfers=0;
 BYTE *persistent=base;
 // Stores through a pointer retained across Execute/Unmap reach exactly dirty pages.
 write_byte(4097,0x31);write_byte(8193,0x32);write_byte(length-1,0x33);
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved));
 assert(moved==8192+123 && canonical[4097]==0x31 && canonical[8193]==0x32 && canonical.back()==0x33);
 assert(uploads.size()==2 && uploads[0].first==4096 && uploads[0].second==8192);
 assert(uploads[1].first==3*65536 && uploads[1].second==123);
 assert(base==persistent);
 uploads.clear();moved=0;
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && uploads.empty() && !moved);
 // A GPU writer needs a baseline before submission, not after GPU completion.
 s.GpuWritten=TRUE;
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && s.HeapStagingShadow);
 canonical[100]=0xab;canonical[9000]=0xcd;
 // Concurrent CPU store in a disjoint byte of the SAME page must survive copy-out.
 write_byte(101,0xee);
 assert(heap_staging_transfer(&d,&s,&p,true,&transfers,&moved));
 assert(base==persistent && base[100]==0xab && base[101]==0xee && base[9000]==0xcd);
 // Driver download writes may mark all pages. Only genuine CPU differences upload.
 for(size_t i=0;i<length;i+=4096)dirty.push_back(i);
 uploads.clear();moved=0;
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved));
 assert(moved==4096 && canonical[101]==0xee && canonical[100]==0xab);
 uploads.clear();moved=0;
 for(size_t i=0;i<length;i+=4096)dirty.push_back(i);
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && uploads.empty());
 // Failed watch/copy cannot erase stores by publishing clean state.
 watch_fail=true;write_byte(5000,0x61);
 assert(!heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && !s.Sync.Valid);
 watch_fail=false;transfer_fail=true;
 assert(!heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && !s.Sync.Valid);
 transfer_fail=false;moved=0;
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && moved==length && canonical[5000]==0x61);
 alloc_fail=true;write_byte(1,0x9a);
 assert(!heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && !s.Sync.Valid);
 alloc_fail=false;
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && canonical[1]==0x9a);
 // First copy succeeds, second fails AFTER write-watch reset. Retry must
 // restore all bytes, including the dirty page whose bit was consumed.
 uploads.clear();write_byte(0,0x91);write_byte(3*65536,0x92);fail_after=1;
 assert(!heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && !s.Sync.Valid);
 assert(uploads.size()==1 && canonical[0]==0x91 && canonical[3*65536]!=0x92);
 fail_after=0;moved=0;
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && moved==length && canonical[3*65536]==0x92);
 // Unwatched allocation fallback is conservative, never silently clean.
 s.WriteWatch=FALSE;write_byte(2,0x9b);
 assert(heap_staging_transfer(&d,&s,&p,false,&transfers,&moved) && canonical[2]==0x9b);
 free(s.HeapStagingShadow);free(base);
}
'''

class HeapStaging(unittest.TestCase):
    def test_actual_dirty_copy_and_readback_merge(self):
        text = (SRC/'umd_gpuva_windows.c').read_text()
        funcs = '\n'.join(function(text,n) for n in ('heap_staging_page_compare','heap_staging_transfer'))
        self.assertIn('static int heap_staging_transfer(',funcs,'missing T13 range synchronization')
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'replay.cpp'; exe=Path(tmp)/'replay'
            src.write_text(PRELUDE.replace('@@FUNCTIONS@@',funcs))
            built=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-Wall','-Wextra',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stderr)

    def test_production_selection_and_completion_order(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Staging must be owner-scoped and GPU completion must follow copy-out even with outgoing queue waits."""
        owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        screen=(SRC/'umd_win32_screen.c').read_text()
        gpu=(SRC/'umd_gpuva_windows.c').read_text()
        self.assertIn('APPLE_AGX_D3D12_HEAP_STAGING',owner)
        self.assertIn('systemDirect = !BorrowedStaging && Bytes <= classInfo->MaximumBytes;',screen)
        self.assertIn('device->HeapStaging',screen)
        transfer=function(gpu,'transfer_slot')
        self.assertLess(transfer.index('transfer_heap_slot('),transfer.index('staging_unchanged('))
        finish=function(gpu,'finish_submission')
        self.assertLess(finish.index('wait_object('),finish.index('transfer_held(device,true)'))
        self.assertLess(finish.index('transfer_held(device,true)'),finish.index('signal_render(device,internal+1)'))
        self.assertLess(finish.index('signal_render(device,internal+1)'),finish.index('*fence=internal+1'))
        ddi=(ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        # Existing tests execute MapHeap; this link guards its cached pointer contract.
        self.assertIn('*data = heap->cpu;',ddi)
        self.assertIn('!heap || heap->native || !heap->locks',ddi)

    def test_actual_finish_publishes_only_after_merged_copyout(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The queue-visible fence must not become observable before persistent READBACK bytes are copied out, including download failure."""
        import re
        text=(SRC/'umd_gpuva_windows.c').read_text()
        funcs='\n'.join(function(text,n) for n in ('heap_staging_page_compare','heap_staging_transfer'))
        finish=function(text,'finish_submission')
        finish=re.sub(r'#if defined\(APPLE_AGX_EXP907_FRAME_RECEIPT\).*?#endif\n','',finish,flags=re.S)
        prefix=PRELUDE[:PRELUDE.index('@@FUNCTIONS@@')]
        prefix=prefix.replace('struct ADMISSION_UMD_DEVICE { ULONGLONG',
            'struct ADMISSION_UMD_DEVICE { uint64_t PendingRenderFence,NextRenderFence; unsigned PendingRenderSequence,RenderSyncObject; bool DrawTerminal; ULONGLONG')
        harness=prefix+funcs+r'''
static ADMISSION_UMD_SCREEN_BUFFER *active;
static bool needs=true,wait_ok=true,signal_ok=true;
static unsigned phase,signals;
static bool completion_needs_download(ADMISSION_UMD_DEVICE *) {return needs;}
static int wait_object(ADMISSION_UMD_DEVICE *,unsigned,uint64_t fence) {assert(fence==41 && phase==0);phase=1;return wait_ok;}
static int transfer_held(ADMISSION_UMD_DEVICE *d,bool download) {
 assert(download && phase==1);phase=2; APPLE_AGX_G3_COPY_REQUEST p={};
 return heap_staging_transfer(d,active,&p,true,nullptr,nullptr);
}
static int signal_render(ADMISSION_UMD_DEVICE *,uint64_t fence) {
 assert(fence==42 && phase==2 && base[3]==0x71 && base[4]==0x82);++signals;phase=3;return signal_ok;
}
'''+finish+r'''
int main() {
 length=8192;base=(BYTE*)calloc(1,length);canonical.assign(length,0);
 ADMISSION_UMD_SCREEN_BUFFER s={base,nullptr,length,TRUE,TRUE,{false}};active=&s;
 ADMISSION_UMD_DEVICE d={};APPLE_AGX_G3_COPY_REQUEST p={};
 write_byte(5,0x42);
 assert(heap_staging_transfer(&d,&s,&p,false,nullptr,nullptr));
 // Consuming GPU work sees the CPU store, then writes its own output.
 assert(canonical[5]==0x42);canonical[3]=0x71;write_byte(4,0x82);
 uint64_t fence=0;
 assert(finish_submission(&d,41,9,&fence)==1 && fence==42 && signals==1 && !d.DrawTerminal);
 assert(base[3]==0x71 && base[4]==0x82);
 phase=0;transfer_fail=true;fence=0;
 assert(finish_submission(&d,41,10,&fence)==2 && d.DrawTerminal && !fence && signals==1);
 transfer_fail=false;phase=0;wait_ok=false;d.DrawTerminal=false;
 assert(finish_submission(&d,41,11,&fence)==2 && d.DrawTerminal && signals==1);
 needs=false;d.DrawTerminal=false;
 assert(finish_submission(&d,41,12,&fence)==1 && fence==41 && d.PendingRenderFence==41 && signals==1);
 free(s.HeapStagingShadow);free(base);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'fence.cpp';exe=Path(tmp)/'fence';src.write_text(harness)
            b=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

    def test_actual_allocation_policy_preserves_c75_and_removes_aperture(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? An UPLOAD heap or internal shader/encoder/query BO must request CpuVisible=0, while D3D11 C75c and shared/import paths retain their allocation policy."""
        screen=(SRC/'umd_win32_screen.c').read_text()
        start=screen.index('  systemDirect = !BorrowedStaging')
        end=screen.index('\n#else',start)
        policy=screen[start:end]
        harness=r'''
#include <cassert>
#include <cstdint>
using UINT=unsigned;using BOOL=int;using APPLE_AGX_U64=uint64_t;
#define FALSE 0
#define ADMISSION_WIN32_ALLOCATION_VERSION_LOCAL 2
#define ADMISSION_WIN32_ALLOCATION_GPU_LOCAL 3
#define AgxWin32BufferClassGeneral 1
#define AppleAgxWin32BufferCpuRead 1
#define AppleAgxWin32BufferCpuWrite 2
#define AppleAgxWin32BufferGpuRead 4
#define AppleAgxWin32BufferGpuWrite 8
#define AppleAgxWin32BufferGpuOnly 16
#define AppleAgxWin32BufferShareable 32
struct DEVICE {bool HeapStaging;};struct CLASS {uint64_t MaximumBytes;};
struct DESC {unsigned Version;struct {unsigned Type,CpuVisible;} Allocation;};
static DESC result;static BOOL direct;
static int select(DEVICE *device,unsigned ClassId,unsigned Flags,bool BorrowedStaging,uint64_t Bytes) {
 CLASS c={256ull<<20};auto *classInfo=&c;BOOL systemDirect=0;DESC description={1,{1,1}};
@@POLICY@@
 result=description;direct=systemDirect;return 1;
}
int main() {
 DEVICE d={};
 for(uint64_t size: {uint64_t(4096),uint64_t(16<<20),uint64_t(128<<20),uint64_t(256<<20)}) {
  for(unsigned cls=1;cls<=3;++cls) {
   unsigned flags=2|4|(cls==1?1|8:0);
   d.HeapStaging=false;assert(select(&d,cls,flags,false,size));assert(direct && result.Allocation.CpuVisible);
   d.HeapStaging=true;assert(select(&d,cls,flags,false,size));assert(!direct && !result.Allocation.CpuVisible && result.Allocation.Type==3);
  }
  assert(select(&d,1,16|4|8,false,size));assert(direct && !result.Allocation.CpuVisible);
  assert(select(&d,1,1|2|4|8|32,false,size));assert(direct && result.Allocation.CpuVisible);
  assert(select(&d,1,1|2|4|8,true,size));assert(!direct && !result.Allocation.CpuVisible);
 }
}
'''.replace('#include <cstdint>','#include <cstdint>\n#include <initializer_list>').replace('@@POLICY@@',policy)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'policy.cpp';exe=Path(tmp)/'policy';src.write_text(harness)
            b=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

    def test_copyout_never_reads_or_stores_gpu_unchanged_bytes(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Two volatile reads can observe a concurrent CPU store and then erase it, even when the GPU never changed that byte."""
        text=(SRC/'umd_gpuva_windows.c').read_text()
        funcs='\n'.join(function(text,n) for n in ('heap_staging_page_compare','heap_staging_transfer'))
        # Replace only the volatile access primitive with an adversarial
        # interleaving observer; execute the production condition and stores.
        funcs=funcs.replace('volatile BYTE *mapped = cpu + offset;', 'RacingBytes mapped = {cpu + offset};')
        prefix=PRELUDE[:PRELUDE.index('@@FUNCTIONS@@')]
        harness=prefix+r'''
static unsigned unchanged_reads,unchanged_stores;
struct RacingByte {
 BYTE *p; bool unchanged;
 operator BYTE() const { BYTE old=*p; if(unchanged) {++unchanged_reads;*p=0x83;}return old; }
 RacingByte &operator=(BYTE value) {if(unchanged)++unchanged_stores;*p=value;return *this;}
};
struct RacingBytes { BYTE *p; RacingByte operator[](size_t i) {return {p+i,p+i==base+101};} };
'''+funcs+r'''
int main() {
 length=4096;base=(BYTE*)calloc(1,length);canonical.assign(length,0);
 ADMISSION_UMD_DEVICE d={};ADMISSION_UMD_SCREEN_BUFFER s={base,nullptr,length,TRUE,TRUE,{false}};
 APPLE_AGX_G3_COPY_REQUEST p={};
 assert(heap_staging_transfer(&d,&s,&p,false,nullptr,nullptr));
 canonical[100]=0x72;
 assert(heap_staging_transfer(&d,&s,&p,true,nullptr,nullptr));
 assert(!unchanged_reads && !unchanged_stores && base[100]==0x72);
 free(s.HeapStagingShadow);free(base);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'race.cpp';exe=Path(tmp)/'race';src.write_text(harness)
            b=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

    def test_actual_transfer_wrapper_clean_probe_wait_and_query_failures(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Clean mapped heaps must avoid allocation/query/wait work; dirty uploads wait for readers, while readback must not replace lost GPU output with fresh storage."""
        text=(SRC/'umd_gpuva_windows.c').read_text()
        funcs='\n'.join(function(text,n) for n in ('heap_staging_page_compare','heap_staging_transfer','transfer_heap_slot'))
        prefix=PRELUDE[:PRELUDE.index('@@FUNCTIONS@@')]
        prefix=prefix.replace(' SYNC Sync;', ' SYNC Sync; unsigned SubmissionHolds, KernelAllocation; ULONGLONG LastUseFence, CanonicalGpuVa;')
        prefix=prefix.replace('struct ADMISSION_UMD_DEVICE { ULONGLONG','struct ADMISSION_UMD_DEVICE { unsigned RenderSyncObject; ULONGLONG')
        prefix=prefix.replace('struct APPLE_AGX_G3_COPY_REQUEST { UINT Operation,TransferBytes;', 'struct APPLE_AGX_G3_COPY_REQUEST { UINT Operation,TransferBytes,Magic,Version,Bytes,Allocation; ULONGLONG GpuVa,ProcessGeneration,MappingGeneration;')
        prefix=prefix.replace(' assert(*count>=dirty.size());', ''' if(*count<dirty.size()) {assert(!flags);*count=1;*pages=base+dirty[0];return 0;}
 assert(*count>=dirty.size());''')
        prefix=prefix.replace('static int copy_escape(', 'static unsigned canonical_queries,replacements,waits;static bool query_fail,wait_fail;\nstatic int copy_escape(')
        prefix=prefix.replace(' if(transfer_fail ||', ''' if(!p->Operation) {++canonical_queries;if(query_fail)return 0;p->ProcessGeneration=7;p->MappingGeneration=8;return 1;}
 if(transfer_fail ||''')
        # Definitions before the real wrapper, after platform/copy primitives.
        prefix+=r'''
#define APPLE_AGX_G3_COPY_MAGIC 0x43565041u
#define APPLE_AGX_G3_COPY_VERSION 1
#define APPLE_AGX_G3_COPY_QUERY 0
static int wait_object(ADMISSION_UMD_DEVICE *,unsigned,ULONGLONG) {++waits;return !wait_fail;}
static int query_canonical(ADMISSION_UMD_DEVICE *d,ADMISSION_UMD_SCREEN_BUFFER *s,APPLE_AGX_G3_COPY_REQUEST *p) {
 if(copy_escape(d,p))return 1;++replacements;s->Sync.Valid=false;return 0;
}
'''
        harness=prefix+funcs+r'''
int main() {
 length=131072;base=(BYTE*)calloc(1,length);canonical.assign(length,0);
 ADMISSION_UMD_DEVICE d={};ADMISSION_UMD_SCREEN_BUFFER s={base,nullptr,length,TRUE,FALSE,{false}};
 assert(transfer_heap_slot(&d,&s,false,nullptr,nullptr));assert(canonical_queries==1);
 // Clean execute does not allocate even its 64 KiB request, nor wait/query.
 alloc_fail=true;s.SubmissionHolds=2;s.LastUseFence=9;
 assert(transfer_heap_slot(&d,&s,false,nullptr,nullptr));assert(!waits && canonical_queries==1);
 alloc_fail=false;write_byte(4,0x19);write_byte(70000,0x29);wait_fail=true;
 assert(!transfer_heap_slot(&d,&s,false,nullptr,nullptr));assert(waits==1 && dirty.size()==2);
 wait_fail=false;
 assert(transfer_heap_slot(&d,&s,false,nullptr,nullptr));assert(waits==2 && canonical[4]==0x19 && canonical[70000]==0x29);
 query_fail=true;
 assert(!transfer_heap_slot(&d,&s,true,nullptr,nullptr));assert(!replacements && !s.Sync.Valid);
 assert(!transfer_heap_slot(&d,&s,false,nullptr,nullptr));assert(replacements==1);
 free(s.HeapStagingShadow);free(base);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'wrapper.cpp';exe=Path(tmp)/'wrapper';src.write_text(harness)
            b=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
