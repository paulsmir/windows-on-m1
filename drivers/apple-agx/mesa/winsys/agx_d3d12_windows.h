#ifndef AGX_D3D12_WINDOWS_H
#define AGX_D3D12_WINDOWS_H
/* D3D12 C4b (investigation/analysis/D3D12-C4-copy.md): the native Asahi owner
 * of a D3D12 device -- UMD device storage on its own GPU-VA kernel context,
 * the Asahi screen and context -- built like the D3D10 Windows frontend
 * (agx_d3d10_windows.cpp). D3D12 heaps are buffer resources of this screen
 * and CopyBufferRegion runs through the context's resource_copy_region
 * (Asahi: libagx_copy_uint4/uchar on a compute batch). No Mesa types cross
 * this boundary. */
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AGX_D3D12_OWNER AGX_D3D12_OWNER;

typedef struct {
  void *Resource;            /* struct pipe_resource * */
  unsigned long long GpuVa;  /* Asahi's GPU mapping of the buffer */
  void *Cpu;                 /* persistent CPU mapping */
  unsigned long long Bytes;
} AGX_D3D12_BUFFER;

typedef void (*AGX_D3D12_ERROR_CALLBACK)(void *Context, HRESULT Error);

typedef struct {
  const void *AdapterCallbacks; /* const D3DDDI_ADAPTERCALLBACKS * */
  HANDLE RuntimeAdapter;
  const void *KernelCallbacks;  /* const D3DDDI_DEVICECALLBACKS * */
  HANDLE RuntimeDevice;
  AGX_D3D12_ERROR_CALLBACK Error;
  void *ErrorContext;
  /* the D3D12 device's Win32 generation: the KMD accepts only one per kernel
   * device, so the owner's context and every queue context use it */
  unsigned long Generation;
} AGX_D3D12_OWNER_ARGS;

HRESULT AgxD3d12OwnerCreate(const AGX_D3D12_OWNER_ARGS *Args, AGX_D3D12_OWNER **Owner);
HRESULT AgxD3d12OwnerDestroy(AGX_D3D12_OWNER *Owner);
HRESULT AgxD3d12OwnerCreateBuffer(AGX_D3D12_OWNER *Owner, unsigned long long Bytes,
                                  AGX_D3D12_BUFFER *Buffer);
void AgxD3d12OwnerDestroyBuffer(AGX_D3D12_OWNER *Owner, AGX_D3D12_BUFFER *Buffer);
/* The D3DKMT allocation handle behind a buffer (0 when unknown). */
UINT AgxD3d12OwnerBufferAllocation(AGX_D3D12_OWNER *Owner, const AGX_D3D12_BUFFER *Buffer);
/* Records one GPU copy; byte ranges must lie inside both buffers. */
HRESULT AgxD3d12OwnerCopy(AGX_D3D12_OWNER *Owner, const AGX_D3D12_BUFFER *Dst,
                          unsigned long long DstOffset, const AGX_D3D12_BUFFER *Src,
                          unsigned long long SrcOffset, unsigned long long Bytes);
/* C5 (investigation/analysis/D3D12-C5-compute.md): a compute shader
 * compiled from its D3D tokenized program (DXBC -> NIR -> Asahi), with the
 * thread-group size the program declares. */
typedef struct {
  void *State;          /* the context's compute state object */
  unsigned Block[3];
} AGX_D3D12_COMPUTE;
HRESULT AgxD3d12OwnerCreateCompute(AGX_D3D12_OWNER *Owner, const UINT *Tokens, AGX_D3D12_COMPUTE *Compute);
void AgxD3d12OwnerDestroyCompute(AGX_D3D12_OWNER *Owner, AGX_D3D12_COMPUTE *Compute);
/* A raw/structured UAV bound at shader-buffer slot u#: Buffer NULL = unbound;
 * the view spans [Offset, Buffer->Bytes) (a D3D12 root descriptor has no
 * size). */
#define AGX_D3D12_UAV_SLOTS 8
typedef struct {
  const AGX_D3D12_BUFFER *Buffer;
  unsigned long long Offset;
  unsigned long long Bytes; /* C5c: a view's size; 0 = to the buffer end */
} AGX_D3D12_UAV;
/* C5b: a constant buffer at UBO slot b#: a buffer view (Buffer, Offset,
 * Bytes (0 = to the end); up to 64 KiB, the D3D maximum, never past the
 * buffer) or root constants
 * (Data, Bytes; copied when the dispatch is recorded); both NULL = unbound. */
#define AGX_D3D12_CBV_SLOTS 14
typedef struct {
  const AGX_D3D12_BUFFER *Buffer;
  unsigned long long Offset;
  const void *Data;
  unsigned Bytes;
} AGX_D3D12_CBV;
/* C5d: a raw/structured buffer SRV t#, bound as an R32_UINT texel buffer
 * (the dxbc_to_nir convention: one 32-bit word per texel); Bytes 0 = to the
 * buffer end. */
#define AGX_D3D12_SRV_SLOTS 16
typedef struct {
  const AGX_D3D12_BUFFER *Buffer;
  unsigned long long Offset;
  unsigned long long Bytes;
} AGX_D3D12_SRV;
/* Records one dispatch of x*y*z groups; its writes are ordered before every
 * later recorded command (D3D12 UAV and transition barriers between them
 * need no further AGX work: the launch ends with a full memory barrier). */
HRESULT AgxD3d12OwnerDispatch(AGX_D3D12_OWNER *Owner, const AGX_D3D12_COMPUTE *Compute,
                              const AGX_D3D12_UAV Uavs[AGX_D3D12_UAV_SLOTS],
                              const AGX_D3D12_CBV Cbvs[AGX_D3D12_CBV_SLOTS],
                              const AGX_D3D12_SRV Srvs[AGX_D3D12_SRV_SLOTS], unsigned X, unsigned Y, unsigned Z);
/* Submits the recorded work and waits for it (C4b orders it before the
 * runtime's later queue signal by completing it first). */
HRESULT AgxD3d12OwnerSubmitAndWait(AGX_D3D12_OWNER *Owner);

#ifdef __cplusplus
}
#endif
#endif
