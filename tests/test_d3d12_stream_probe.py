"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The bounded hardware discriminator must issue a pure counted draw followed by
an ordinary draw, with a count outside both records, and fail on a wrong pixel
counter. Otherwise a missing/extra draw or lost VDM continuation can look like a
passing probe. Execute the actual probe's record/command/verdict fragments;
Windows/D3D calls are modeled, not GPU execution.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT/'drivers/apple-agx/windows/icd/agx_d3d12_probe.c'


class StreamProbe(unittest.TestCase):
    def test_bounded_records_followup_and_counter_verdict(self):
        s = PROBE.read_text().split('static int draw_triangle(', 1)[1]
        s = s[:s.index('\n}\n')]
        flags = s[s.index('    /* EXP1369:'):s.index('    ID3D12CommandSignature *signature')]
        start = s.index('        {\n            /* C14:')
        records = s[start:s.index('        if (expanded)', start)]
        start = s.index('        if (indexed) {\n            D3D12_INDEX_BUFFER_VIEW')
        commands = s[start:s.index(' else if (expanded)', start)]
        start = s.index('        if (stream_index32) {')
        indices = s[start:s.index('        memcpy(bytes + 256', start)]
        verdict = s[s.rindex('    return '):]
        counter = next(line.strip() for line in s.splitlines() if 'counted_ok = c[0]' in line)
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef unsigned UINT;typedef int HRESULT;
typedef struct {uint64_t BufferLocation;unsigned SizeInBytes,Format;} D3D12_INDEX_BUFFER_VIEW;
typedef struct {unsigned Type;} D3D12_INDIRECT_ARGUMENT_DESC;
typedef struct {unsigned ByteStride,NumArgumentDescs;D3D12_INDIRECT_ARGUMENT_DESC *pArgumentDescs;unsigned NodeMask;} D3D12_COMMAND_SIGNATURE_DESC;
#define DXGI_FORMAT_R16_UINT 57
#define DXGI_FORMAT_R32_UINT 42
#define D3D12_INDIRECT_ARGUMENT_TYPE_DRAW 0
#define D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED 1
#define SUCCEEDED(h) ((h)>=0)
static int IID_ID3D12CommandSignature;
static unsigned char buffer[4096];
static unsigned direct,indexed_direct,indirect_calls,stride,kind,max_count,count_offset;
static int refuse;
static unsigned index_format,index_bytes;
static uint64_t ID3D12Resource_GetGPUVirtualAddress(void *p){assert(p==buffer);return 0x100000;}
static void ID3D12GraphicsCommandList_IASetIndexBuffer(void *l,D3D12_INDEX_BUFFER_VIEW *v){assert(l&&v->BufferLocation==0x100200);index_format=v->Format;index_bytes=v->SizeInBytes;}
static void ID3D12GraphicsCommandList_DrawIndexedInstanced(void *l,unsigned n,unsigned i,unsigned start,int bias,unsigned first){assert(l&&n==6&&i==1&&!start&&!bias&&!first);++indexed_direct;}
static void ID3D12GraphicsCommandList_DrawInstanced(void *l,unsigned n,unsigned i,unsigned start,unsigned first){assert(l&&n==3&&i==1&&!start&&!first);++direct;}
static HRESULT ID3D12Device_CreateCommandSignature(void *d,D3D12_COMMAND_SIGNATURE_DESC *s,void *root,int *iid,void **sig){assert(d&&!root&&iid==&IID_ID3D12CommandSignature&&s->NumArgumentDescs==1&&!s->NodeMask);stride=s->ByteStride;kind=s->pArgumentDescs[0].Type;*sig=(void *)1;return refuse?-1:0;}
static void ID3D12GraphicsCommandList_ExecuteIndirect(void *l,void *sig,unsigned max,void *args,uint64_t offset,void *count,uint64_t at){assert(l&&sig&&args==buffer&&offset==2048&&count==buffer);++indirect_calls;max_count=max;count_offset=(unsigned)at;}
static int verdict(int pass,int stream_probe,int stream_counter_ok){
''' + verdict + '\n}\n' + r'''
static void run(int mode){
''' + flags + r'''
const char *step="host";unsigned char *bytes=buffer;
void *device=(void *)1,*list=(void *)1,*vertices=buffer,*signature=NULL;
HRESULT hr=0;
memset(buffer,0xcd,sizeof(buffer));direct=indexed_direct=indirect_calls=0;
''' + indices + records + commands + r'''
if(mode==2){assert(indexed_direct==1&&!direct&&!indirect_calls);return;}
if(refuse){assert(!direct&&!indexed_direct&&!indirect_calls&&hr==-1);return;}
assert(indirect_calls==1&&max_count==2);
assert(stride==(indexed?20u:16u)&&kind==(indexed?1u:0u));
assert(count_offset==(stream_probe?2112u:2080u));
const UINT *args=(const void *)(buffer+2048);
assert(args[0]==(indexed?6u:3u)&&args[1]==1&&args[2]==0&&args[3]==0);
assert(args[stride/4]==args[0]&&args[stride/4+1]==1);
unsigned expected_count=mode>=17?(unsigned)(mode-17)%3u:(mode==14?0u:1u);
assert(*(const UINT *)(buffer+count_offset)==expected_count);
if(mode>=17){assert(indexed&&index_format==(mode>=20?42u:57u)&&index_bytes==(mode>=20?24u:12u));}
if(mode>=20){const UINT expected[6]={0,1,2,2,1,3};assert(!memcmp(buffer+512,expected,sizeof(expected)));}
assert(direct==(stream_probe&&!indexed?1u:0u)&&indexed_direct==(stream_probe&&indexed?1u:0u));
/* Literal good totals: 512 shaded pixels per triangle, 1024 per quad. */
unsigned green=indexed?1024u:512u;
UINT count=stream_probe?(expected_count+1u)*green:green;const UINT *c=&count;
int counted_ok=0;
''' + counter + r'''
assert(counted_ok&&verdict(1,stream_probe,counted_ok)==0);
++count;
''' + counter + r'''
assert(!counted_ok&&verdict(1,1,counted_ok)==1);
assert(verdict(0,1,1)==1);
}
int main(void){run(2);run(5);for(int m=14;m<=22;++m)run(m);refuse=1;for(int m=14;m<=22;++m)run(m);return 0;}
'''
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p/'probe.c').write_text(code)
            r = subprocess.run(['clang', '-std=c11', '-fsanitize=address,undefined',
                                str(p/'probe.c'), '-o', str(p/'probe')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([str(p/'probe')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
