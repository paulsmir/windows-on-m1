// cs_5_1 (D3D12 C47): twelve UAVs, more than the eight UAV slots: typed
// textures, a typed buffer and raw buffers past the slots read the heap.
RWTexture2D<float4> t0 : register(u0);
RWTexture2D<float4> t1 : register(u1);
RWTexture2D<float4> t2 : register(u2);
RWTexture2D<float4> t3 : register(u3);
RWTexture2D<float4> t4 : register(u4);
RWTexture2D<float4> t5 : register(u5);
RWTexture2D<float4> t6 : register(u6);
RWTexture2D<float4> t7 : register(u7);
RWTexture2D<float4> t8 : register(u8);
RWBuffer<uint> b9 : register(u9);
RWByteAddressBuffer r10 : register(u10);
RWByteAddressBuffer r11 : register(u11);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
  float4 v = t0[id.xy] + t1[id.xy] + t2[id.xy] + t3[id.xy] + t4[id.xy] + t5[id.xy] + t6[id.xy] + t7[id.xy];
  t8[id.xy] = v + t8[id.xy];
  b9[id.x] = b9[id.x + 1] + 1;
  uint w, h; t8.GetDimensions(w, h);
  uint old; r10.InterlockedAdd(0, 1, old);
  r11.Store2(8, uint2(r10.Load(4) + old, w));
  uint size; r11.GetDimensions(size);
  r11.Store(0, size);
}
