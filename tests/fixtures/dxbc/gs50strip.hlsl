// gs_5_0 triangle-strip output feeding the FL 11_1 / CR pixel shader variant
// (ps50tir reads TEXCOORD0): a four-vertex strip (two triangles) per input
// triangle; C61C wraps this GS to emit the expanded triangles itself.
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
[maxvertexcount(4)]
void main(triangle V t[3], inout TriangleStream<V> s)
{
    V o = t[0]; s.Append(o);
    o = t[1]; s.Append(o);
    o = t[2]; s.Append(o);
    o = t[2]; o.pos.x += 0.25; s.Append(o);
    s.RestartStrip();
}
