// DXBC continue / continuec inside a loop (EXP1839 MiniEngine assertion)
RWStructuredBuffer<uint> o : register(u0);
cbuffer c : register(b0) { uint n; uint skip; };
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint s = 0;
    [loop] for (uint i = 0; i < n; ++i) {
        if (i == skip) continue;
        if ((i & 3) == id.x) { s += 2; continue; }
        s += i;
    }
    o[id.y * 16 + id.x] = s;
}
