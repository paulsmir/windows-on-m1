RWTexture1D<uint> target1d : register(u0);
cbuffer Shape : register(b0) {uint width,height,depth,mip;}
[numthreads(64,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
 if(tid.x<width) {
  uint before=target1d[tid.x];
  target1d[tid.x]=before==0xa5a5a5a5u ? 0x71000000u+mip*0x00100000u+tid.x : 0xdeadc023u;
 }
}
