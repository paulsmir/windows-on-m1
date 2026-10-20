#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include "agx_d3d12_library_wire_cng.h"
static int check(bool okay,const char *name){if(!okay){std::printf("CNG_FAIL %s\n",name);return 1;}return 0;}
int main(){
 AgxD3d12CngSha256 c;int fail=0;fail+=check(c.Init(),"init");
 static constexpr std::uint8_t want_abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
 const std::uint8_t a[]={'a'},bc[]={'b','c'},abc[]={'a','b','c'};
 std::uint8_t out[32]{};
 AgxD3d12WireSpan one[]={{abc,3}},two[]={{a,1},{bc,2}};
 fail+=check(AgxD3d12CngSha256::Digest(one,1,out,&c)&&std::memcmp(out,want_abc,32)==0,"abc");
 fail+=check(AgxD3d12CngSha256::Digest(two,2,out,&c)&&std::memcmp(out,want_abc,32)==0,"scatter");
 fail+=check(!AgxD3d12CngSha256::Digest(two,2,out,nullptr),"null context");
 fail+=check(!AgxD3d12CngSha256::Digest(nullptr,1,out,&c),"null spans");
 std::printf("CNG_SHA256_%s\n",fail?"FAIL":"PASS");return fail?1:0;
}
