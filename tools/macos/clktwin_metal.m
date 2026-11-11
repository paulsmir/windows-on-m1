// Metal twin of the D3D12 CLKTWIN probe step (drivers/apple-agx/windows/icd/agx_d3d12_probe.c clk_twin,
// agx_d3d12_probe_cs_clktwin.h; architect 2026-11-11T15:35Z, clock-twin REPORT.md Part B): same kernel
// (8 independent FFMA chains, 4x unrolled in a runtime-R loop, one final store), same R = 10000, a = 0.999,
// b = 0.001, same shapes N32 (one 32-thread group), L(G) 1024-thread groups G = 1..16, W(G) G = 56 and 224;
// per case one command buffer with 1 and with 5 dispatches (buffer memory barrier between), per-dispatch =
// (T5 - T1) / 4 from GPUEndTime - GPUStartTime, best of 3. P6 hold: 2 s of W56 filler command buffers first,
// and a W56 filler committed behind every timed command buffer so the GPU never idles while the CPU waits.
// Fast math is off (the D3D12 side compiles with D3DCOMPILE_ENABLE_STRICTNESS) so the sum is not reassociated.
// Build: clang -fobjc-arc -framework Metal -framework Foundation clktwin_metal.m -o clktwin_metal
// Usage: clktwin_metal [archive.metallib-path]   (with a path: also write an MTLBinaryArchive of both pipelines
//        for the code-equality gate, disassembled with .local/metal-oracle/dis.py)
// Check the clock separately: sudo powermetrics --samplers gpu_power -i 250 (about 100 % at 1278 MHz). Keep < 30 s.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <math.h>
#include <string.h>
#define CLKTWIN_R 10000u
static NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\n"
  "struct P { uint R; float a; float b; };\n"
  "#define BODY \\\n"
  "  float t = (float)id;\\\n"
  "  float x0=1.0f+t*1e-7f, x1=1.0f+t*2e-7f, x2=1.0f+t*3e-7f, x3=1.0f+t*4e-7f;\\\n"
  "  float x4=1.0f+t*5e-7f, x5=1.0f+t*6e-7f, x6=1.0f+t*7e-7f, x7=1.0f+t*8e-7f;\\\n"
  "  for (uint i = 0; i < p.R; ++i) {\\\n"
  "    _Pragma(\"unroll\") for (uint u = 0; u < 4; ++u) {\\\n"
  "      x0=fma(x0,p.a,p.b); x1=fma(x1,p.a,p.b); x2=fma(x2,p.a,p.b); x3=fma(x3,p.a,p.b);\\\n"
  "      x4=fma(x4,p.a,p.b); x5=fma(x5,p.a,p.b); x6=fma(x6,p.a,p.b); x7=fma(x7,p.a,p.b); } }\\\n"
  "  o[id] = as_type<uint>(x0+x1+x2+x3+x4+x5+x6+x7);\n"
  "[[max_total_threads_per_threadgroup(32)]] kernel void k32(device uint *o [[buffer(0)]], constant P &p [[buffer(1)]],\n"
  "  uint id [[thread_position_in_grid]]) { BODY }\n"
  "[[max_total_threads_per_threadgroup(1024)]] kernel void k1024(device uint *o [[buffer(0)]], constant P &p [[buffer(1)]],\n"
  "  uint id [[thread_position_in_grid]]) { BODY }\n";

static float expected(unsigned tid, unsigned r, float a, float b, int fused) {
  float t = (float)tid, x[8];
  static const float k[8] = {1e-7f, 2e-7f, 3e-7f, 4e-7f, 5e-7f, 6e-7f, 7e-7f, 8e-7f};
  for (int j = 0; j < 8; ++j) { if (fused) x[j] = fmaf(t, k[j], 1.0f); else { volatile float m = t * k[j]; x[j] = 1.0f + m; } }
  for (unsigned i = 0; i < r * 4u; ++i)
    for (int j = 0; j < 8; ++j) {
      if (fused) x[j] = fmaf(x[j], a, b);
      else { volatile float m = x[j] * a; x[j] = m + b; } }
  volatile float s = x[0];
  for (int j = 1; j < 8; ++j) s = s + x[j];
  return s;
}

