// Original C15 compiler-only fixture; one static raw UAV, no optional SM6 feature.
RWByteAddressBuffer Out : register(u0, space0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x < 64)
        Out.Store(id.x * 4, id.x * 3 + 7);
}
