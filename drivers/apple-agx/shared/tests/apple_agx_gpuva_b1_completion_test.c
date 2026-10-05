#include "apple_agx_gpuva_b1_completion.h"
#include <assert.h>

struct fixture { unsigned int bound, flushed, released; };

static unsigned int flush(void *opaque, const void *address,
                          unsigned int bytes) {
  struct fixture *f = opaque;
  assert(address != 0 && bytes == 0x4000u);
  if (!f->bound) return 0u;
  f->flushed = 1u;
  return 1u;
}

static unsigned int release(void *opaque, unsigned int fence) {
  struct fixture *f = opaque;
  assert(fence == 1u);
  if (!f->bound || !f->flushed) return 0u;
  f->bound = 0u;
  f->released = 1u;
  return 1u;
}

int main(void) {
  struct fixture f = {1u, 0u, 0u};
  APPLE_AGX_GPUVA_B1_COMPLETION_IO io = {&f, flush, release};
  assert(AppleAgxGpuvaB1FinishCompletion(&io, &f, 0x4000u, 1u) ==
         AppleAgxGpuvaB1CompletionOk);
  assert(f.flushed && f.released && !f.bound);

  f = (struct fixture){0u, 0u, 0u};
  assert(AppleAgxGpuvaB1FinishCompletion(&io, &f, 0x4000u, 1u) ==
         AppleAgxGpuvaB1CompletionFlushFailed);
  assert(!f.released);
  return 0;
}
