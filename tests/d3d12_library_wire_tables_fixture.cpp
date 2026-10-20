#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "agx_d3d12_library_wire_tables.h"
static void u32(std::vector<std::uint8_t>&b,std::size_t n,std::uint32_t v){for(unsigned i=0;i<4;++i)b[n+i]=std::uint8_t(v>>(i*8));}
static void u64(std::vector<std::uint8_t>&b,std::size_t n,std::uint64_t v){for(unsigned i=0;i<8;++i)b[n+i]=std::uint8_t(v>>(i*8));}
static std::vector<std::uint8_t> good(){
 std::vector<std::uint8_t>b(AgxD3d12LibraryWireBytes,0);
 std::memcpy(b.data(),"AGXPLB1",7);u32(b,8,1);u32(b,12,256);u64(b,16,b.size());
 u64(b,24,464);u32(b,32,1);u32(b,36,2);u64(b,40,256);u64(b,48,320);u64(b,56,448);
 u32(b,256,7);u32(b,260,1); // one pipeline points at recipe component
 u32(b,320,1);u32(b,324,1);u64(b,328,448);u64(b,336,8); // leaf
 u32(b,384,0x100);u32(b,388,1);u64(b,392,456);u64(b,400,8); // recipe
 b[408]=1;for(int i=0;i<8;++i){b[448+i]=std::uint8_t(i+1);b[456+i]=std::uint8_t(i+11);}
 return b;
}
static unsigned checks=0;
static void check(bool ok,const char *what){++checks;if(!ok){std::fprintf(stderr,"WIRE_TABLE_FAIL %s\n",what);std::exit(1);}}
static bool run(const std::vector<std::uint8_t>&b,std::size_t capacity=2){AgxD3d12LibraryInterval scratch[2]{};AgxD3d12LibraryEnvelope e{};return AgxD3D12CheckLibraryTables(b.data(),b.size(),scratch,capacity,&e);}
int main(){
 auto b=good();check(run(b),"valid two components");
 auto empty=good();u64(empty,24,256);u32(empty,32,0);u32(empty,36,0);u64(empty,48,256);u64(empty,56,256);
 std::memset(empty.data()+256,0,208);
 AgxD3d12LibraryEnvelope empty_out{};
 check(AgxD3D12CheckLibraryTables(empty.data(),empty.size(),nullptr,0,&empty_out),"empty null scratch");
 check(empty_out.used==256&&empty_out.component_count==0,"empty output");
 check(!run(b,1),"scratch capacity");
 b[260]=2;check(!run(b),"pipeline nonrecipe ref");
 AgxD3d12LibraryInterval scratch[2]{};AgxD3d12LibraryEnvelope cleared{};cleared.used=999;
 check(!AgxD3D12CheckLibraryTables(b.data(),b.size(),scratch,2,&cleared)&&cleared.used==0,"failure clears output");
 b=good();
 u32(b,260,2);check(!run(b),"pipeline out of range");b=good();
 b[296]=1;check(!run(b),"pipeline reserved");b=good();
 u32(b,320,0);check(!run(b),"component kind");b=good();
 u32(b,324,0);check(!run(b),"component schema");b=good();
 u64(b,392,448);check(!run(b),"overlapping payload");b=good();
 u64(b,392,464);check(!run(b),"payload out of used");b=good();
 b[440]=1;check(!run(b),"component reserved");b=good();
 b[464]=1;check(!run(b),"trailing nonzero");b=good();
 std::memmove(b.data()+328,b.data()+320,144);std::memset(b.data()+320,0,8);
 u64(b,24,472);u64(b,48,328);u64(b,56,456);u64(b,336,456);u64(b,400,464);
 check(run(b),"valid table gap");b[320]=1;check(!run(b),"table gap nonzero");b=good();
 u32(b,320,0x100);u32(b,384,1);check(!run(b),"component canonical order");b=good();
 std::printf("WIRE_TABLE_PASS checks=%u\n",checks);
}
