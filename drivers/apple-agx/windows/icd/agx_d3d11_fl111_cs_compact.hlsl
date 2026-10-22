// D3D11 FL 11_1 probe, D11UAVCOMPACT: buffer views and an append counter at
// registers that the D3D11.1 translator compacts (u1 -> buffer 0, u3 -> 1,
// u7 -> 2, u3's counter -> 3) next to a typed view kept at image 5.
RWByteAddressBuffer raw1 : register(u1);
AppendStructuredBuffer<uint> app3 : register(u3);
RWTexture2D<uint> tex5 : register(u5);
RWByteAddressBuffer raw7 : register(u7);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.y * 8 + id.x;
    raw1.Store(i * 4, i + 100);
    app3.Append(i);
    tex5[id.xy] = i * 3;
    raw7.InterlockedAdd(0, 1);
}
