// Original proposed R32_UINT fixture. UAV GetDimensions is not covered.
RWTexture1DArray<uint> selectedArray : register(u0);
cbuffer Shape : register(b0) {uint width,layers,firstGlobalSlice,mip;}
[numthreads(64,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
 if(tid.x<width && tid.y<layers) {
  uint previous=selectedArray[tid.xy];
  selectedArray[tid.xy]=previous==0xa5a5a5a5u ?
   0x74000000u+((firstGlobalSlice+tid.y)<<20)+(mip<<16)+tid.x : 0xdeadc024u;
 }
}
