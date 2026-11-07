/* R1 Standard IO span CPU contract; host WDK model, not Windows ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <climits>
#include <sys/mman.h>
#include <unistd.h>
using UINT=std::uint32_t;
using SIZE_T=std::size_t;
struct D3D12DDIARG_SIGNATURE_ENTRY { UINT system_value,reg; unsigned char mask; UINT component_type,min_precision; };
struct D3D12DDIARG_STAGE_IO_SIGNATURES {
 const D3D12DDIARG_SIGNATURE_ENTRY *pInputSignatureDeprecated;
 UINT NumInputSignatureEntries;
 const D3D12DDIARG_SIGNATURE_ENTRY *pOutputSignatureDeprecated;
 UINT NumOutputSignatureEntries;
};
struct D3D12DDIARG_CREATE_SHADER_0010 { union {const D3D12DDIARG_STAGE_IO_SIGNATURES *Standard; const void *Tessellation;} IOSignatures; };
struct AgxShaderTrustedSpan {const void *data;SIZE_T bytes;};
#include "agx_d3d12_r1_io_span.h"
static unsigned checks,failures;
static void check(bool good,const char*name){++checks;if(!good){++failures;std::printf("R1_IO_SPAN_FAIL %s\n",name);}}
int main(){
 AgxD3d12R1IoSpans out{{reinterpret_cast<const void*>(1),9},{reinterpret_cast<const void*>(1),9}};
 D3D12DDIARG_CREATE_SHADER_0010 arg{};
 check(!AgxD3d12AcquireR1IoSpans(nullptr,4,&out)&&!out.input.data&&!out.output.data,"null args zero output");
 check(!AgxD3d12AcquireR1IoSpans(&arg,4,nullptr),"null output refused");
 check(AgxD3d12AcquireR1IoSpans(&arg,4,&out)&&!out.input.data&&!out.output.data,"empty Standard");
 D3D12DDIARG_SIGNATURE_ENTRY input[2]{},output[3]{};
 D3D12DDIARG_STAGE_IO_SIGNATURES io{input,2,output,3};arg.IOSignatures.Standard=&io;
 check(AgxD3d12AcquireR1IoSpans(&arg,4,&out),"valid Standard spans");
 check(out.input.data==input&&out.input.bytes==sizeof(input)&&out.output.data==output&&out.output.bytes==sizeof(output),"exact distinct bytes");
 io.pInputSignatureDeprecated=nullptr;
 check(!AgxD3d12AcquireR1IoSpans(&arg,4,&out)&&!out.input.data&&!out.output.data,"missing input refused before output");
 io.pInputSignatureDeprecated=input;io.pOutputSignatureDeprecated=nullptr;
 check(!AgxD3d12AcquireR1IoSpans(&arg,4,&out)&&!out.input.data&&!out.output.data,"missing output refused before publish");
 io.pOutputSignatureDeprecated=output;
 const long page=sysconf(_SC_PAGESIZE);void*guard=mmap(nullptr,page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
 check(guard!=MAP_FAILED,"guard allocated");
 io.pInputSignatureDeprecated=static_cast<const D3D12DDIARG_SIGNATURE_ENTRY*>(guard);io.NumInputSignatureEntries=5;
 check(!AgxD3d12AcquireR1IoSpans(&arg,4,&out)&&!out.input.data&&!out.output.data,"over policy before guard read");
 io.NumInputSignatureEntries=4;
 check(AgxD3d12AcquireR1IoSpans(&arg,4,&out)&&out.input.data==guard&&out.input.bytes==4*sizeof(D3D12DDIARG_SIGNATURE_ENTRY),"pointer only no guard read");
 check(!AgxD3d12AcquireR1IoSpans(&arg,0,&out)&&!out.input.data,"zero policy rejects nonempty");
 munmap(guard,page);
 std::printf("R1_IO_SPAN_PASS checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
