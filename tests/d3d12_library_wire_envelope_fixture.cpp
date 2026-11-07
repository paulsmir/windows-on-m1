#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include "agx_d3d12_library_wire_envelope.h"

static void put32(std::vector<std::uint8_t> &b, std::size_t at, std::uint32_t v) {
    for (unsigned i=0;i<4;++i) b[at+i]=static_cast<std::uint8_t>(v>>(i*8));
}
static void put64(std::vector<std::uint8_t> &b, std::size_t at, std::uint64_t v) {
    for (unsigned i=0;i<8;++i) b[at+i]=static_cast<std::uint8_t>(v>>(i*8));
}
static std::vector<std::uint8_t> fresh() {
    std::vector<std::uint8_t> b(AgxD3d12LibraryWireBytes,0);
    const char magic[8]={'A','G','X','P','L','B','1',0};
    std::memcpy(b.data(),magic,8);
    put32(b,8,1);put32(b,12,256);put64(b,16,b.size());put64(b,24,256);
    put64(b,40,256);put64(b,48,256);put64(b,56,256);
    return b;
}
static unsigned checks=0;
static void check(bool value,const char *name) {
    ++checks;if(!value){std::fprintf(stderr,"WIRE_ENVELOPE_FAIL %s\n",name);std::exit(1);}
}
static void reject(std::vector<std::uint8_t> &b,const char *name) {
    AgxD3d12LibraryEnvelope out{};out.used=777;
    check(!AgxD3D12CheckLibraryEnvelope(b.data(),b.size(),&out),name);
    check(out.used==0,"failed parse cleared output");
}
int main(){
    auto b=fresh();AgxD3d12LibraryEnvelope e{};
    check(AgxD3D12CheckLibraryEnvelope(b.data(),b.size(),&e),"empty envelope");
    check(e.used==256&&e.pipeline_count==0&&e.component_count==0,"empty output");
    check(!AgxD3D12CheckLibraryEnvelope(nullptr,b.size(),&e)&&e.used==0,"null refused");
    check(!AgxD3D12CheckLibraryEnvelope(b.data(),255,&e)&&e.used==0,"short refused without read");
    check(!AgxD3D12CheckLibraryEnvelope(b.data(),b.size()-1,&e)&&e.used==0,"nonfixed buffer refused");
    check(!AgxD3D12CheckLibraryEnvelope(b.data(),b.size(),nullptr),"null output refused");
    b[0]='X';reject(b,"magic");b=fresh();
    put32(b,8,2);reject(b,"version");b=fresh();
    put32(b,12,255);reject(b,"header size");b=fresh();
    put64(b,16,0);reject(b,"total bytes");b=fresh();
    put64(b,24,255);reject(b,"used below header");b=fresh();
    put64(b,24,b.size()+1ull);reject(b,"used beyond arena");b=fresh();
    put32(b,32,1025);reject(b,"pipeline count limit");b=fresh();
    put32(b,36,8193);reject(b,"component count limit");b=fresh();
    put64(b,40,257);reject(b,"misaligned pipeline table");b=fresh();
    put32(b,32,1);put64(b,24,320);reject(b,"pipeline table beyond used");b=fresh();
    put64(b,40,0xfffffffffffffff8ull);reject(b,"large offset");b=fresh();
    put32(b,32,1);put64(b,48,256);put64(b,56,320);put64(b,24,320);reject(b,"tables overlap");b=fresh();
    put32(b,36,1);put64(b,56,256);put64(b,24,320);reject(b,"payload overlaps component table");b=fresh();
    b[176]=1;reject(b,"reserved header");b=fresh();
    put32(b,32,1);put32(b,36,1);put64(b,48,320);put64(b,56,384);put64(b,24,400);
    check(AgxD3D12CheckLibraryEnvelope(b.data(),b.size(),&e),"one each envelope");
    check(e.pipeline_count==1&&e.component_count==1&&e.pipeline_table==256&&e.component_table==320&&e.payload_data==384&&e.used==400,"one each output");
    std::printf("WIRE_ENVELOPE_PASS checks=%u\n",checks);
}
