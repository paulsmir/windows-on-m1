// D3D11.1 (FL 11_1) 64 UAV registers: fxc /T cs_5_0 /O3 /E main.
// Buffer views and the append counter compact into shader buffers; typed
// views keep their register as the image slot.
RWByteAddressBuffer raw0 : register(u0);
RWTexture2D<float4> tex9 : register(u9);
AppendStructuredBuffer<uint4> app40 : register(u40);
RWTexture2D<uint> tex62 : register(u62);
RWByteAddressBuffer raw63 : register(u63);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    tex9.GetDimensions(w, h);
    tex9[id.xy] = float4(id.xy, w, h);
    InterlockedAdd(tex62[id.xy], 1);
    app40.Append(uint4(id, 7));
    raw0.Store(id.x * 4, id.y);
    raw63.InterlockedAdd(0, raw0.Load(4));
}
