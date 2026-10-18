// Tier1 boundary probe: root t127, table t126, root u7 and CBV b13.
ByteAddressBuffer a : register(t127);
StructuredBuffer<uint> b : register(t126);
RWByteAddressBuffer output : register(u7);
cbuffer Offset : register(b13) { uint bias; };
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, a.Load(id.x * 4) + b[id.x] * 2 + bias);
}
