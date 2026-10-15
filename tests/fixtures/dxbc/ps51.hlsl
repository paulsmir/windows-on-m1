// ps_5_1: resource arrays in register spaces, dynamic indexing.
Texture2D textures[8] : register(t0, space1);
SamplerState samplers[2] : register(s0, space2);
cbuffer C : register(b0, space3) { uint which; };
float4 main(float2 uv : TEXCOORD0, nointerpolation uint i : INDEX) : SV_Target {
  return textures[NonUniformResourceIndex(i)].Sample(samplers[which & 1], uv) + textures[which].Load(int3(0, 0, 0));
}
