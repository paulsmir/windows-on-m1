# D3D12 C4: command lists and the first GPU copy

Status 2026-10-17. C1 (EXP1355), C2 (EXP1359) and C3 (EXP1362) are proven.
C4a (95f8a45e, EXP1363) adds allocators, lists with their runtime DDI table,
Close/Reset and execution of lists that record nothing. C4b is the first
GPU work recorded by a D3D12 list: CopyBufferRegion between two committed
buffers, verified on the CPU.

## WINDOWS CONTRACT

- d3d12umddi.h: list-table PFND3D12DDI_COPYBUFFERREGION_0003 (dst resource,
  dst offset, src resource, src offset, bytes) and PFND3D12DDI_RESOURCEBARRIER
  (transitions UPLOAD/COPY_DEST/COPY_SOURCE); queue-table
  PFND3D12DDI_EXECUTECOMMANDLISTS; the runtime signals the fence through the
  queue's kernel context after the execute (C3: EXP1362).
- WDDM 2 GpuMmu submission from a UMD: D3DDDICB_SUBMITCOMMAND on the queue's
  context (the D3D12 core-layer callbacks expose the KM callbacks of the
  device), command buffer GPU VA + private driver data; the runtime orders a
  later queue signal after it (Dxgkrnl monitored-fence re-read at DMA
  completion, "Context monitoring").

## AGX/ASAHI CONTRACT

- Kernels: Mesa libagx copy.cl `libagx_copy_uint4` (32 lanes, 16 bytes per
  lane) and `libagx_copy_uchar` (tail), precompiled for G13 by the Mesa build
  (the same libagx the D3D11 path links).
- Submission: the KMD G4 compute envelope (apple_agx_g4_submit.h):
  APPLE_AGX_G4_PRIVATE_HEADER_COMPUTE (+ CPMT preemption range) +
  APPLE_AGX_G4_NATIVE_COMPUTE (drm_asahi_cmd_compute: CDM control stream
  base/end, sampler heap, helper, timestamps); USC addresses are 32-bit
  offsets from APPLE_AGX_G4_USC_EXECUTION_BASE (0x1100000000) inside the
  4 GiB USC window; the CDM stream and programs live in the process GPU VA.
  The KMD parser (apple_agx_g4_submit.c, AppleAgxG4ParseComputeEx) proves every
  referenced range is mapped and builds the firmware RunCompute item.
- CDM encoding: Asahi genxml (CDM launch with USC pipeline, grid/workgroup,
  stream terminate), as agx_launch / hk_dispatch emit it.

## TRANSLATION

1. Device: one CPU-visible C2a heap in the USC window holding the uploaded
   libagx copy programs (shader binary + USC control words) and a per-queue
   arena for CDM streams, argument buffers and the CPMT buffer.
2. CopyBufferRegion records {dst VA + offset, src VA + offset, bytes} in the
   list.
3. ExecuteCommandLists: for each recorded copy, write the kernel arguments
   (dest, src pointers) and a CDM launch of ceil(bytes / 16) lanes of
   copy_uint4 (+ copy_uchar for the tail) into the arena, then one G4 compute
   envelope submitted on the queue's context with the allocation list of the
   heaps involved (residency).
4. ResourceBarrier: no AGX work between copy kernels on one queue beyond the
   CDM barrier bits the envelope already carries (APPLE_AGX_G4_NATIVE_HEADER
   CdmBarrier); recorded as a no-op until a case needs more.

## WHAT IS STILL UNKNOWN

- How the D3D12 frontend reaches the precompiled libagx binaries and the CDM
  encoders without a Gallium context (link the libagx/genxml objects the
  D3D11 UMD already builds; RED -> GREEN offline test of the encoded stream
  against Mesa's agx_launch for the same kernel).
- Whether the KMD's compute parser accepts a submission from a D3D12 queue
  context whose process mappings come from the D3D12 heaps (it validates the
  same GPU VA space) -- hardware.
- The CPMT size requirement for G13 (the KMD passes it to the parser).

## Checkpoints

- C4a (EXP1363): empty list execute + signal.
- C4b: CopyBufferRegion 64 KiB UPLOAD -> DEFAULT -> READBACK (two copies),
  CPU compares; receipts for the envelope and the KMD parse.
