// D3D12 C41 probes (EXP1839 MiniEngine): fxc /T cs_5_0 /O3
// C41A /E cs_continue /Vn g_agx_d3d12_probe_cs_c41a -> agx_d3d12_probe_cs_c41a.h:
//   continue and continuec inside a loop (FillLightGridCS)
// C41B /E cs_uavinfo /Vn g_agx_d3d12_probe_cs_c41b -> agx_d3d12_probe_cs_c41b.h:
//   resinfo on typed UAV textures (DebugDrawHistogramCS)
RWByteAddressBuffer o : register(u0);
RWTexture2D<float4> t2 : register(u1);
RWTexture2DArray<uint> a2 : register(u2);

[numthreads(64, 1, 1)]
void cs_continue(uint3 id : SV_DispatchThreadID)
{
    uint s = 0;
    [loop] for (uint i = 0; i < 40; ++i) {
        if (i % 3 == id.x % 3) continue;
        if ((i & 7) == 5) { s += 1000; continue; }
        s += i;
    }
    o.Store(id.x * 4, s);
}

[numthreads(1, 1, 1)]
void cs_uavinfo()
{
    uint w, h, aw, ah, al;
    float fw, fh;
    t2.GetDimensions(w, h);
    t2.GetDimensions(fw, fh);
    a2.GetDimensions(aw, ah, al);
    o.Store4(0, uint4(w, h, aw, ah));
    o.Store4(16, uint4(al, asuint(fw), asuint(fh), 0));
}
