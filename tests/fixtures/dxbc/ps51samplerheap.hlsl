// ps_5_1 (D3D12 C47): an unbounded sampler range read from the heap with
// Sample, SampleBias, SampleGrad and SampleLevel.
Texture2D t : register(t0);
SamplerState samplers[] : register(s0, space1);
float4 main(float2 uv : TEXCOORD0, nointerpolation uint i : INDEX) : SV_Target {
  return t.Sample(samplers[i], uv) + t.SampleBias(samplers[i + 1], uv, 0.5) +
         t.SampleGrad(samplers[i + 2], uv, ddx(uv), ddy(uv)) + t.SampleLevel(samplers[1500], uv, 2.0);
}
