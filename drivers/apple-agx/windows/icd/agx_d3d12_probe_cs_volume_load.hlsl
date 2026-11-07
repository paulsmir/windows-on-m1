// C21B: a typed volume SRV selects one resource mip; Load's mip is view-local.
Texture3D<uint> source_volume : register(t0);
RWByteAddressBuffer output_words : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height, depth, levels;
    source_volume.GetDimensions(0, width, height, depth, levels);
    if (all(id == uint3(0, 0, 0)))
        output_words.Store4(0, uint4(width, height, depth, levels));
    if (id.x >= width || id.y >= height || id.z >= depth) return;
    uint index = (id.z * height + id.y) * width + id.x;
    output_words.Store(16 + index * 4, source_volume.Load(int4(id, 0)));
}
