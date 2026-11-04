"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
System paging used one map/copy/unmap per 4 KiB even for contiguous PFNs.
Run the real resolver/encoder/executor across the run limit and partial pages;
check exact contents, bounded mapping count, continuation and mapping failure.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body
from test_g4_paging_backing_identity import FIXTURE, RENDER, SHARED

class BulkPaging(unittest.TestCase):
    def test_bulk_system_transfer_and_fill(self):
        source = (RENDER/'src/gpuva_g3_paging_windows.c').read_text()
        funcs = '\n'.join(body(source, n) for n in (
            'AdmissionG3FindPagingEdge', 'AdmissionG3ResolveLogicalVa',
            'AdmissionG3EncodeVirtualPaging', 'AdmissionG3MapPagingIpa',
            'AdmissionG3FillPattern', 'AdmissionG3ExecuteVirtualPaging'))
        code = FIXTURE.split('int main(void)')[0]
        start = code.index('static unsigned char local[')
        end = code.index('static unsigned encoded;', start)
        code = code[:start] + '''static unsigned char local[0xc00000], sys[0x600000];
static unsigned maps, unmaps, fail_map;
static void *mapped; static UINT mapped_bytes;
''' + code[end:]
        start = code.index('static void *MmMapIoSpace(')
        end = code.index('@@FUNCS@@', start)
        code = code[:start] + '''static void *MmMapIoSpace(PHYSICAL_ADDRESS pa,UINT n,UINT cache){
 assert(cache==MmCached && !(n&4095)); ++maps;
 if(fail_map) return NULL;
 assert(!mapped && pa.QuadPart>=0x2000000 && n<=sizeof(sys));
 assert((ULONGLONG)pa.QuadPart-0x2000000<=sizeof(sys)-n);
 mapped=sys+(pa.QuadPart-0x2000000);mapped_bytes=n;return mapped;
}
static void MmUnmapIoSpace(void *p,UINT n){
 assert(p==mapped && n==mapped_bytes);mapped=NULL;++unmaps;
}
''' + code[end:]
        code = code.replace('@@FUNCS@@', funcs) + MAIN
        if os.environ.get('RC21_BENCH'):
            code = '#include <stdio.h>\n#include <time.h>\n' + code.replace('/* BENCH */', BENCH)
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp); (p/'t.c').write_text(code)
            r = subprocess.run(['clang', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I'+str(RENDER/'include'),
                '-I'+str(SHARED/'include'), str(p/'t.c'),
                str(RENDER/'src/render_paging.c'), '-o', str(p/'t')],
                capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([str(p/'t')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
            if os.environ.get("RC21_BENCH"):
                print(r.stdout, end="")

MAIN = r'''
int main(void){
 ADMISSION_G3_STATE state={0};ADMISSION_CONTEXT a={&state,0};DEVICE d={&a.ObjectAdapter};
 APPLE_AGX_GPUVA_G3_NODE middle={NULL,2,3,0},root={&middle,1,2,0};
 static APPLE_AGX_GPUVA_G3_LOGICAL_PTE ptes[8192];
 ADMISSION_G3_TABLE_SHADOW table={NULL,3,ptes};
 ADMISSION_G3_PROCESS p={{1,0,&root},&table,{0}};
 ADMISSION_RENDER_CONTEXT c={{123,&d},&p,0,1};
 unsigned char dma[4096];ADMISSION_PAGING_RECORD records[64];
 const UINT bytes=0x402001;
 for(UINT direction=0;direction<3;++direction)for(UINT phase=0;phase<4;++phase){
  for(UINT i=0;i<1536;++i){
   ptes[16+i].Flags=3;ptes[16+i].SegmentId=direction==1?0:2;
   ptes[16+i].GuestIpa=(direction==1?0x2000000:0x100000)+i*4096;
   ptes[4096+i].Flags=3;ptes[4096+i].SegmentId=direction==0?0:2;
   ptes[4096+i].GuestIpa=(direction==0?0x2000000:0x700000)+i*4096;
  }
  unsigned char *src=direction==1?sys:local,*dst=direction==0?sys:local+0x600000;
  memset(dst,0xee,0x600000);
  for(UINT i=0;i<bytes;++i)src[phase+i]=(unsigned char)(i*13+7);
  DXGKARG_BUILDPAGINGBUFFER x={0};x.Operation=8;x.hSystemContext=&c;
  x.TransferVirtual.SourceVirtualAddress=0x10000+phase;
  x.TransferVirtual.DestinationVirtualAddress=0x1000000+phase;
  x.TransferVirtual.TransferSizeInBytes=bytes;x.TransferVirtual.TransferDirection=direction;
  maps=unmaps=0;UINT covered=0,passes=0;
  do {
   x.pDmaBuffer=dma;x.DmaSize=sizeof(dma);x.pDmaBufferPrivateData=records;
   /* Force continuation exactly at the 4 MiB run bound. */
   x.DmaBufferPrivateDataSize=sizeof(records[0]);encoded=0;
   int status=AdmissionG3EncodeVirtualPaging(&a,&x);
   assert(status==(passes==0?STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER:0));
   assert(encoded==1 && records[0].Bytes==(passes==0?0x400000:0x2001));
   assert(AdmissionPagingRecordsValid(records,1,64,sizeof(ADMISSION_PAGING_MARKER)));
   assert(!AdmissionG3ExecuteVirtualPaging(&a,records));
   covered+=records[0].Bytes;++passes;
   assert(x.MultipassOffset==covered && passes<=2);
  }while(covered<bytes);
  assert(!memcmp(src+phase,dst+phase,bytes));
  assert(dst[phase+bytes]==0xee && (!phase || dst[phase-1]==0xee));
  assert(maps==(direction==2?0u:2u) && maps==unmaps && !mapped);
  if(direction!=2){
   fail_map=1;assert(AdmissionG3ExecuteVirtualPaging(&a,records)==STATUS_INSUFFICIENT_RESOURCES);
   assert(!mapped && maps==unmaps+1);fail_map=0;
  }
 }
 /* System fill preserves phase and both boundary bytes. */
 for(UINT i=0;i<4;++i){ptes[4096+i].SegmentId=0;ptes[4096+i].GuestIpa=0x2000000+i*4096;}
 memset(sys,0xee,sizeof(sys));
 DXGKARG_BUILDPAGINGBUFFER x={0};x.Operation=9;x.hSystemContext=&c;
 x.FillVirtual.DestinationVirtualAddress=0x1000003;x.FillVirtual.FillSizeInBytes=0x2ff9;
 x.FillVirtual.FillPattern=0x12345678;x.pDmaBuffer=dma;x.DmaSize=sizeof(dma);
 x.pDmaBufferPrivateData=records;x.DmaBufferPrivateDataSize=sizeof(records);encoded=0;
 assert(!AdmissionG3EncodeVirtualPaging(&a,&x) && encoded==1);
 assert(!AdmissionG3ExecuteVirtualPaging(&a,records));
 for(UINT i=0;i<0x2ff9;++i)assert(sys[3+i]==((unsigned char*)&x.FillVirtual.FillPattern)[i&3]);
 assert(sys[2]==0xee && sys[0x2ffc]==0xee && !state.Lock && !mapped);
 /* Different endpoint offsets must split at both page boundaries. */
 memset(&x,0,sizeof(x));x.Operation=8;x.hSystemContext=&c;
 x.TransferVirtual.SourceVirtualAddress=0x10001;x.TransferVirtual.DestinationVirtualAddress=0x1000002;
 x.TransferVirtual.TransferSizeInBytes=0x2001;x.TransferVirtual.TransferDirection=0;
 x.pDmaBuffer=dma;x.DmaSize=sizeof(dma);x.pDmaBufferPrivateData=records;
 x.DmaBufferPrivateDataSize=sizeof(records);encoded=0;
 assert(!AdmissionG3EncodeVirtualPaging(&a,&x) && encoded==5);
 for(UINT i=0;i<encoded;++i)assert(!AdmissionG3ExecuteVirtualPaging(&a,records+i));
 assert(!memcmp(local+1,sys+2,0x2001));
 /* BENCH */
}
'''

BENCH = r'''
 for(UINT i=0;i<1024;++i){ptes[4096+i].SegmentId=0;ptes[4096+i].GuestIpa=0x2000000+i*4096;}
 maps=unmaps=0;unsigned submissions=0;
 clock_t start=clock();
 for(UINT repeat=0;repeat<64;++repeat){
  memset(&x,0,sizeof(x));x.Operation=8;x.hSystemContext=&c;
  x.TransferVirtual.SourceVirtualAddress=0x10000;x.TransferVirtual.DestinationVirtualAddress=0x1000000;
  x.TransferVirtual.TransferSizeInBytes=0x400000;x.TransferVirtual.TransferDirection=0;
  do {
   x.pDmaBuffer=dma;x.DmaSize=sizeof(dma);x.pDmaBufferPrivateData=records;
   x.DmaBufferPrivateDataSize=sizeof(records);encoded=0;
   int status=AdmissionG3EncodeVirtualPaging(&a,&x);
   assert(status==0 || status==STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER);
   for(UINT i=0;i<encoded;++i)assert(!AdmissionG3ExecuteVirtualPaging(&a,records+i));
   ++submissions;
  }while(x.MultipassOffset<0x400000);
 }
 double elapsed=(double)(clock()-start)/CLOCKS_PER_SEC;
 assert(!memcmp(local,sys,0x400000));
 printf("host sanitized encoder+executor: 256 MiB, %.6f CPU s, %.1f MiB/s, %u buffers, %u maps, %u unmaps\n",elapsed,256/elapsed,submissions,maps,unmaps);
'''
