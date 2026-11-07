// C61C (dxc -T gs_6_0): the application geometry shader of a conservative
// pipeline -- each triangle passes through as a three-vertex strip, so
// C61A's reference applies; the driver wraps this GS to expand.
struct V { float4 pos : SV_Position; };
[maxvertexcount(3)]
void main(triangle V t[3], inout TriangleStream<V> s)
{
    s.Append(t[0]);
    s.Append(t[1]);
    s.Append(t[2]);
    s.RestartStrip();
}
