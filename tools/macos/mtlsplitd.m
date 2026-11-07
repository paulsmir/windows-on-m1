// mtlsplitd: per-DISPATCH GPU duration of an unmodified Metal app (diagnostic only).
//
// Like mtlsplit, every render/compute/blit encoder gets its own command buffer on the
// app's queue, committed at endEncoding (order preserved; hazard tracking and MTLFence
// work across command buffers of one queue).  In addition, compute encoders whose index
// inside their app command buffer is listed in MTLSPLITD_ENC (comma list, or "all") are
// returned as a proxy: every dispatch after the first ends the current real encoder,
// commits it, opens a fresh encoder in a fresh command buffer and replays the bound
// state (last value per setter slot, every useResource/useHeap) before dispatching.
// An indirect piece starts with its grid-capture blit, which may overlap the previous
// piece: use duration = gpu_end - max(gpu_start, previous piece gpu_end).
// M1 has no dispatch-boundary counter sampling, so this is the only per-dispatch GPU
// time available on Apple's own driver.
//
// Build: clang -fobjc-arc -dynamiclib -framework Metal -framework Foundation \
//          mtlsplitd.m -o mtlsplitd.dylib
// Env:   MTLSPLITD_OUT (default /tmp/mtlsplitd.csv), MTLSPLITD_ENC (e.g. "7,11").
// CSV:   kind,app_cb_seq,enc_index,dispatch_index,gpu_start_s,gpu_end_s,pipeline,
//        tg_x,tg_y,tg_z,tptg_x,tptg_y,tptg_z,indirect
//        [,grid=XxYxZ for indirect: the GPU-written counts, copied by a 12-byte blit]
//        (compute-d = one dispatch piece; compute-e = an empty piece before an indirect one).
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#import <stdatomic.h>

static FILE *g_out;
static NSLock *g_lock;
static atomic_uint g_seq;
static NSSet<NSNumber *> *g_split_enc;
static BOOL g_split_all;
static const void *kSub = &kSub, *kInfo = &kInfo;

@interface SplitInfo : NSObject
@property unsigned seq;
@property unsigned n;
@end
@implementation SplitInfo
@end

static void out_line(NSString *s) {
  [g_lock lock];
  if (g_out) { fputs(s.UTF8String, g_out); fputc('\n', g_out); fflush(g_out); }
  [g_lock unlock];
}

static SplitInfo *info(id cb) {
  SplitInfo *i = objc_getAssociatedObject(cb, kInfo);
  if (!i) {
    i = [SplitInfo new];
    i.seq = atomic_fetch_add(&g_seq, 1);
    objc_setAssociatedObject(cb, kInfo, i, OBJC_ASSOCIATION_RETAIN);
  }
  return i;
}

static id<MTLCommandBuffer> new_cb(id<MTLCommandQueue> q, NSString *prefix) {
  id<MTLCommandBuffer> s = [q commandBuffer];
  [s addCompletedHandler:^(id<MTLCommandBuffer> c) {
    out_line([NSString stringWithFormat:@"%@,%.9f,%.9f", prefix, c.GPUStartTime, c.GPUEndTime]);
  }];
  return s;
}

typedef id (*Enc1)(id, SEL, id);
typedef id (*Enc0)(id, SEL);
typedef id (*EncT)(id, SEL, MTLDispatchType);
typedef void (*EndFn)(id, SEL);
static Enc1 o_render, o_cpass, o_bpass;
static Enc0 o_cplain, o_bplain;
static EncT o_ctype;
static EndFn o_end_r, o_end_c, o_end_b;

static NSString *pipe_name(id ps);

// ---------------------------------------------------------------- split proxy
@interface SplitEncoder : NSProxy {
@public
  id cur;                         // current real compute encoder
  id<MTLCommandBuffer> curCb;     // its command buffer (not yet committed)
  id<MTLCommandQueue> queue;
  int kind;                       // 0 plain, 1 descriptor, 2 dispatch type
  id passDesc;
  MTLDispatchType dtype;
  unsigned seq, enc, ndisp;
  BOOL dispatchedInCur;
  NSMutableDictionary<NSString *, NSInvocation *> *state;  // slot -> last setter (lookup only)
  NSMutableArray<NSInvocation *> *history;  // every setter in call order (replayed in order)
  NSMutableArray<NSInvocation *> *uses;                     // useResource/useHeap
  NSString *pending;               // CSV fields of the dispatch in curCb
}
@end

