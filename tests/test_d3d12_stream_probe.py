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
    def test_large_statistics_verdict_checks_every_counter(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? RC57 must fail
        on wrong IA/VS/clipper counts or internal compute leaking into CS;
        a pixel-only pass cannot validate the open statistics query.
        """
        source=PROBE.read_text()
        begin=source.index('            stream_statistics_ok = q->IAVertices')
        assignment=source[begin:source.index(';',begin)+1]
        fields='IAVertices IAPrimitives VSInvocations GSInvocations GSPrimitives CInvocations CPrimitives PSInvocations HSInvocations DSInvocations CSInvocations'.split()
        code='#include <assert.h>\n#include <stdint.h>\nstruct stats {'+';'.join('uint64_t '+f for f in fields)+';};\n'
        code+='static int check(const struct stats *q,uint64_t draws){int stream_statistics_ok=0;'+assignment+'return stream_statistics_ok;}\n'
        code+='int main(void){struct stats q={0};uint64_t draws=2400*7;q.IAVertices=q.VSInvocations=6*draws;q.IAPrimitives=q.CInvocations=q.CPrimitives=2*draws;q.PSInvocations=1024*draws;assert(check(&q,draws));'
        for f in fields:
            code+='q.'+f+'++;'+('assert(!check(&q,draws));' if f!='PSInvocations' else 'assert(check(&q,draws));')+'q.'+f+'--;'
        code+='q.PSInvocations=0;assert(!check(&q,draws));}\n'
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'stats.c').write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Werror',str(p/'stats.c'),'-o',str(p/'stats')],check=True)
            subprocess.run([str(p/'stats')],check=True)

    def test_bounded_records_followup_and_counter_verdict(self):
        s = PROBE.read_text().split('static int draw_triangle(', 1)[1]
        s = s[:s.index('\n}\n')]
        flags = s[s.index('    /* EXP1369:'):s.index('    ID3D12CommandSignature *signature')]
        start = s.index('        {\n            /* C14:')
        records = s[start:s.index('        if (expanded)', start)]
        start = s.index('        if (indexed) {\n            D3D12_INDEX_BUFFER_VIEW')
        commands = s[start:s.index(' else if (expanded)', start)]
        query_start=s.rindex('        if (stream_large) {',0,start)
        query_begin=s[query_start:start]
        query_end=s.index('        if (stream_large && results && SUCCEEDED(hr))')
        query_resolve=s[query_end:s.index('        if (queried && results)',query_end)]
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
#define FAILED(h) ((h)<0)
static int IID_ID3D12CommandSignature;
static unsigned char buffer[65536],gpu_buffer[16];
static unsigned copies,copy_bytes[2],copy_dst_offset[2],copy_src_offset[2],transitions;
static unsigned direct,indexed_direct,indirect_calls,stride,kind,max_count,count_offset;
static int refuse;
static unsigned index_format,index_bytes,query_ends,query_begins,query_resolves,query_active,large_test;
typedef struct {unsigned Type,Count,NodeMask;} D3D12_QUERY_HEAP_DESC;
#define D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS 1
#define D3D12_HEAP_TYPE_READBACK 2
#define D3D12_RESOURCE_STATE_COPY_DEST 3
#define D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT 0x200
#define D3D12_HEAP_TYPE_DEFAULT 1
#define D3D12_RESOURCE_FLAG_NONE 0
#define E_FAIL -1
static void *c21_buffer(void *d,unsigned t,unsigned long long bytes,unsigned flags,unsigned state){
 assert(d&&t==1&&bytes==60000ull*20u+256u&&!flags&&state==3&&!copies);return gpu_buffer;}
static void ID3D12GraphicsCommandList_CopyBufferRegion(void *l,void *dst,unsigned long long dst_offset,void *src,unsigned long long src_offset,unsigned long long bytes){
 assert(l&&dst==gpu_buffer&&src==buffer&&copies<2&&!transitions&&!indirect_calls);
 copy_dst_offset[copies]=(unsigned)dst_offset;copy_src_offset[copies]=(unsigned)src_offset;copy_bytes[copies++]=(unsigned)bytes;}
static void transition(void *l,void *r,unsigned before,unsigned after){
 assert(l&&r==gpu_buffer&&before==3&&after==0x200&&copies==2&&!indirect_calls);++transitions;}
static int IID_ID3D12QueryHeap;
static HRESULT ID3D12Device_CreateQueryHeap(void *d,D3D12_QUERY_HEAP_DESC *q,int *iid,void **h){assert(d&&q->Type==1&&q->Count==1&&!q->NodeMask&&iid==&IID_ID3D12QueryHeap);*h=(void *)1;return 0;}
static void *committed_buffer(void *d,unsigned t,unsigned s){assert(d&&t==2&&s==3);return (void *)1;}
static void ID3D12GraphicsCommandList_BeginQuery(void *l,void *q,unsigned t,unsigned i){assert(l&&q&&t==1&&!i&&!query_active&&!indirect_calls);query_active=1;++query_begins;}
static void ID3D12GraphicsCommandList_ResolveQueryData(void *l,void *q,unsigned t,unsigned start,unsigned count,void *dst,unsigned offset){assert(l&&q&&t==1&&!start&&count==1&&dst&&!offset&&!query_active&&query_ends==1&&indexed_direct==1);++query_resolves;}
#define D3D12_QUERY_TYPE_PIPELINE_STATISTICS 1
static void ID3D12GraphicsCommandList_EndQuery(void *l,void *q,unsigned t,unsigned i){assert(l&&q&&t==1&&!i&&query_active&&!indexed_direct);query_active=0;++query_ends;}
static uint64_t ID3D12Resource_GetGPUVirtualAddress(void *p){assert(p==buffer);return 0x100000;}
static void ID3D12GraphicsCommandList_IASetIndexBuffer(void *l,D3D12_INDEX_BUFFER_VIEW *v){assert(l&&v->BufferLocation==0x100200);index_format=v->Format;index_bytes=v->SizeInBytes;}
static void ID3D12GraphicsCommandList_DrawIndexedInstanced(void *l,unsigned n,unsigned i,unsigned start,int bias,unsigned first){assert(l&&n==6&&i==1&&!start&&!bias&&!first&&!query_active);++indexed_direct;}
static void ID3D12GraphicsCommandList_DrawInstanced(void *l,unsigned n,unsigned i,unsigned start,unsigned first){assert(l&&n==3&&i==1&&!start&&!first);++direct;}
static HRESULT ID3D12Device_CreateCommandSignature(void *d,D3D12_COMMAND_SIGNATURE_DESC *s,void *root,int *iid,void **sig){assert(d&&!root&&iid==&IID_ID3D12CommandSignature&&s->NumArgumentDescs==1&&!s->NodeMask);stride=s->ByteStride;kind=s->pArgumentDescs[0].Type;*sig=(void *)1;return refuse?-1:0;}
static void ID3D12GraphicsCommandList_ExecuteIndirect(void *l,void *sig,unsigned max,void *args,uint64_t offset,void *count,uint64_t at){
 assert(l&&sig&&((args==buffer&&offset==2048&&(count==buffer||count==NULL))||(args==gpu_buffer&&!offset&&count==gpu_buffer&&transitions==1)));assert(query_active==large_test);++indirect_calls;max_count=max;count_offset=(unsigned)at;}
static int verdict(int pass,int stream_probe,int stream_counter_ok,int stream_large,int stream_statistics_ok){
''' + verdict + '\n}\n' + r'''
static void run(int mode){
''' + flags + r'''
const char *step="host";unsigned char *bytes=buffer;
void *device=(void *)1,*list=(void *)1,*vertices=buffer,*signature=NULL,*gpu_args=NULL;
void *statistics=(void *)1,*results=NULL;
HRESULT hr=0;
memset(buffer,0xcd,sizeof(buffer));direct=indexed_direct=indirect_calls=query_ends=query_begins=query_resolves=query_active=0;large_test=mode>=23&&mode<=25;
copies=transitions=0;
''' + indices + records + query_begin + commands + query_resolve + r'''
if(mode==2){assert(indexed_direct==1&&!direct&&!indirect_calls);return;}
if(refuse){assert(!direct&&!indexed_direct&&!indirect_calls&&hr==-1);return;}
assert(query_ends==(large_test?1u:0u)&&query_begins==query_ends&&query_resolves==query_ends);
assert(indirect_calls==(mode==25?7u:1u)&&max_count==(mode==26?60000u:mode>=23?2400u:2u));
/* RC59G: both GPU copies precede the barrier and the ExecuteIndirect; the
 * DEFAULT count sits past MaxCount records, the live records are all 2400. */
if(mode==26){assert(copies==2&&transitions==1&&gpu_args==gpu_buffer);
 assert(!copy_dst_offset[0]&&copy_src_offset[0]==2048&&copy_bytes[0]==2400u*20u);
 assert(copy_dst_offset[1]==60000u*20u&&copy_src_offset[1]==2048u+2400u*20u&&copy_bytes[1]==4);
 assert(copy_src_offset[1]>=copy_src_offset[0]+copy_bytes[0]);
 const UINT *live=(const void *)(buffer+2048);assert(live[2399*5]==6&&live[2399*5+1]==1);}
else assert(!copies&&!transitions&&!gpu_args);
assert(stride==(indexed?20u:16u)&&kind==(indexed?1u:0u));
assert(count_offset==(mode==24?0u:mode==26?60000u*20u:mode>=23?2048u+2400u*20u:stream_probe?2112u:2080u));
const UINT *args=(const void *)(buffer+2048);
assert(args[0]==(indexed?6u:3u)&&args[1]==1&&args[2]==0&&args[3]==0);
assert(args[stride/4]==args[0]&&args[stride/4+1]==1);
unsigned expected_count=mode>=23?2400u:mode>=17?(unsigned)(mode-17)%3u:(mode==14?0u:1u);
if(mode!=24)assert(*(const UINT *)(buffer+(mode==26?2048u+2400u*20u:count_offset))==expected_count);
if(mode>=17){assert(indexed&&index_format==(mode>=20?42u:57u)&&index_bytes==(mode>=20?24u:12u));}
if(mode>=20){const UINT expected[6]={0,1,2,2,1,3};assert(!memcmp(buffer+512,expected,sizeof(expected)));}
assert(direct==(stream_probe&&!indexed?1u:0u)&&indexed_direct==(stream_probe&&indexed?1u:0u));
/* Literal good totals: 512 shaded pixels per triangle, 1024 per quad. */
unsigned green=indexed?1024u:512u;
UINT count=stream_probe?(expected_count*(mode==25?7u:1u)+1u)*green:green;const UINT *c=&count;
int counted_ok=0;
''' + counter + r'''
assert(counted_ok&&verdict(1,stream_probe,counted_ok,large_test,1)==0);
++count;
''' + counter + r'''
assert(!counted_ok&&verdict(1,1,counted_ok,large_test,1)==1);
assert(verdict(0,1,1,large_test,1)==1);
if(large_test)assert(verdict(1,1,1,1,0)==1);
}
int main(void){run(2);run(5);for(int m=14;m<=26;++m)run(m);refuse=1;for(int m=14;m<=26;++m)run(m);return 0;}
'''
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p/'probe.c').write_text(code)
            r = subprocess.run(['clang', '-std=c11', '-fsanitize=address,undefined',
                                str(p/'probe.c'), '-o', str(p/'probe')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([str(p/'probe')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
