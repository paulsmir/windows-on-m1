Texture2D<float4> t : register(t0);
Texture2D<float> d : register(t1);
SamplerState s : register(s0);
SamplerComparisonState c : register(s1);
float4 main(float4 p : SV_Position) : SV_Target {
  int x = (int)p.x;
  float2 centre = float2(0.5, 0.5);
  float2 uv = p.xy / 4.0;
  float4 biased = t.SampleBias(s, uv, 2.0);
  float4 implicit = t.Sample(s, uv);
  float4 compared = d.SampleCmp(c, float2(0.25, 0.25), 0.5).xxxx;
  if (x == 0) return t.SampleLevel(s, centre, 1);
  if (x == 1) return biased;
  if (x == 2) return t.SampleGrad(s, centre, float2(0.5, 0), float2(0, 0.5));
  if (x == 3) { uint w, h, l; t.GetDimensions(1, w, h, l);
                return float4(w / 4.0, h / 4.0, l / 4.0, 1); }
  if (x == 4) return compared;
  if (x == 5) return d.SampleCmpLevelZero(c, float2(0.75, 0.75), 0.5).xxxx;
  if (x == 6) return implicit;
  return float4(0, 0, 0, 1);
}
