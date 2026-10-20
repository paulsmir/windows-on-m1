#include <CommonCrypto/CommonDigest.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "agx_d3d12_library_wire_digest.h"
static void u32(std::vector<std::uint8_t>&b,std::size_t n,std::uint32_t v){for(unsigned i=0;i<4;++i)b[n+i]=std::uint8_t(v>>(i*8));}
static void u64(std::vector<std::uint8_t>&b,std::size_t n,std::uint64_t v){for(unsigned i=0;i<8;++i)b[n+i]=std::uint8_t(v>>(i*8));}
static bool digest(const AgxD3d12WireSpan*sp,std::size_t count,std::uint8_t out[32],void*){
 CC_SHA256_CTX ctx;CC_SHA256_Init(&ctx);
 for(std::size_t i=0;i<count;++i)CC_SHA256_Update(&ctx,sp[i].data,static_cast<CC_LONG>(sp[i].bytes));
 CC_SHA256_Final(out,&ctx);return true;
}
static bool fail_digest(const AgxD3d12WireSpan*,std::size_t,std::uint8_t[32],void*){return false;}
static std::array<std::uint8_t,32> compiler_key,adapter_key;
static constexpr std::uint64_t ddi=0x000c000a00010000ull;
static std::vector<std::uint8_t> good(){
 std::vector<std::uint8_t>b(AgxD3d12LibraryWireBytes,0);std::memcpy(b.data(),"AGXPLB1",7);
 u32(b,8,1);u32(b,12,256);u64(b,16,b.size());u64(b,24,464);
 u32(b,32,1);u32(b,36,2);u64(b,40,256);u64(b,48,320);u64(b,56,448);
 compiler_key.fill(0x11);adapter_key.fill(0x22);
 std::memcpy(b.data()+64,compiler_key.data(),32);std::memcpy(b.data()+96,adapter_key.data(),32);
 u64(b,160,ddi);u32(b,168,60);u32(b,172,1);
 u32(b,256,7);u32(b,260,1);
 u32(b,320,1);u32(b,324,1);u64(b,328,448);u64(b,336,8);
 u32(b,384,0x100);u32(b,388,1);u64(b,392,456);u64(b,400,8);
 for(int i=0;i<8;++i){b[448+i]=std::uint8_t(i+1);b[456+i]=std::uint8_t(i+11);}
 std::uint8_t hash[32];AgxD3d12WireSpan leaf[]{ {b.data()+448,8} },recipe[]{ {b.data()+456,8} };
 digest(leaf,1,hash,nullptr);std::memcpy(b.data()+344,hash,32);
 digest(recipe,1,hash,nullptr);std::memcpy(b.data()+408,hash,32);std::memcpy(b.data()+264,hash,32);
 const std::uint8_t zeros[32]{};AgxD3d12WireSpan all[]{ {b.data(),128},{zeros,32},{b.data()+160,b.size()-160} };
 digest(all,3,hash,nullptr);std::memcpy(b.data()+128,hash,32);return b;
}
static unsigned checks=0;
static void check(bool ok,const char*name){++checks;if(!ok){std::fprintf(stderr,"WIRE_DIGEST_FAIL %s\n",name);std::exit(1);}}
static bool run(const std::vector<std::uint8_t>&b,AgxD3d12WireDigestCallback cb=digest){
 AgxD3d12LibraryInterval scratch[2]{};AgxD3d12LibraryEnvelope e{};
 return AgxD3D12CheckLibraryDigests(b.data(),b.size(),scratch,2,cb,nullptr,compiler_key.data(),adapter_key.data(),ddi,60,1,&e);
}
int main(){
 auto b=good();check(run(b),"valid digest and identity");
 b[448]^=1;check(!run(b),"component corruption");b=good();
 b[344]^=1;check(!run(b),"component digest corruption");b=good();
 b[264]^=1;check(!run(b),"pipeline recipe digest corruption");b=good();
 b[128]^=1;check(!run(b),"whole digest corruption");b=good();
 check(!run(b,fail_digest),"backend failure");
 AgxD3d12LibraryInterval scratch[2]{};AgxD3d12LibraryEnvelope out{};out.used=777;
 check(!AgxD3D12CheckLibraryDigests(b.data(),b.size(),scratch,2,digest,nullptr,adapter_key.data(),adapter_key.data(),ddi,60,1,&out)&&out.used==0,"compiler key mismatch clears output");
 check(!AgxD3D12CheckLibraryDigests(b.data(),b.size(),scratch,2,digest,nullptr,compiler_key.data(),compiler_key.data(),ddi,60,1,&out),"adapter key mismatch");
 check(!AgxD3D12CheckLibraryDigests(b.data(),b.size(),scratch,2,digest,nullptr,compiler_key.data(),adapter_key.data(),ddi+1,60,1,&out),"DDI mismatch");
 check(!AgxD3D12CheckLibraryDigests(b.data(),b.size(),scratch,2,digest,nullptr,compiler_key.data(),adapter_key.data(),ddi,61,1,&out),"model mismatch");
 check(!AgxD3D12CheckLibraryDigests(b.data(),b.size(),scratch,2,digest,nullptr,compiler_key.data(),adapter_key.data(),ddi,60,2,&out),"semantic profile mismatch");
 check(!AgxD3D12CheckLibraryDigests(b.data(),b.size(),scratch,2,nullptr,nullptr,compiler_key.data(),adapter_key.data(),ddi,60,1,&out),"null backend");
 check(!AgxD3D12CheckLibraryDigests(b.data(),b.size(),scratch,2,digest,nullptr,nullptr,adapter_key.data(),ddi,60,1,&out),"null key");
 std::printf("WIRE_DIGEST_PASS checks=%u\n",checks);
}
