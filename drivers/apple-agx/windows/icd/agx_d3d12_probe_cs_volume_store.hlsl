// C21C: full selected-mip UAV; dimensions come from known root constants.
// Typed UAV GetDimensions/resinfo is deliberately outside this checkpoint.
RWTexture3D<uint> output_volume : register(u0);
cbuffer VolumeParams : register(b0) { uint width; uint height; uint depth; uint mip_tag; };

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height || id.z >= depth) return;
    output_volume[id] = 0x71000000u + mip_tag * 0x00100000u + id.z * 0x1000u + id.y * 0x40u + id.x;
}
