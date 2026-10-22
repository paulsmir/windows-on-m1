// EXP1879G (Night Raid GT2 pixel shader pattern): a dynamically indexed
// TextureCube range followed by a dynamically indexed Texture2D range and a
// static texture. The cube run must stop at the 2D range's base slot.
TextureCube cubes[12] : register(t1, space2);
Texture2D flats[3] : register(t1, space3);
Texture2D plain : register(t0, space0);
SamplerState s : register(s0, space0);
cbuffer Pick : register(b0, space0) { uint i; uint j; };
float4 main(float4 pos : SV_Position, float3 d : TEXCOORD0) : SV_Target
{
    return cubes[i].SampleLevel(s, d, 0) + flats[j].SampleLevel(s, d.xy, 0) + plain.Sample(s, d.xy);
}
