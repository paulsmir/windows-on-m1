// cs_4_0 (compute on D3D10.x hardware, D3D11 "ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x"):
// one raw or structured UAV, raw and structured SRVs, a constant buffer,
// groupshared memory written only at SV_GroupIndex, a group barrier.
struct Item { uint id; float value; };
StructuredBuffer<Item> items : register(t0);
ByteAddressBuffer words : register(t1);
RWStructuredBuffer<uint4> result : register(u0);
cbuffer params : register(b0) { uint scale; uint bias; };
groupshared uint partial[64];
[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
  Item it = items[dtid.x];
  partial[gi] = it.id * scale + words.Load(dtid.x * 4);
  GroupMemoryBarrierWithGroupSync();
  uint sum = 0;
  for (uint i = 0; i < 64; ++i) sum += partial[i];
  result[dtid.x] = uint4(sum, gid.x, asuint(it.value * 2.0f), partial[(gi + 1) & 63] + bias);
}
