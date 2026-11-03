// D3D12 MEMBW probe (diagnostic): mode 0 streams the whole source buffer with
// 16-byte loads (grid-stride over `threads` invocations); mode 1 follows
// `steps` dependent loads through a random cycle of 64-byte lines (word 0 of
// each line holds the next line). The stores are almost never taken; they
// only keep the loads alive.
// fxc /T cs_5_0 /E main /O3 /Vn g_agx_d3d12_probe_cs_membw /Fh agx_d3d12_probe_cs_membw.h
ByteAddressBuffer src : register(t0);
RWByteAddressBuffer outv : register(u0);
cbuffer Params : register(b0) { uint count16; uint mode; uint steps; uint threads; };

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint4 acc = uint4(0, 0, 0, 0);
    if (mode == 0) {
        for (uint i = id.x; i < count16; i += threads)
            acc ^= src.Load4(i * 16);
    } else {
        uint node = (id.x * 4099u) % (count16 / 4u);
        for (uint s = 0; s < steps; ++s)
            node = src.Load(node * 64u);
        acc.x = node;
    }
    if (acc.x == 0xdeadbeefu && acc.y == 0x01234567u)
        outv.Store(0, acc.z ^ acc.w);
    if (mode == 1 && acc.x == 0xffffffffu)
        outv.Store(4, acc.x);
}