static id<MTLCommandBuffer> encode(id<MTLCommandQueue> q, id<MTLComputePipelineState> ps, id<MTLBuffer> out,
                                   const void *p, size_t plen, unsigned groups, unsigned nt, unsigned n) {
  id<MTLCommandBuffer> cb = [q commandBuffer];
  id<MTLComputeCommandEncoder> c = [cb computeCommandEncoder];
  [c setComputePipelineState:ps]; [c setBuffer:out offset:0 atIndex:0]; [c setBytes:p length:plen atIndex:1];
  for (unsigned i = 0; i < n; ++i) {
    if (i) [c memoryBarrierWithScope:MTLBarrierScopeBuffers];
    [c dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(nt, 1, 1)]; }
  [c endEncoding];
  return cb;
}

int main(int argc, char **argv) { @autoreleasepool {
  enum { CASES = 19 };
  static const char *names[CASES] = {"N32","L1","L2","L3","L4","L5","L6","L7","L8","L9","L10","L11","L12","L13","L14",
                                     "L15","L16","W56","W224"};
  static const unsigned groups[CASES] = {1,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,56,224};
  struct { unsigned R; float a, b; } p = {CLKTWIN_R, 0.999f, 0.001f};
  id<MTLDevice> d = MTLCreateSystemDefaultDevice(); NSError *e = nil;
  MTLCompileOptions *opt = [MTLCompileOptions new];
  if (@available(macOS 15.0, *)) opt.mathMode = MTLMathModeSafe; else opt.fastMathEnabled = NO;
  id<MTLLibrary> l = [d newLibraryWithSource:src options:opt error:&e]; if (!l) { NSLog(@"%@", e); return 1; }
  MTLComputePipelineDescriptor *pd32 = [MTLComputePipelineDescriptor new], *pd1024 = [MTLComputePipelineDescriptor new];
  pd32.computeFunction = [l newFunctionWithName:@"k32"]; pd1024.computeFunction = [l newFunctionWithName:@"k1024"];
  id<MTLComputePipelineState> ps32 = [d newComputePipelineStateWithDescriptor:pd32 options:0 reflection:nil error:&e];
  id<MTLComputePipelineState> ps1024 = [d newComputePipelineStateWithDescriptor:pd1024 options:0 reflection:nil error:&e];
  if (!ps32 || !ps1024) { NSLog(@"%@", e); return 1; }
  printf("CLKTWIN setup R=%u a=%.3f b=%.3f device %s max_threads %lu\n", p.R, p.a, p.b, d.name.UTF8String,
         (unsigned long)ps1024.maxTotalThreadsPerThreadgroup);
  if (argc > 1) {  // code-equality gate: binary archive of both pipelines
    MTLBinaryArchiveDescriptor *ad = [MTLBinaryArchiveDescriptor new];
    id<MTLBinaryArchive> ar = [d newBinaryArchiveWithDescriptor:ad error:&e];
    BOOL ok = ar && [ar addComputePipelineFunctionsWithDescriptor:pd32 error:&e] &&
              [ar addComputePipelineFunctionsWithDescriptor:pd1024 error:&e] &&
              [ar serializeToURL:[NSURL fileURLWithPath:@(argv[1])] error:&e];
    printf("CLKTWIN_ARCHIVE %s %s\n", argv[1], ok ? "written" : e.localizedDescription.UTF8String); }
  id<MTLBuffer> out = [d newBufferWithLength:1u << 20 options:MTLResourceStorageModeShared];
  id<MTLCommandQueue> q = [d newCommandQueue];
  {  // oracle: one L(1) dispatch, four lanes against fused and unfused CPU forms
    id<MTLCommandBuffer> cb = encode(q, ps1024, out, &p, sizeof p, 1, 1024, 1); [cb commit]; [cb waitUntilCompleted];
    static const unsigned lanes[4] = {0, 1, 511, 1023}; unsigned fused = 0, unfused = 0;
    for (int i = 0; i < 4; ++i) {
      float f = expected(lanes[i], p.R, p.a, p.b, 1), u = expected(lanes[i], p.R, p.a, p.b, 0);
      unsigned gv = ((unsigned *)out.contents)[lanes[i]], fv, uv; memcpy(&fv, &f, 4); memcpy(&uv, &u, 4);
      fused += gv == fv; unfused += gv == uv;
      printf("CLKTWIN oracle lane %u gpu 0x%08x fused 0x%08x unfused 0x%08x\n", lanes[i], gv, fv, uv); }
    printf("CLKTWIN_ORACLE fused_match %u/4 unfused_match %u/4 %s\n", fused, unfused,
           fused == 4 ? "FFMA" : unfused == 4 ? "MUL+ADD" : "NEITHER (reassociated sum or other code)"); }
  id<MTLCommandBuffer> last = nil;
  { NSDate *t0 = [NSDate date]; unsigned lists = 0;  // 2 s warm-up, two fillers in flight
    id<MTLCommandBuffer> prev = nil;
    while ([[NSDate date] timeIntervalSinceDate:t0] < 2.0) {
      id<MTLCommandBuffer> cb = encode(q, ps1024, out, &p, sizeof p, 56, 1024, 1); [cb commit];
      if (prev) [prev waitUntilCompleted]; prev = cb; ++lists; }
    last = prev; printf("CLKTWIN warm-up lists %u\n", lists); }
  double best[CASES][2], wall[CASES][2];
  for (int m = 0; m < CASES; ++m) best[m][0] = best[m][1] = wall[m][0] = wall[m][1] = 1e30;
  for (int rep = 0; rep < 3; ++rep) for (unsigned m = 0; m < CASES; ++m) for (int k = 0; k < 2; ++k) {
    unsigned n = k ? 5u : 1u;
    id<MTLCommandBuffer> cb = encode(q, m ? ps1024 : ps32, out, &p, sizeof p, groups[m], m ? 1024u : 32u, n);
    id<MTLCommandBuffer> fill = encode(q, ps1024, out, &p, sizeof p, 56, 1024, 1);
    NSDate *t0 = [NSDate date];
    [cb commit]; [fill commit]; [cb waitUntilCompleted];
    double wms = [[NSDate date] timeIntervalSinceDate:t0] * 1e3, ms = (cb.GPUEndTime - cb.GPUStartTime) * 1e3;
    if (ms < best[m][k]) best[m][k] = ms;
    if (wms < wall[m][k]) wall[m][k] = wms;
    last = fill;
    printf("CLKTWIN rep %d case %s groups %u dispatches %u gpu_ms %.4f wall_ms %.3f status %ld\n", rep, names[m], groups[m], n,
           ms, wms, (long)cb.status); }
  [last waitUntilCompleted];
  double per[CASES], l1; unsigned gstar = 0;
  for (unsigned m = 0; m < CASES; ++m) {
    per[m] = (best[m][1] - best[m][0]) / 4.0;
    printf("CLKTWIN_CASE %s groups %u threads %u t1_ms %.4f t5_ms %.4f per_dispatch_ms %.4f wall1_ms %.3f wall5_ms %.3f\n",
           names[m], groups[m], m ? groups[m] * 1024u : 32u, best[m][0], best[m][1], per[m], wall[m][0], wall[m][1]); }
  l1 = per[1];
  printf("CLKTWIN_LADDER");
  for (unsigned m = 1; m <= 16; ++m) printf(" L%u=%.4f(%.3f)", groups[m], per[m], l1 > 0 ? per[m] / l1 : 0.0);
  printf("\n");
  for (unsigned m = 2; m <= 16 && !gstar; ++m) if (l1 > 0 && per[m] > 1.25 * l1) gstar = groups[m];
  printf("CLKTWIN_GSTAR %u (first G with L(G) > 1.25 x L1; 8 = seven cores in parallel, 7 = six)\n", gstar);
  printf("CLKTWIN_WIDE W56/L1 %.3f (56/7 = 8) W224/L1 %.3f (224/7 = 32) cores_from_W224 %.3f cores_from_W56 %.3f\n",
         l1 > 0 ? per[17] / l1 : 0.0, l1 > 0 ? per[18] / l1 : 0.0, per[18] > 0 ? 224.0 * l1 / per[18] : 0.0,
         per[17] > 0 ? 56.0 * l1 / per[17] : 0.0);
  printf("CLKTWIN_N32 per_dispatch_ms %.4f ns_per_iter %.3f cycles_per_iter_at_1278MHz %.2f (32 FFMA per iteration)\n",
         per[0], per[0] * 1e6 / CLKTWIN_R, per[0] * 1e6 / CLKTWIN_R * 1.278);
  return 0; } }
