#ifndef AGX_D3D12_PROBE_CS_CLKTWIN_H
#define AGX_D3D12_PROBE_CS_CLKTWIN_H
/* CLKTWIN (architect 2026-11-11T15:35Z, clock-twin REPORT.md Part B): an
 * arithmetic-only compute kernel whose only memory traffic is one final store.
 * Eight independent FFMA chains, 4x unrolled inside a [loop] with a runtime
 * trip count R, so 32 FFMA per iteration and no constant folding (R, a, b are
 * root constants). NT (threads per group) is a compile-time define: 32 for
 * N32, 1024 for the L(G) ladder and the W(G) full-width shapes. The same
 * source text is used by tools/macos/clktwin_metal.m (MSL spelling). */
static const char clktwin_hlsl[] =
    "RWByteAddressBuffer output:register(u0);\n"
    "cbuffer Params:register(b0) { uint R; float a; float b; };\n"
    "[numthreads(NT,1,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
    " float t=(float)id.x;\n"
    " float x0=1.0f+t*1e-7f, x1=1.0f+t*2e-7f, x2=1.0f+t*3e-7f, x3=1.0f+t*4e-7f;\n"
    " float x4=1.0f+t*5e-7f, x5=1.0f+t*6e-7f, x6=1.0f+t*7e-7f, x7=1.0f+t*8e-7f;\n"
    " [loop] for(uint i=0;i<R;++i) {\n"
    "  [unroll] for(uint u=0;u<4;++u) {\n"
    "   x0=mad(x0,a,b); x1=mad(x1,a,b); x2=mad(x2,a,b); x3=mad(x3,a,b);\n"
    "   x4=mad(x4,a,b); x5=mad(x5,a,b); x6=mad(x6,a,b); x7=mad(x7,a,b); } }\n"
    " output.Store(4u*id.x,asuint(x0+x1+x2+x3+x4+x5+x6+x7)); }\n";

/* CPU oracle for one thread: fused = fmaf per step (single rounding), unfused
 * = separate float multiply and add roundings. A bit-exact match names the
 * form the GPU executed; neither = reassociated sum or a different kernel. */
static float clktwin_expected(unsigned tid, unsigned r, float a, float b, int fused) {
    float t=(float)tid, x[8];
    static const float k[8]={1e-7f,2e-7f,3e-7f,4e-7f,5e-7f,6e-7f,7e-7f,8e-7f};
    /* fxc folds the init 1 + t*k into one mad: same form as the loop */
    for (int j=0;j<8;++j) { if (fused) x[j]=fmaf(t,k[j],1.0f); else { volatile float m=t*k[j]; x[j]=1.0f+m; } }
    for (unsigned i=0;i<r*4u;++i)
        for (int j=0;j<8;++j) {
            if (fused) x[j]=fmaf(x[j],a,b);
            else { volatile float m=x[j]*a; x[j]=m+b; }
        }
    {
        volatile float s=x[0];
        for (int j=1;j<8;++j) s=s+x[j];
        return s;
    }
}
#endif
