"""Capture allocation metadata while the paging callback owns its handle."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]


class RefusalProvenance(unittest.TestCase):
    def test_extended_refusal_reaches_registry_writer(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A producer receipt version bump must not be silently discarded by
        the registry writer, including the accompanying bind refusal.
        """
        header = (ROOT / 'drivers/apple-agx/render-admission/include/render_admission.h').read_text()
        track = header[header.index('typedef struct _ADMISSION_G3_ALLOC_TRACK'):
                       header.index('typedef struct _ADMISSION_G3_LEAF_HISTORY_SNAPSHOT')]
        receipt = 'struct _ADMISSION_G4_SUBMIT_FAILURE {' + header.split(
            'struct _ADMISSION_G4_SUBMIT_FAILURE {', 1)[1].split('} G4SubmitFailure;', 1)[0] + '};'
        writer = body((ROOT / 'drivers/apple-agx/render-admission/src/receipts.c').read_text(),
                      'AdmissionRecordG4SubmitFailure')
        program = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
typedef uint64_t ULONGLONG; typedef unsigned ULONG; typedef void *HANDLE;
#define _Use_decl_annotations_
#define PASSIVE_LEVEL 0
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_SET_VALUE 2
#define NT_SUCCESS(x) ((x)>=0)
''' + track + receipt + r'''
typedef struct { HANDLE PhysicalDeviceObject; int G4SubmitFailureClaim, G4SubmitFailureCount, G4BindRefusalClaim;
  struct _ADMISSION_G4_SUBMIT_FAILURE G4SubmitFailure; unsigned G4BindRefusal; } ADMISSION_CONTEXT;
