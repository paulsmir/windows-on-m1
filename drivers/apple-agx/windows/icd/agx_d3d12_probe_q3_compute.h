#ifndef AGX_D3D12_PROBE_Q3_COMPUTE_H
#define AGX_D3D12_PROBE_Q3_COMPUTE_H
/* One bounded dispatch; each of 64 lanes retains its own recurrence dependency.
 * Runtime iterations and [loop] prevent constant unrolling. Unsigned arithmetic
 * is modulo 2^32, with an independent O(log n) CPU affine-power oracle. */
static const char q3_compute_hlsl[] =
    "RWByteAddressBuffer output:register(u0);\n"
    "cbuffer Params:register(b0) { uint iterations; };\n"
    "[numthreads(64,1,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
    " uint x=3u*id.x+7u;\n"
    " [loop] for(uint k=0;k<iterations;++k) x=x*1664525u+1013904223u;\n"
    " output.Store(4u*id.x,x); }\n";
static unsigned q3_compute_expected(unsigned lane, unsigned iterations) {
    unsigned mul=1664525u, add=1013904223u, x=3u*lane+7u;
    while (iterations) {
        if (iterations & 1u) x=mul*x+add;
        add*=mul+1u;
        mul*=mul;
        iterations>>=1;
    }
    return x;
}
static unsigned q3_bad_compute(const unsigned *words, unsigned iterations) {
    unsigned bad=0;
    for (unsigned i=0;i<64;++i) bad+=words[i]!=q3_compute_expected(i,iterations);
    return bad;
}
#endif
