# D3D12 C45: heaps the CPU cannot map stay unmapped

## Evidence

EXP1847/EXP1848 ModelViewer (x64) runs at ~5 fps: GPU-timed Scene Render
~190 ms even with a constant pixel shader (variant F). The UMD receipts of one
frame (`x64-ModelViewerF-umd.log`, QPC 24 MHz): the owner's draws cost
bind+draw_vbo+unbind ~1.7 ms, flush ~0.6 ms, retire ~4 ms, yet each
ExecuteCommandLists is preceded by `measure-xfer download=1 token 0x7d bytes
0x03fd0000` taking ~3.9 M ticks (~163 ms). It happens once per present (266
downloads of that token, 266 presents). The buffer is 0x3fc6900 bytes with SRV
and UAV views: MiniEngine's `Lighting::m_LightGrid`
(480 x 270 cells x (4 + 128 x 4) bytes), written by FillLightGrid's compute
shader every frame and read only by the GPU.

## WINDOWS CONTRACT

WDK 26100 `d3d12umddi.h`: `D3D12DDIARG_CREATEHEAP_0001.CPUPageProperty` is
`D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE` for heaps the CPU cannot access (the
runtime's translation of `D3D12_HEAP_TYPE_DEFAULT` and of custom heaps without
CPU access). Map of a resource in such a heap is invalid, so the CPU never
observes their contents; WRITE_COMBINE/WRITE_BACK heaps (UPLOAD, READBACK,
custom CPU heaps) are mapped.

## AGX/ASAHI CONTRACT AND UMD STAGING

The heaps are Asahi buffers of the native owner. `AgxD3d12OwnerCreateBuffer`
mapped every one (`AgxWin32AsahiBufferWriteMap` -> `agx_bo_map`), and CreateHeap
forced `heap->cpu_visible = TRUE`. A mapped slot is not `cpu_quiet`, so
`download_due` holds after every submission that writes it (EXP985 written
list: compute batches add every BO they are the Mesa writer of), and
`complete_render` downloads the whole slot through the copy escape. Unmapped
slots skip that download and the per-submission staging inspection; a later
Mesa map (ExecuteIndirect's `buffer_map` of its argument buffer) downloads the
slot in `AdmissionUmdGpuvaPrepareCpuMap` (EXP995).

## TRANSLATION

CreateHeap keeps `cpu_visible` from `CPUPageProperty` and passes it to
`AgxD3d12OwnerCreateBuffer`; only a CPU-visible heap gets the persistent map.
Other buffers are only marked valid (`AgxWin32AsahiBufferMarkValid`, the same
validity `BufferWriteMap` sets, since D3D12 heaps are zero-initialised). MapHeap
returns the persistent map of a native heap and refuses a native heap without
one (`!cpu_visible`).

## WHAT IS STILL UNKNOWN

Only the measured frame time: whether the remaining ~27 ms between the download
and the owner's receipt is replay work or other staging.

## Checkpoint

ModelViewer B/F/H frame time well below 200 ms with an unchanged image; probes
and the sample sweep unchanged (all use DEFAULT heaps read back through
READBACK heaps).