static id make_real(SplitEncoder *p, id<MTLCommandBuffer> cb) {
  switch (p->kind) {
  case 1: return o_cpass(cb, @selector(computeCommandEncoderWithDescriptor:), p->passDesc);
  case 2: return o_ctype(cb, @selector(computeCommandEncoderWithDispatchType:), p->dtype);
  default: return o_cplain(cb, @selector(computeCommandEncoder));
  }
}

static NSString *slot_key(NSInvocation *inv) {
  NSString *name = NSStringFromSelector(inv.selector);
  NSMethodSignature *sig = inv.methodSignature;
  NSUInteger n = sig.numberOfArguments;
  if ([name hasSuffix:@"atIndex:"] && n > 2) {
    NSUInteger idx = 0;
    [inv getArgument:&idx atIndex:n - 1];
    return [NSString stringWithFormat:@"%@#%lu", name, (unsigned long)idx];
  }
  if ([name hasSuffix:@"withRange:"] && n > 2) {
    NSRange r = {0, 0};
    [inv getArgument:&r atIndex:n - 1];
    return [NSString stringWithFormat:@"%@#%lu+%lu", name, (unsigned long)r.location,
                                      (unsigned long)r.length];
  }
  return name;  // setComputePipelineState:, setImageblockWidth:height:, setStageInRegion: ...
}

// retainArguments keeps object arguments alive but copies only the POINTER of C arrays and
// setBytes data; copy those so a replay after the app reused its memory stays exact.
static const void *kKeep = &kKeep;
static void copy_arg(NSInvocation *inv, NSUInteger at, size_t bytes, BOOL objects, NSUInteger count) {
  void *ptr = NULL;
  [inv getArgument:&ptr atIndex:at];
  if (!ptr || !bytes) return;
  NSMutableData *d = [NSMutableData dataWithBytes:ptr length:bytes];
  void *np = d.mutableBytes;
  [inv setArgument:&np atIndex:at];
  NSMutableArray *keep = objc_getAssociatedObject(inv, kKeep);
  if (!keep) {
    keep = [NSMutableArray new];
    objc_setAssociatedObject(inv, kKeep, keep, OBJC_ASSOCIATION_RETAIN);
  }
  [keep addObject:d];
  if (objects) {
    __unsafe_unretained id *o = (__unsafe_unretained id *)np;
    for (NSUInteger k = 0; k < count; ++k)
      if (o[k]) [keep addObject:o[k]];
  }
}

static void deep_copy(NSInvocation *inv) {
  NSString *n = NSStringFromSelector(inv.selector);
  if ([n isEqualToString:@"setBytes:length:atIndex:"]) {
    NSUInteger len = 0;
    [inv getArgument:&len atIndex:3];
    copy_arg(inv, 2, len, NO, 0);
  } else if ([n hasSuffix:@"withRange:"]) {
    NSRange r = {0, 0};
    [inv getArgument:&r atIndex:inv.methodSignature.numberOfArguments - 1];
    copy_arg(inv, 2, r.length * sizeof(id), YES, r.length);
    if ([n isEqualToString:@"setBuffers:offsets:withRange:"])
      copy_arg(inv, 3, r.length * sizeof(NSUInteger), NO, 0);
    if ([n isEqualToString:@"setSamplerStates:lodMinClamps:lodMaxClamps:withRange:"]) {
      copy_arg(inv, 3, r.length * sizeof(float), NO, 0);
      copy_arg(inv, 4, r.length * sizeof(float), NO, 0);
    }
  } else if ([n hasPrefix:@"useResources:count:"] || [n isEqualToString:@"useHeaps:count:"]) {
    NSUInteger count = 0;
    [inv getArgument:&count atIndex:3];
    copy_arg(inv, 2, count * sizeof(id), YES, count);
  }
}

@implementation SplitEncoder

- (NSMethodSignature *)methodSignatureForSelector:(SEL)sel {
  return [cur methodSignatureForSelector:sel];
}

- (BOOL)respondsToSelector:(SEL)sel { return [cur respondsToSelector:sel]; }

