// ps_5_1 (D3D12 C47): an unbounded SRV range indexed beyond the 128 slots.
Texture2D textures[] : register(t0, space1);
SamplerState s : register(s0);
cbuffer C : register(b0) { uint which; };
float4 main(float2 uv : TEXCOORD0, nointerpolation uint i : INDEX) : SV_Target {
  return textures[NonUniformResourceIndex(i)].Sample(s, uv) + textures[which + 600].Load(int3(0, 0, 0));
}
