#ifndef APPLE_AGX_GPUVA_B1_COMPLETION_H
#define APPLE_AGX_GPUVA_B1_COMPLETION_H

typedef struct _APPLE_AGX_GPUVA_B1_COMPLETION_IO {
  void *Context;
  unsigned int (*Flush)(void *Context, const void *Address,
                        unsigned int Bytes);
  unsigned int (*Release)(void *Context, unsigned int Fence);
} APPLE_AGX_GPUVA_B1_COMPLETION_IO;

typedef enum _APPLE_AGX_GPUVA_B1_COMPLETION_RESULT {
  AppleAgxGpuvaB1CompletionOk = 0,
  AppleAgxGpuvaB1CompletionFlushFailed,
  AppleAgxGpuvaB1CompletionReleaseFailed
} APPLE_AGX_GPUVA_B1_COMPLETION_RESULT;

static inline APPLE_AGX_GPUVA_B1_COMPLETION_RESULT
AppleAgxGpuvaB1FinishCompletion(
    const APPLE_AGX_GPUVA_B1_COMPLETION_IO *Io,
    const void *Output, unsigned int Bytes, unsigned int Fence) {
  if (Io == (const void *)0 || Io->Flush == (void *)0 ||
      Io->Release == (void *)0 || Output == (const void *)0 ||
      Bytes == 0u || Fence == 0u ||
      !Io->Flush(Io->Context, Output, Bytes))
    return AppleAgxGpuvaB1CompletionFlushFailed;
  if (!Io->Release(Io->Context, Fence))
    return AppleAgxGpuvaB1CompletionReleaseFailed;
  return AppleAgxGpuvaB1CompletionOk;
}

#endif
