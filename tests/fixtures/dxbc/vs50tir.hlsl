// FL 11_1 forced sample count: the vertex shader feeding ps50tir through
// the expansion GS: fxc /T vs_5_0 /O3 /E main.
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V main(float4 p : POSITION, float2 uv : TEXCOORD0)
{
    V o;
    o.pos = p;
    o.uv = uv;
    return o;
}
