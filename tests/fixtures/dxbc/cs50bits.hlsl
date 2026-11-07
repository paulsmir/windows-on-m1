// EXP1821 regression: fxc emits ubfe/ibfe/bfi for these shifts and masks;
// Asahi consumes none of nir_op_ubfe/ibfe/bfm/bfi.
// fxc /T cs_5_0 /E main /Fo cs50bits.dxbc /Fc cs50bits.asm cs50bits.hlsl
RWStructuredBuffer<uint4> data : register(u0);
cbuffer Shift : register(b0) { uint s; uint w; };

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint flat : SV_GroupIndex)
{
    uint4 v = data[id.x];
    uint a = (v.x >> 3) & 0x1f;                  // ubfe 5, 3
    int b = ((int)v.y << 5) >> 27;               // ibfe 5, 22
    uint c = (v.z & ~0xff00u) | ((v.w << 8) & 0xff00u); // bfi 8, 8
    uint d = (v.x >> (s & 31)) & ((1u << (w & 31)) - 1u); // dynamic extract
    uint e = (flat & 127u) | (id.y << 7);        // bfi 25, 7 (nBody)
    data[id.x] = uint4(a, (uint)b, c, d ^ e);
}
