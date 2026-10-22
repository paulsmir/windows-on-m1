// C51: SM 6.2 fp32 denorm modes (dxc -denorm preserve / ftz); AGX flushes
// fp32 denormals, so both translate and run as FTZ.
RWBuffer<float> o : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    o[id.x] = o[id.x] * 0.5f;
}
