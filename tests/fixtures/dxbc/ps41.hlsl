// ps_4_1: gather4, lod, sample positions, sample info, multisampled loads,
// per-sample interpolation.
Texture2D t : register(t0); Texture2DMS<float4, 4> ms : register(t1);
SamplerState s : register(s0);
float4 main(float4 pos : SV_Position, sample float2 uv : TEXCOORD0, uint si : SV_SampleIndex) : SV_Target {
  float4 g = t.Gather(s, uv);
  float lod = t.CalculateLevelOfDetail(s, uv) + t.CalculateLevelOfDetailUnclamped(s, uv);
  float2 sp = ms.GetSamplePosition(si);
  uint w, h, n; ms.GetDimensions(w, h, n);
  float4 m = ms.Load(int2(pos.xy), si);
  return g + m + float4(sp, lod, n);
}
