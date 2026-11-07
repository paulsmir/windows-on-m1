// resinfo on typed UAV textures (MiniEngine DebugDrawHistogramCS)
RWTexture2D<float4> t2 : register(u0);
RWTexture2DArray<uint> a2 : register(u1);
RWByteAddressBuffer o : register(u2);
[numthreads(1, 1, 1)]
void main()
{
    uint w, h, n, aw, ah, al;
    float fw, fh;
    t2.GetDimensions(w, h);
    t2.GetDimensions(fw, fh);
    a2.GetDimensions(aw, ah, al);
    o.Store4(0, uint4(w, h, aw, ah));
    o.Store4(16, uint4(al, asuint(fw), asuint(fh), 0));
}
