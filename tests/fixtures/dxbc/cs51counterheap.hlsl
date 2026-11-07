// cs_5_1 (D3D12 C47): an append buffer past the eight UAV slots reads its
// counter from the descriptor heap.
RWByteAddressBuffer r0 : register(u0); RWByteAddressBuffer r1 : register(u1);
RWByteAddressBuffer r2 : register(u2); RWByteAddressBuffer r3 : register(u3);
RWByteAddressBuffer r4 : register(u4); RWByteAddressBuffer r5 : register(u5);
RWByteAddressBuffer r6 : register(u6); RWByteAddressBuffer r7 : register(u7);
AppendStructuredBuffer<uint> append : register(u8);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
  r0.Store(0, r1.Load(0) + r2.Load(0) + r3.Load(0) + r4.Load(0) + r5.Load(0) + r6.Load(0) + r7.Load(0));
  append.Append(id.x);
}
