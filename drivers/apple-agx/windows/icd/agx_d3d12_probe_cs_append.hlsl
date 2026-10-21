// C33A: every even thread of 128 appends its id; the hidden counter starts
// at 5 (copied before the dispatch), so it ends at 69 and slots 5..68 hold
// each even id once.
AppendStructuredBuffer<uint> queue_out : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if ((id.x & 1u) == 0u)
        queue_out.Append(id.x);
}
