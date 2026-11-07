// cs_5_1 (D3D12 C47): unbounded raw, structured and typed buffer SRV ranges
// read from the descriptor heap.
ByteAddressBuffer raw[] : register(t0, space1);
StructuredBuffer<uint2> structured[] : register(t0, space2);
Buffer<float4> typed[] : register(t0, space3);
RWByteAddressBuffer output : register(u0);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint bytes; raw[id.x].GetDimensions(bytes);
  uint count, stride; structured[id.x + 1].GetDimensions(count, stride);
  output.Store4(0, uint4(raw[id.x].Load(4), structured[id.x + 1][2].y, asuint(typed[700][3].z), bytes + count));
}
