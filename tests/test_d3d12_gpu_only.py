"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

D3D12 CPU intent must reach the winsys, while formats requiring CPU fallback
must remain CPU-visible. These execute production owner constructor bodies.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_g4_gpu_only import DRIVER, run_c


class D3d12GpuOnly(unittest.TestCase):
    def test_owner_heap_cpu_intent(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? NOT_AVAILABLE must select GPU-only resource flags while upload/readback still eagerly map."""
        from test_umd_deallocated_slot_no_evict import function
        owner=(DRIVER/'mesa/winsys/agx_d3d12_windows.cpp').read_text()
        start=owner.index('extern "C" HRESULT AgxD3d12OwnerCreateBuffer(')
        body=owner[start:owner.index('\nextern "C"',start+1)]
        harness=r'''
#include <cassert>
#include <cstring>
#include <cstdint>
typedef int HRESULT,BOOL;
enum {E_INVALIDARG=-1,E_OUTOFMEMORY=-2,S_OK=0,PIPE_BUFFER=1,PIPE_FORMAT_R8_UNORM=1,
 PIPE_USAGE_STAGING=1,PIPE_BIND_VERTEX_BUFFER=1,PIPE_BIND_INDEX_BUFFER=2,
 PIPE_BIND_CONSTANT_BUFFER=4,PIPE_BIND_SHADER_BUFFER=8,PIPE_BIND_SAMPLER_VIEW=16,
 PIPE_RESOURCE_FLAG_UNMAPPABLE=256,AdmissionCpuOwnerPointer=1};
struct pipe_resource { unsigned target,format,width0,height0,depth0,array_size,usage,bind,flags; };
struct pipe_screen { pipe_resource *(*resource_create)(pipe_screen *,const pipe_resource *); };
struct AGX_D3D12_OWNER { pipe_screen *Screen; void *Context; };
struct AGX_D3D12_BUFFER { void *Resource; unsigned long long GpuVa; void *Cpu; unsigned long long Bytes; };
struct OwnerLock { OwnerLock(AGX_D3D12_OWNER *) {} };
static unsigned maps,marks;static pipe_resource created;static char cpu[16];
static pipe_resource *create(pipe_screen *,const pipe_resource *r) {created=*r;return &created;}
static void *AgxWin32AsahiBufferWriteMap(void *,pipe_resource *) {++maps;return cpu;}
static uint64_t AgxWin32AsahiBufferGpuAddress(pipe_resource *) {return 0x10000;}
static int AgxWin32AsahiBufferMarkValid(void *,pipe_resource *) {++marks;return 1;}
static void AgxWin32AsahiResourceRelease(pipe_resource **) {assert(0);}
static void owner_cpu_pointer(AGX_D3D12_OWNER *,AGX_D3D12_BUFFER *,unsigned) {}
@@BODY@@
int main() {
 pipe_screen s={create};AGX_D3D12_OWNER o={&s,&s};AGX_D3D12_BUFFER b={};
 assert(AgxD3d12OwnerCreateBuffer(&o,65536,0,&b)==S_OK);
 assert(created.flags==PIPE_RESOURCE_FLAG_UNMAPPABLE && !maps && marks==1 && !b.Cpu);
 assert(AgxD3d12OwnerCreateBuffer(&o,65536,1,&b)==S_OK);
 assert(!created.flags && maps==1 && marks==1 && b.Cpu==cpu);
}
'''.replace('@@BODY@@',body)
        with tempfile.TemporaryDirectory(prefix='t7h-owner-') as tmp:
            src,exe=Path(tmp)/'test.cpp',Path(tmp)/'test';src.write_text(harness)
            b=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_bc_volume_retains_cpu_fallback(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? BC3D copies cannot use the uncompressed staging render-target path."""
        owner=(DRIVER/'mesa/winsys/agx_d3d12_windows.cpp').read_text()
        start=owner.index('extern "C" HRESULT AgxD3d12OwnerCreateTexture3D(')
        start=owner.index('return E_NOTIMPL;',start)+len('return E_NOTIMPL;')
        end=owner.index('  struct pipe_resource *resource=',start)
        run_c(self,r'''
#include <assert.h>
#define PIPE_RESOURCE_FLAG_UNMAPPABLE 256u
/* Format-block classification is an existing owner dependency. */
static int AgxD3d12FormatBlock(unsigned f,unsigned *w,unsigned *h) {*w=*h=f==1?4:1;return 1;}
enum {PIPE_TEXTURE_2D=2, PIPE_BIND_RENDER_TARGET=1, PIPE_BIND_SAMPLER_VIEW=2, PIPE_BIND_DEPTH_STENCIL=4};
static int supports(void *screen,unsigned fmt,unsigned target,unsigned samples,unsigned storage,unsigned bind) {
 (void)screen;(void)target;(void)samples;(void)storage;(void)bind;return fmt!=2;
}
struct screen {int (*is_format_supported)(void *,unsigned,unsigned,unsigned,unsigned,unsigned);};
static unsigned flags_for(unsigned format) {
 struct screen scr={supports};struct {struct screen *Screen;} own={&scr},*owner=&own;
 struct {unsigned flags,format,bind;} templ={0,format,0};
@@BODY@@
 return templ.flags;
}
int main(void) {assert(flags_for(0)==256);assert(flags_for(1)==0);assert(flags_for(2)==0);}
'''.replace('@@BODY@@',owner[start:end]))

    def test_committed_2d_intent_and_staging_eligibility(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? T7h excluded the Z32F DEFAULT texture implicated by EXP1918U5. Single-plane depth must opt in via its color staging format; combined/stencil, CPU-accessible, placed, BC, MSAA and unsupported staging formats retain their backing."""
        ddi=(DRIVER/'d3d12/agx_d3d12_ddi.cpp').read_text()
        a=ddi.index('    const bool placed = !heap_arg')
        a=ddi.index('        const UINT bind =',a);b=ddi.index('        /* C7b:',a)
        owner=(DRIVER/'mesa/winsys/agx_d3d12_windows.cpp').read_text()
        c=owner.index('  if ((bind & AGX_D3D12_BIND_GPU_ONLY)');d=owner.index('  templ.bind =',c)
        h=r'''
#include <assert.h>
#include <stddef.h>
typedef unsigned UINT;
#define AGX_D3D12_BIND_RENDER_TARGET 1u
#define AGX_D3D12_BIND_SAMPLER 2u
#define AGX_D3D12_BIND_DEPTH_STENCIL 4u
#define AGX_D3D12_BIND_GPU_ONLY 8u
#define PIPE_BIND_RENDER_TARGET 1u
#define PIPE_BIND_SAMPLER_VIEW 2u
#define PIPE_TEXTURE_2D 2u
#define PIPE_RESOURCE_FLAG_UNMAPPABLE 256u
#define D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET 1u
#define D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE 2u
#define D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL 4u
#define D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE 1u
enum pipe_format { PIPE_FORMAT_NONE=100, PIPE_FORMAT_Z16_UNORM,
 PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_R16_UNORM, PIPE_FORMAT_R32_FLOAT,
 PIPE_FORMAT_Z24_UNORM_S8_UINT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,
 PIPE_FORMAT_S8_UINT, PIPE_FORMAT_Z24X8_UNORM };
static unsigned unsupported=9;
static int supports(void *s,unsigned f,unsigned t,unsigned n,unsigned m,unsigned bind) {
 (void)s;(void)t;assert(n==1 && m==1 && bind==3);
 /* Depth is not color-renderable: query the interchange format. */
 return f!=unsupported && (f<100 || f==PIPE_FORMAT_R16_UNORM || f==PIPE_FORMAT_R32_FLOAT);
}
static unsigned placement(unsigned cpu,unsigned resource_flags,unsigned placed,unsigned bw,unsigned bh,unsigned samples,unsigned format) {
 struct {unsigned CPUPageProperty;} heap={cpu},*heap_arg=placed?NULL:&heap;
 struct {unsigned Flags;} resource={resource_flags},*resource_arg=&resource;
 struct screen {int (*is_format_supported)(void *,unsigned,unsigned,unsigned,unsigned,unsigned);} screen={supports};
 struct {struct screen *Screen;} own={&screen},*owner=&own;
 struct {unsigned format,flags;} templ={format,0};
@@DDI@@
@@OWNER@@
 return templ.flags;
}
int main(void) {
 assert(placement(1,1,0,1,1,1,0)==256);
 assert(placement(1,2,0,1,1,1,0)==256); /* sampled color can still be staged */
 assert(!placement(2,1,0,1,1,1,0));assert(!placement(1,1,1,1,1,1,0));
 assert(!placement(1,2,0,4,4,1,0));assert(!placement(1,4,0,1,1,1,0));
 assert(!placement(1,1,0,1,1,4,0));assert(!placement(1,2,0,1,1,1,9));
 const unsigned depth[]={PIPE_FORMAT_Z16_UNORM,PIPE_FORMAT_Z32_FLOAT};
 const unsigned staging[]={PIPE_FORMAT_R16_UNORM,PIPE_FORMAT_R32_FLOAT};
 for(unsigned i=0;i<2;++i) {
   assert(placement(1,4,0,1,1,1,depth[i])==256);
   assert(placement(1,6,0,1,1,1,depth[i])==256);
   assert(placement(1,6,0,1,1,0,depth[i])==256);
   assert(!placement(2,6,0,1,1,1,depth[i]));
   assert(!placement(1,6,1,1,1,1,depth[i]));
   assert(!placement(1,6,0,1,1,4,depth[i]));
   unsupported=staging[i];assert(!placement(1,6,0,1,1,1,depth[i]));unsupported=9;
 }
 const unsigned excluded[]={PIPE_FORMAT_Z24_UNORM_S8_UINT,PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,
   PIPE_FORMAT_S8_UINT,PIPE_FORMAT_Z24X8_UNORM,PIPE_FORMAT_NONE};
 for(unsigned i=0;i<sizeof(excluded)/sizeof(excluded[0]);++i)
   assert(!placement(1,6,0,1,1,1,excluded[i]));
}
'''.replace('@@DDI@@',ddi[a:b]).replace('@@OWNER@@',owner[c:d])
        run_c(self,h)