- (void)flushCur {
  o_end_c(cur, @selector(endEncoding));
  NSString *fields = pending ? pending : [NSString stringWithFormat:@"compute-d,%u,%u,-", seq, enc];
  // the completed handler was attached at creation with a placeholder; re-tag via a block box
  objc_setAssociatedObject(curCb, kSub, fields, OBJC_ASSOCIATION_RETAIN);
  [curCb commit];
}

// A new piece; with an indirect buffer, a 12-byte blit first copies the GPU-written
// threadgroup counts into a shared buffer so the CSV records the real grid.
- (void)openNewIndirect:(id<MTLBuffer>)ib offset:(NSUInteger)off {
  curCb = [queue commandBuffer];
  pending = nil;
  id<MTLCommandBuffer> mine = curCb;
  id<MTLBuffer> cap = nil;
  if (ib) {
    cap = [queue.device newBufferWithLength:12 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> b = o_bplain(curCb, @selector(blitCommandEncoder));
    [b copyFromBuffer:ib sourceOffset:off toBuffer:cap destinationOffset:0 size:12];
    o_end_b(b, @selector(endEncoding));
  }
  [curCb addCompletedHandler:^(id<MTLCommandBuffer> c) {
    NSString *f = objc_getAssociatedObject(mine, kSub);
    NSString *grid = @"";
    if (cap) {
      const uint32_t *g = cap.contents;
      grid = [NSString stringWithFormat:@",grid=%ux%ux%u", g[0], g[1], g[2]];
    }
    out_line([NSString stringWithFormat:@"%@,%.9f,%.9f%@", f ? f : @"compute-d,?", c.GPUStartTime,
                                        c.GPUEndTime, grid]);
  }];
  cur = make_real(self, curCb);
  for (NSInvocation *inv in uses) [inv invokeWithTarget:cur];
  // Replay every setter in the app's call order: setBuffer/setBufferOffset/setBytes on one
  // index have different selectors, so a per-selector "last value" replayed in dictionary
  // order could let a stale buffer or offset win (EXP2226: BIF0 page faults at address 0).
  for (NSInvocation *inv in history) [inv invokeWithTarget:cur];
  dispatchedInCur = NO;
}

- (void)openNew { [self openNewIndirect:nil offset:0]; }

- (void)forwardInvocation:(NSInvocation *)inv {
  SEL sel = inv.selector;
  NSString *name = NSStringFromSelector(sel);
  if (sel == @selector(endEncoding)) {
    [self flushCur];
    return;
  }
  BOOL isDispatch = [name hasPrefix:@"dispatch"];
  if (isDispatch) {
    BOOL indirect = [name containsString:@"Indirect"];
    if (indirect) {
      // always a fresh piece, so the grid capture blit precedes the dispatch
      __unsafe_unretained id ib = nil;
      NSUInteger off = 0;
      [inv getArgument:&ib atIndex:2];
      [inv getArgument:&off atIndex:3];
      if (!dispatchedInCur) pending = [NSString stringWithFormat:@"compute-e,%u,%u,-", seq, enc];
      [self flushCur];
      [self openNewIndirect:ib offset:off];
    } else if (dispatchedInCur) {
      [self flushCur];
      [self openNew];
    }
    MTLSize g = {0, 0, 0}, t = {0, 0, 0};
    NSUInteger n = inv.methodSignature.numberOfArguments;
    if (!indirect) [inv getArgument:&g atIndex:2];
    [inv getArgument:&t atIndex:n - 1];
    id ps = nil;
    NSInvocation *pinv = state[@"setComputePipelineState:"];
    if (pinv) {
      __unsafe_unretained id tmp = nil;
      [pinv getArgument:&tmp atIndex:2];
      ps = tmp;
    }
    NSString *label = pipe_name(ps);
    pending = [NSString stringWithFormat:@"compute-d,%u,%u,%u,%@,%lu,%lu,%lu,%lu,%lu,%lu,%d", seq, enc,
                                         ndisp++, label, (unsigned long)g.width,
                                         (unsigned long)g.height, (unsigned long)g.depth,
                                         (unsigned long)t.width, (unsigned long)t.height,
                                         (unsigned long)t.depth, indirect ? 1 : 0];
    // CSV order: fields..., gpu_start, gpu_end -- reorder in the analyser
    [inv invokeWithTarget:cur];
    dispatchedInCur = YES;
    return;
  }
  // invoke first (the app's memory is still valid), then keep a deep copy for replay
  [inv invokeWithTarget:cur];
  if ([name hasPrefix:@"set"] && ![name isEqualToString:@"setLabel:"]) {
    [inv retainArguments];
    deep_copy(inv);
    state[slot_key(inv)] = inv;
    [history addObject:inv];
  } else if ([name hasPrefix:@"use"] || [name hasPrefix:@"waitForFence"]) {
    // waitForFence must precede the first dispatch of every piece: replay it as state.
    [inv retainArguments];
    deep_copy(inv);
    [uses addObject:inv];
  }
  // memoryBarrier*, labels, debug groups, updateFence: forwarded only (an encoder boundary
  // orders more strongly than a barrier inside one encoder).
}
@end

// a compute encoder for the app: plain sub command buffer, or the split proxy
static id compute_for(id<MTLCommandBuffer> app, int kind, id desc, MTLDispatchType t) {
  SplitInfo *i = info(app);
  unsigned k = i.n++, seq = i.seq;
  if (g_split_all || [g_split_enc containsObject:@(k)]) {
    SplitEncoder *p = [SplitEncoder alloc];
    p->queue = app.commandQueue;
    p->kind = kind;
    p->passDesc = desc;
    p->dtype = t;
    p->seq = seq;
    p->enc = k;
    p->state = [NSMutableDictionary new];
    p->history = [NSMutableArray new];
    p->uses = [NSMutableArray new];
    [p openNew];
    return p;
  }
  id<MTLCommandBuffer> s =
      new_cb(app.commandQueue, [NSString stringWithFormat:@"compute,%u,%u,-", seq, k]);
  id e = kind == 1   ? o_cpass(s, @selector(computeCommandEncoderWithDescriptor:), desc)
         : kind == 2 ? o_ctype(s, @selector(computeCommandEncoderWithDispatchType:), t)
                     : o_cplain(s, @selector(computeCommandEncoder));
  objc_setAssociatedObject(e, kSub, s, OBJC_ASSOCIATION_RETAIN);
  return e;
}

static id other_for(id<MTLCommandBuffer> app, NSString *kind) {
  SplitInfo *i = info(app);
  unsigned k = i.n++;
  return new_cb(app.commandQueue, [NSString stringWithFormat:@"%@,%u,%u,-", kind, i.seq, k]);
}

static id tag(id enc, id<MTLCommandBuffer> s) {
  objc_setAssociatedObject(enc, kSub, s, OBJC_ASSOCIATION_RETAIN);
  return enc;
}
static id h_render(id self, SEL c, id d) {
  id<MTLCommandBuffer> s = other_for(self, @"render");
  return tag(o_render(s, c, d), s);
}
static id h_cpass(id self, SEL c, id d) { return compute_for(self, 1, d, MTLDispatchTypeSerial); }
static id h_cplain(id self, SEL c) { return compute_for(self, 0, nil, MTLDispatchTypeSerial); }
static id h_ctype(id self, SEL c, MTLDispatchType t) { return compute_for(self, 2, nil, t); }
static id h_bplain(id self, SEL c) {
  id<MTLCommandBuffer> s = other_for(self, @"blit");
  return tag(o_bplain(s, c), s);
}
static id h_bpass(id self, SEL c, id d) {
  id<MTLCommandBuffer> s = other_for(self, @"blit");
  return tag(o_bpass(s, c, d), s);
}
static void commit_sub(id self) {
  id<MTLCommandBuffer> s = objc_getAssociatedObject(self, kSub);
  if (s) {
    objc_setAssociatedObject(self, kSub, nil, OBJC_ASSOCIATION_RETAIN);
    [s commit];
  }
}
static void h_end_r(id self, SEL c) { o_end_r(self, c); commit_sub(self); }
static void h_end_c(id self, SEL c) { o_end_c(self, c); commit_sub(self); }
static void h_end_b(id self, SEL c) { o_end_b(self, c); commit_sub(self); }

// pipeline -> kernel function name (pipelines are usually unlabeled)
static NSMapTable *g_names;
static NSString *pipe_name(id ps) {
  if (!ps) return @"-";
  NSString *n;
  [g_lock lock];
  n = [g_names objectForKey:ps];
  [g_lock unlock];
  if (n) return n;
  return [ps label] ? [ps label] : [NSString stringWithFormat:@"%p", ps];
}
static void remember(id ps, id fn) {
  if (!ps || ![fn respondsToSelector:@selector(name)]) return;
  [g_lock lock];
  [g_names setObject:[fn name] forKey:ps];
  [g_lock unlock];
}
typedef id (*PF)(id, SEL, id, id *);
typedef id (*PFO)(id, SEL, id, MTLPipelineOption, id *, id *);
static PF o_pf;
static PFO o_pfo, o_pdo;
static id h_pf(id self, SEL c, id fn, id *err) {
  id ps = o_pf(self, c, fn, err);
  remember(ps, fn);
  return ps;
}
static id h_pfo(id self, SEL c, id fn, MTLPipelineOption o, id *r, id *err) {
  id ps = o_pfo(self, c, fn, o, r, err);
  remember(ps, fn);
  return ps;
}
static id h_pdo(id self, SEL c, id desc, MTLPipelineOption o, id *r, id *err) {
  id ps = o_pdo(self, c, desc, o, r, err);
  remember(ps, [desc computeFunction]);
  return ps;
}

static void hook(Class k, SEL s, IMP n, IMP *o) {
  Method m = class_getInstanceMethod(k, s);
  if (m) *o = method_setImplementation(m, n);
}

__attribute__((constructor)) static void init(void) {
  @autoreleasepool {
    g_lock = [NSLock new];
    const char *p = getenv("MTLSPLITD_OUT");
    g_out = fopen(p ? p : "/tmp/mtlsplitd.csv", "w");
    const char *e = getenv("MTLSPLITD_ENC");
    NSMutableSet *set = [NSMutableSet new];
    if (e && !strcmp(e, "all")) g_split_all = YES;
    else if (e)
      for (NSString *x in [@(e) componentsSeparatedByString:@","])
        if (x.length) [set addObject:@(x.intValue)];
    g_split_enc = set;
    g_names = [NSMapTable mapTableWithKeyOptions:NSPointerFunctionsWeakMemory |
                                                 NSPointerFunctionsObjectPointerPersonality
                                    valueOptions:NSPointerFunctionsStrongMemory];
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    Class dc = [dev class];
    hook(dc, @selector(newComputePipelineStateWithFunction:error:), (IMP)h_pf, (IMP *)&o_pf);
    hook(dc, @selector(newComputePipelineStateWithFunction:options:reflection:error:), (IMP)h_pfo,
         (IMP *)&o_pfo);
    hook(dc, @selector(newComputePipelineStateWithDescriptor:options:reflection:error:),
         (IMP)h_pdo, (IMP *)&o_pdo);
    id<MTLCommandQueue> q = [dev newCommandQueue];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    MTLTextureDescriptor *td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:4 height:4 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = [dev newTextureWithDescriptor:td];
    id re = [cb renderCommandEncoderWithDescriptor:rp];
    Class rc = [re class];
    [re endEncoding];
    id ce = [cb computeCommandEncoder];
    Class cc = [ce class];
    [ce endEncoding];
    id be = [cb blitCommandEncoder];
    Class bc = [be class];
    [be endEncoding];
    Class cbc = [cb class];
    hook(cbc, @selector(renderCommandEncoderWithDescriptor:), (IMP)h_render, (IMP *)&o_render);
    hook(cbc, @selector(computeCommandEncoderWithDescriptor:), (IMP)h_cpass, (IMP *)&o_cpass);
    hook(cbc, @selector(computeCommandEncoder), (IMP)h_cplain, (IMP *)&o_cplain);
    hook(cbc, @selector(computeCommandEncoderWithDispatchType:), (IMP)h_ctype, (IMP *)&o_ctype);
    hook(cbc, @selector(blitCommandEncoder), (IMP)h_bplain, (IMP *)&o_bplain);
    hook(cbc, @selector(blitCommandEncoderWithDescriptor:), (IMP)h_bpass, (IMP *)&o_bpass);
    hook(rc, @selector(endEncoding), (IMP)h_end_r, (IMP *)&o_end_r);
    hook(cc, @selector(endEncoding), (IMP)h_end_c, (IMP *)&o_end_c);
    hook(bc, @selector(endEncoding), (IMP)h_end_b, (IMP *)&o_end_b);
    out_line([NSString stringWithFormat:@"# mtlsplitd %@ cb=%@ c=%@ split=%s", dev.name,
                                        NSStringFromClass(cbc), NSStringFromClass(cc),
                                        e ? e : "none"]);
  }
}