static unsigned writes, binds, flushes, closes;
static struct _ADMISSION_G4_SUBMIT_FAILURE saved;
static int KeGetCurrentIrql(void) { return 0; }
static int InterlockedCompareExchange(int *p,int a,int b) { (void)a;(void)b;return *p; }
static int IoOpenDeviceRegistryKey(HANDLE p,unsigned a,unsigned b,HANDLE *key) {
  (void)a;(void)b;*key=p;return 0;
}
static void WriteBinary(HANDLE key,const wchar_t *name,const void *data,unsigned bytes) {
  assert(key);
  if(!wcscmp(name,L"Wom1G4SubmitFailure")) {
    assert(bytes==sizeof(saved)); memcpy(&saved,data,bytes); ++writes;
  } else { assert(!wcscmp(name,L"Wom1G4BindRefusal") && bytes==sizeof(unsigned)); ++binds; }
}
static int ZwFlushKey(HANDLE key) { assert(key);++flushes;return 0; }
static void ZwClose(HANDLE key) { assert(key);++closes; }
''' + writer + r'''
int main(void) {
  ADMISSION_CONTEXT ctx={0}; ctx.PhysicalDeviceObject=&ctx;
  ctx.G4SubmitFailureClaim=ctx.G4BindRefusalClaim=2; ctx.G4SubmitFailureCount=7;
  ctx.G4SubmitFailure.Version=4; ctx.G4SubmitFailure.Bytes=sizeof(saved);
  ctx.G4SubmitFailure.LogicalAllocation[2]=0x1234;
  ctx.G4SubmitFailure.PrepareMask=0x86cd; ctx.G4SubmitFailure.PrepareSite=3;
  ctx.G4SubmitFailure.PrepareEntryState=1;
  ctx.G4SubmitFailure.TrackFound=1; ctx.G4SubmitFailure.Track.ClassId=3;
  AdmissionRecordG4SubmitFailure(&ctx);
  assert(writes==1 && binds==1 && flushes==1 && closes==1);
  assert(saved.Version==4 && saved.Bytes==456 && saved.TotalFailures==7);
  assert(saved.PrepareMask==0x86cd && saved.PrepareSite==3 && saved.PrepareEntryState==1);
  assert(offsetof(struct _ADMISSION_G4_SUBMIT_FAILURE, PrepareMask)==392);
  assert(offsetof(struct _ADMISSION_G4_SUBMIT_FAILURE, PrepareEntryState)==452);
  assert(saved.LogicalAllocation[2]==0x1234 && saved.Track.ClassId==3);
  --ctx.G4SubmitFailure.Bytes; AdmissionRecordG4SubmitFailure(&ctx);
  assert(writes==1 && binds==1);
  ctx.G4SubmitFailure.Bytes=sizeof(saved); ctx.G4SubmitFailure.Version=3;
  AdmissionRecordG4SubmitFailure(&ctx); assert(writes==1 && binds==1);
}
'''
        with tempfile.TemporaryDirectory(prefix='t7-publish-') as tmp:
            path = Path(tmp) / 'replay.c'; binary = Path(tmp) / 'replay'
            path.write_text(program)
            result = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', str(path), '-o', str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_paging_copies_metadata_before_handle_lifetime_ends(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A later G4 refusal must identify class and last paging outcome from
        copied metadata, without dereferencing a possibly retired allocation.
        A reused diagnostic bucket must not retain another handle's metadata.
        """
        source = (ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c').read_text()
        # Only WDK object boundaries are substituted; both receipt functions
        # and their data layout are extracted from repository sources.
        header = (ROOT / 'drivers/apple-agx/render-admission/include/render_admission.h').read_text()
        track = header[header.index('#define ADMISSION_G3_ALLOC_TRACK_COUNT'):
                       header.index('typedef struct _ADMISSION_G3_LEAF_HISTORY_SNAPSHOT')]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t ULONGLONG; typedef uintptr_t ULONG_PTR;
typedef unsigned ULONG, UINT; typedef void VOID; typedef int NTSTATUS;
typedef void *HANDLE;
#define MAXULONG UINT32_MAX
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((int)0xc000000d)
#define ADMISSION_ALLOCATION_OBJECT_MAGIC 0x1234u
#define ADMISSION_G3_LEAF_RING 8u
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef struct { ULONGLONG QuadPart; } LARGE_INTEGER;
static LARGE_INTEGER KeQueryPerformanceCounter(void *p) {
  (void)p; static ULONGLONG n; return (LARGE_INTEGER){++n};
}
typedef struct { struct { UINT Magic; struct { ULONGLONG Size; UINT Type, CpuVisible; } Description; } Object;
  UINT Win32ClassId, Win32Flags; } ADMISSION_ALLOCATION_HANDLE;
typedef struct { UINT Valid, Segment; } DXGK_PTE;
typedef struct { HANDLE hAllocation; DXGK_PTE *pPageTableEntries;
  UINT NumPageTableEntries, StartIndex; ULONGLONG FirstPteVirtualAddress;
  struct { UINT Repeat, InitialUpdate, NotifyEviction, Use64KBPages, NativeFence; } Flags;
} DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE;
typedef struct { ULONGLONG Qpc, ProcessId, TableIpa, Allocation, FirstVa, MappingGeneration;
  UINT First, Count, ValidCount, Flags, Status, FirstSegment; } ADMISSION_G3_LEAF_HISTORY;
''' + track + r'''
typedef struct { ADMISSION_G3_LEAF_HISTORY LeafHistory[ADMISSION_G3_LEAF_RING];
 UINT LeafHistoryNext; ADMISSION_G3_ALLOC_TRACK AllocTrack[ADMISSION_G3_ALLOC_TRACK_COUNT]; } ADMISSION_G3_STATE;
typedef struct { ADMISSION_G3_STATE *State; struct { ULONGLONG ProcessId, MappingGeneration; } Graph; } ADMISSION_G3_PROCESS;
''' + body(source, 'AdmissionG3RecordLeaf') + r''' 
int main(void) {
  ADMISSION_G3_STATE *state = calloc(1, sizeof(*state));
  ADMISSION_G3_PROCESS process = {0}; process.State=state;
  process.Graph.ProcessId=71; process.Graph.MappingGeneration=3;
  ADMISSION_ALLOCATION_HANDLE allocation={0};
  allocation.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
  allocation.Object.Description.Size=0x60000;
  allocation.Object.Description.Type=7;
  allocation.Object.Description.CpuVisible=1;
  allocation.Win32ClassId=2; allocation.Win32Flags=5;
  DXGK_PTE pte={0}; pte.Valid=1; pte.Segment=3;
  DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE update={0};
  update.hAllocation=&allocation; update.pPageTableEntries=&pte;
  update.NumPageTableEntries=1; update.FirstPteVirtualAddress=0x1100200000ULL;
  AdmissionG3RecordLeaf(&process,0x90000000ULL,&update,STATUS_SUCCESS);
  ULONGLONG key=(ULONGLONG)(ULONG_PTR)&allocation;
  ADMISSION_G3_ALLOC_TRACK *track=&state->AllocTrack[
      ((key>>6)^(key>>16))%ADMISSION_G3_ALLOC_TRACK_COUNT];
  assert(track->MetadataValid && track->ClassId==2 && track->Win32Flags==5);
  assert(track->AllocationBytes==0x60000 && track->AllocationType==7 && track->CpuVisible);
  assert(track->LastStatus==0 && track->LastSegment==3 && track->Maps==1);
  pte.Segment=0; update.Flags.NotifyEviction=1;
  AdmissionG3RecordLeaf(&process,0x90000000ULL,&update,STATUS_INVALID_PARAMETER);
  assert(track->LastStatus==(ULONG)STATUS_INVALID_PARAMETER && track->LastFlags==4);
  assert(track->LastSegment==0 && track->Maps==2);
  memset(&allocation,0,sizeof(allocation));
  assert(track->ClassId==2 && track->AllocationBytes==0x60000);
  /* Native fences are explicitly not allocation pointers. Never dereference. */
  update.hAllocation=(HANDLE)(ULONG_PTR)0x8000; update.Flags.NativeFence=1;
  key=0x8000; track=&state->AllocTrack[((key>>6)^(key>>16))%ADMISSION_G3_ALLOC_TRACK_COUNT];
  track->Allocation=key+1; track->MetadataValid=1; track->ClassId=99;
  AdmissionG3RecordLeaf(&process,0x90000000ULL,&update,STATUS_INVALID_PARAMETER);
  assert(track->Allocation==key && !track->MetadataValid && !track->ClassId);
  free(state);
  puts("G4 paging provenance: PASS");
}
'''
        with tempfile.TemporaryDirectory(prefix='t7-provenance-') as tmp:
            path = Path(tmp) / 'replay.c'; binary = Path(tmp) / 'replay'
            path.write_text(program)
            result = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                '-fsanitize=address,undefined', '-I', str(ROOT / 'tests'),
                '-I', str(ROOT / 'drivers/apple-agx/shared/include'),
                str(path), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('G4 paging provenance: PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
