// vs_4_0: constant buffers, matrices, loops, indexable temporaries,
// immediate constant buffer, integer ops, SV semantics.
cbuffer Frame : register(b0) { float4x4 world_view_proj; float4 tint; int count; uint mask; };
cbuffer Bones : register(b1) { float4 bones[64]; };
static const float weights[5] = { 0.1f, 0.2f, 0.4f, 0.2f, 0.1f };
struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; uint4 idx : BLENDINDICES; uint vid : SV_VertexID; uint iid : SV_InstanceID; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; nointerpolation uint id : TEXCOORD1; float4 col : COLOR0; float clip0 : SV_ClipDistance0; };
VSOut main(VSIn i) {
  VSOut o;
  float4 p = float4(i.pos, 1.0f);
  float acc[4] = { 0, 0, 0, 0 };
  [loop] for (int k = 0; k < count; ++k) {
    acc[k & 3] += weights[k % 5] * bones[i.idx[k & 3]].x;
    if (acc[0] > 10.0f) break;
  }
  p.xyz += float3(acc[0], acc[1], acc[2] + acc[3]);
  o.pos = mul(p, world_view_proj);
  o.uv = i.uv * tint.xy + frac(i.uv);
  o.id = (i.vid ^ mask) + (i.iid << 3) | (uint)abs(-count);
  o.col = tint * saturate(dot(i.pos, i.pos)) + sqrt(abs(tint)) * rsqrt(1.0f + p.wwww);
  o.clip0 = p.x - min(max(p.y, -1.0f), 1.0f);
  return o;
}
