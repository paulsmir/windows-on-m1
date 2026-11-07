// C44 parity: a geometry shader (array inputs, position and generic outputs, cut)
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct G { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; nointerpolation uint id : TEXCOORD1; };
cbuffer C : register(b0) { float4 offset; };
[maxvertexcount(6)]
void main(triangle V v[3], uint prim : SV_PrimitiveID, inout TriangleStream<G> s)
{
    for (uint k = 0; k < 2; ++k) {
        for (uint i = 0; i < 3; ++i) {
            G g;
            g.pos = v[i].pos + offset * k;
            g.uv = v[i].uv;
            g.col = v[i].col;
            g.id = prim * 2 + k;
            s.Append(g);
        }
        s.RestartStrip();
    }
}
