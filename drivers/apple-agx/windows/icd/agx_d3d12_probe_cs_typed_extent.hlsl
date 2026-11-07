// Three typed elements must survive owner binding without dword truncation.
Buffer<uint> source_values : register(t0);
RWByteAddressBuffer output_words : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= 3) return;
    uint count;
    source_values.GetDimensions(count);
    output_words.Store(thread_id.x * 4, source_values.Load(thread_id.x));
    if (thread_id.x == 0)
        output_words.Store(12, count);
}
