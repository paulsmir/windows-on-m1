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
#include "../dxbc/dxbc_bindings.h"
#include "../dxbc/dxbc_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AGX_D3D12_OWNER AGX_D3D12_OWNER;
#if defined(APPLE_AGX_D3D12_PREPARED_NIR_OWNER) && APPLE_AGX_D3D12_PREPARED_NIR_OWNER
struct AgxOwnedNir;
#endif

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
/* EXP1822: the owner's recursive lock (ExecuteCommandLists holds it across a replay). */
void AgxD3d12OwnerLock(AGX_D3D12_OWNER *Owner);
void AgxD3d12OwnerUnlock(AGX_D3D12_OWNER *Owner);
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
  struct dxbc_binding_map Bindings; /* C17: immutable SM5.1 semantic-to-slot map */
  uint8_t UavDimensions[DXBC_UAV_DIMENSION_SLOTS];
  uint8_t SrvDimensions[DXBC_RESOURCE_SLOTS]; /* C19: owned direct-slot requirements */
} AGX_D3D12_COMPUTE;
HRESULT AgxD3d12OwnerCreateCompute(AGX_D3D12_OWNER *Owner, const UINT *Tokens, AGX_D3D12_COMPUTE *Compute);
#if defined(APPLE_AGX_D3D12_PREPARED_NIR_OWNER) && APPLE_AGX_D3D12_PREPARED_NIR_OWNER
/* Unwired prepared-compute boundary. Live caller-serialized owner/screen/context
 * and actual prepared object required. Memberwise-empty output; nonempty live
 * output refused unchanged. Root/device compatibility remains caller-owned. */
HRESULT AgxD3d12OwnerCreateComputePrepared(AGX_D3D12_OWNER *Owner,
 const struct AgxOwnedNir *Prepared, AGX_D3D12_COMPUTE *Compute);
#endif /* APPLE_AGX_D3D12_PREPARED_NIR_OWNER */
void AgxD3d12OwnerDestroyCompute(AGX_D3D12_OWNER *Owner, AGX_D3D12_COMPUTE *Compute);
/* A raw/structured UAV bound at shader-buffer slot u#: Buffer NULL = unbound;
 * the view spans [Offset, Buffer->Bytes) (a D3D12 root descriptor has no
 * size). */
#define AGX_D3D12_UAV_SLOTS 8
typedef struct {
  const AGX_D3D12_BUFFER *Buffer;
  unsigned long long Offset;
  unsigned long long Bytes; /* C5c: a view's size; 0 = to the buffer end */
  /* C9: a typed view instead -- image slot u# of Format (DXGI, non-zero):
   * a buffer range, or a 2D (array) texture level and layer range when
   * Texture (Buffer = the texture) */
  DXGI_FORMAT Format;
  int Texture;
  unsigned Level, FirstLayer, Layers;
  /* C33: a structured UAV's append/consume counter -- one 32-bit word at
   * CounterOffset of Counter, shader-buffer slot DXBC_NIR_UAV_COUNTER_SLOT
   * + u# (NULL = none) */
  const AGX_D3D12_BUFFER *Counter;
  unsigned long long CounterOffset;
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
 * buffer end. C9: a typed buffer SRV has its DXGI Format (Texture 0). */
#define AGX_D3D12_SRV_SLOTS 128
typedef struct {
  const AGX_D3D12_BUFFER *Buffer;
  unsigned long long Offset;
  unsigned long long Bytes;
  /* C6e: a 2D (array) texture view instead (Buffer = the texture) */
  int Texture;
  DXGI_FORMAT Format;
  unsigned FirstLevel, Levels, FirstLayer, Layers;
  unsigned ComponentMapping; /* C18: encoded WDK SRV component mapping */
  unsigned Cube; /* C19: cube-class view; actual arrayness from compiled shader */
} AGX_D3D12_SRV;
/* C6e: a sampler s# in the D3D10/11/12 DDI encoding (filter bits, address
 * modes 1..5, comparison 1..8) */
#define AGX_D3D12_SAMPLER_SLOTS 16
typedef struct {
  int Valid;
  unsigned Filter, AddressU, AddressV, AddressW, MaxAnisotropy, ComparisonFunc;
  float MipLodBias, MinLod, MaxLod, BorderColor[4];
} AGX_D3D12_SAMPLER;
/* Records one dispatch of x*y*z groups; its writes are ordered before every
 * later recorded command (D3D12 UAV and transition barriers between them
 * need no further AGX work: the launch ends with a full memory barrier). */
/* C14 (D3D12-C14-execute-indirect.md): an indirect argument stream --
 * MaxCount records of Stride bytes at Offset (D3D12_DRAW_ARGUMENTS,
 * _DRAW_INDEXED_ARGUMENTS or _DISPATCH_ARGUMENTS, the Gallium indirect
 * layouts), optionally limited by a UINT count at CountOffset. */
typedef struct {
  const AGX_D3D12_BUFFER *Buffer; /* NULL = a direct draw/dispatch */
  unsigned long long Offset;
  unsigned Stride, MaxCount;
  const AGX_D3D12_BUFFER *CountBuffer;
  unsigned long long CountOffset;
} AGX_D3D12_INDIRECT;
/* C9: CBVs, SRVs (buffers, typed buffers, textures) and samplers bind as a
 * draw stage's do. C14: Indirect (NULL = direct) launches MaxCount grids
 * read from the argument buffer (no count buffer). */
HRESULT AgxD3d12OwnerDispatch(AGX_D3D12_OWNER *Owner, const AGX_D3D12_COMPUTE *Compute,
                              const AGX_D3D12_UAV Uavs[AGX_D3D12_UAV_SLOTS],
                              const AGX_D3D12_CBV Cbvs[AGX_D3D12_CBV_SLOTS],
                              const AGX_D3D12_SRV Srvs[AGX_D3D12_SRV_SLOTS],
                              const AGX_D3D12_SAMPLER Samplers[AGX_D3D12_SAMPLER_SLOTS], unsigned X, unsigned Y,
                              unsigned Z, const AGX_D3D12_INDIRECT *Indirect);
/* C6a (investigation/analysis/D3D12-C6-graphics.md): textures. Bytes of
 * one texel block of a DXGI format the owner can create (uncompressed,
 * 1x1 blocks), 0 otherwise. */
unsigned AgxD3d12FormatBlockBytes(DXGI_FORMAT Format);
/* C29: bytes of one texture element of a format CreateTexture2D accepts --
 * a 1x1 texel or a BC1-BC7 4x4 block (sampler-only) -- with its *Width x
 * *Height in texels; 0 otherwise. */
unsigned AgxD3d12FormatBlock(DXGI_FORMAT Format, unsigned *Width, unsigned *Height);
/* C31: Bytes bytes of a buffer at Offset, read after the work recorded on
 * the owner's context that writes it (a synchronised map) */
HRESULT AgxD3d12OwnerReadBuffer(AGX_D3D12_OWNER *Owner, const AGX_D3D12_BUFFER *Buffer, unsigned long long Offset,
                                unsigned Bytes, void *Data);
#define AGX_D3D12_BIND_RENDER_TARGET 0x1u
#define AGX_D3D12_BIND_SAMPLER 0x2u
#define AGX_D3D12_BIND_DEPTH_STENCIL 0x4u /* C6f: also translates typeless formats as depth */
/* A 2D texture (array when Layers > 1) of the owner's screen; Texture->Cpu
 * is NULL, Bytes the caller's allocation size. */
HRESULT AgxD3d12OwnerCreateTexture2D(AGX_D3D12_OWNER *Owner, DXGI_FORMAT Format, unsigned Width, unsigned Height,
                                     unsigned Layers, unsigned Levels, unsigned Bind, unsigned long long Bytes,
                                     AGX_D3D12_BUFFER *Texture, unsigned Samples);
/* C32: whether a format has 2x/4x multisampled textures (the AGX's sample
 * counts, agx_is_format_supported): colour and depth formats of 1x1 texel
 * blocks */
int AgxD3d12FormatMultisample(DXGI_FORMAT Format, unsigned Samples);
/* C32: ResolveSubresource -- the average of a multisampled level's samples
 * into a single-sampled level of the same size (a Gallium resolve blit) */
HRESULT AgxD3d12OwnerResolve(AGX_D3D12_OWNER *Owner, const AGX_D3D12_BUFFER *Dst, unsigned DstLevel, unsigned DstLayer,
                             const AGX_D3D12_BUFFER *Src, unsigned SrcLayer, DXGI_FORMAT Format);
/* C21 LIMITED: POT square1..32,depth1..2048,levels1/2,R32_UINT/RGBA8.
 * Pure allocation query; no owner/Screen/device creation.
 * Actual creation uses the same uncompressed one-BO modifier and64KiB size. */
typedef struct {
    unsigned long long Bytes, NativeBytes, DepthStride, MipOffsets[16];
} AGX_D3D12_VOLUME_LAYOUT;
/* C23: same pointer-free native extent/mip record, ordinary nonarray Texture1D. */
HRESULT AgxD3d12QueryTexture1DArrayLayout(DXGI_FORMAT Format,unsigned Width,unsigned Layers,unsigned Levels,unsigned Bind,AGX_D3D12_VOLUME_LAYOUT *Layout);
HRESULT AgxD3d12OwnerCreateTexture1DArray(AGX_D3D12_OWNER *Owner,DXGI_FORMAT Format,unsigned Width,unsigned Layers,unsigned Levels,unsigned Bind,unsigned long long Bytes,AGX_D3D12_BUFFER *Texture);
HRESULT AgxD3d12QueryTexture1DLayout(DXGI_FORMAT Format,unsigned Width,unsigned Levels,unsigned Bind,AGX_D3D12_VOLUME_LAYOUT *Layout);
HRESULT AgxD3d12OwnerCreateTexture1D(AGX_D3D12_OWNER *Owner,DXGI_FORMAT Format,unsigned Width,unsigned Levels,unsigned Bind,unsigned long long Bytes,AGX_D3D12_BUFFER *Texture);
HRESULT AgxD3d12QueryTexture3DLayout(DXGI_FORMAT Format,unsigned Width,unsigned Height,unsigned Depth,
    unsigned Levels,unsigned Bind,AGX_D3D12_VOLUME_LAYOUT *Layout);
HRESULT AgxD3d12QueryTexture3D(DXGI_FORMAT Format,unsigned Width,unsigned Height,unsigned Depth,
    unsigned Levels,unsigned Bind,unsigned long long *Bytes,unsigned long long *MipOffsets /* NULL or16entries */);
HRESULT AgxD3d12OwnerCreateTexture3D(AGX_D3D12_OWNER *Owner,DXGI_FORMAT Format,unsigned Width,
    unsigned Height,unsigned Depth,unsigned Levels,unsigned Bind,unsigned long long Bytes,AGX_D3D12_BUFFER *Texture);
/* Clears layers [FirstLayer, FirstLayer + Layers) of a mip level in the view
 * format; Rects NULL = the whole level. */
HRESULT AgxD3d12OwnerClearRenderTarget(AGX_D3D12_OWNER *Owner, const AGX_D3D12_BUFFER *Texture, DXGI_FORMAT ViewFormat,
                                       unsigned Level, unsigned FirstLayer, unsigned Layers, const float Color[4],
                                       const RECT *Rects, unsigned RectCount);
/* A copy between a texture region (Level, Layer, X/Y, Width x Height
 * texels, Depth layers) and a pitched buffer footprint starting at Offset. */
typedef struct {
  const AGX_D3D12_BUFFER *Texture;
  unsigned Level, Layer, X, Y;
  unsigned Width, Height, Depth;
  const AGX_D3D12_BUFFER *Buffer;
  unsigned long long Offset;
  unsigned Pitch, SlicePitch;
  int ToBuffer; /* 1: texture -> buffer, 0: buffer -> texture */
} AGX_D3D12_TEXTURE_COPY;
HRESULT AgxD3d12OwnerCopyTexture(AGX_D3D12_OWNER *Owner, const AGX_D3D12_TEXTURE_COPY *Copy);
/* C10 (D3D12-C10-uav-clears.md): ClearUnorderedAccessView{Uint,Float} of
 * one view (the D3D11 path's clear_typed/pack_uint_bits): untyped views
 * take Values[0] in every 32-bit word (Uint only); typed views one pixel of
 * their format (Uint: the low bits of each value per channel; Float: the
 * values converted) over the view, or over each rect (textures: x/y
 * rects; buffers: [left, right) elements). */
HRESULT AgxD3d12OwnerClearUav(AGX_D3D12_OWNER *Owner, const AGX_D3D12_UAV *Uav, const void *Values, int Float,
                              const RECT *Rects, unsigned RectCount);
/* C11 (D3D12-C11-queries.md): a query heap of Gallium queries created on
 * first use -- HeapType 0 occlusion (OCCLUSION_COUNTER, and
 * OCCLUSION_PREDICATE for binary occlusion), 1 timestamp (TIMESTAMP),
 * 2 pipeline statistics (11 PIPELINE_STATISTICS_SINGLE counters in the
 * D3D12_QUERY_DATA_PIPELINE_STATISTICS order). QueryType is the D3D12 DDI
 * query type; Resolve writes UINT64 results (statistics: 11 UINT64) for
 * [Start, Start + Count) at Offset, zero for queries never used. */
typedef struct AGX_D3D12_QUERY_HEAP AGX_D3D12_QUERY_HEAP;
HRESULT AgxD3d12OwnerCreateQueryHeap(AGX_D3D12_OWNER *Owner, unsigned HeapType, unsigned Count,
                                     AGX_D3D12_QUERY_HEAP **Heap);
void AgxD3d12OwnerDestroyQueryHeap(AGX_D3D12_OWNER *Owner, AGX_D3D12_QUERY_HEAP *Heap);
HRESULT AgxD3d12OwnerQuery(AGX_D3D12_OWNER *Owner, AGX_D3D12_QUERY_HEAP *Heap, unsigned QueryType, unsigned Index,
                           int Begin);
HRESULT AgxD3d12OwnerResolveQueries(AGX_D3D12_OWNER *Owner, AGX_D3D12_QUERY_HEAP *Heap, unsigned QueryType,
                                    unsigned Start, unsigned Count, const AGX_D3D12_BUFFER *Dst,
                                    unsigned long long Offset);
/* C7b (D3D12-C7-present.md): a 2D render target DXGI can share -- a kernel
 * allocation of the runtime resource (the D3D10/11 presentation-resource
 * path: AdmissionUmdCreateResource with the runtime handle, BGRA8/RGBA8,
 * one mip/layer/sample) imported into the owner as a linear texture.
 * *Allocation receives the D3DKMT allocation, *Presentation the record the
 * destroy call takes. */
HRESULT AgxD3d12OwnerCreatePresentationTexture(AGX_D3D12_OWNER *Owner, DXGI_FORMAT Format, unsigned Width,
                                               unsigned Height, int ShaderResource, HANDLE RuntimeResource,
                                               AGX_D3D12_BUFFER *Texture, void **Presentation, UINT *Allocation);
void AgxD3d12OwnerDestroyPresentationTexture(AGX_D3D12_OWNER *Owner, void *Presentation);
/* C7c: makes a presentation texture's rendering visible to the kernel
 * present (agx_d3d10_windows.cpp AgxD3d10WindowsPresentationSubmitSurfaces:
 * flush for present, publish the allocation's direct map) and returns its
 * D3DKMT allocation in *Allocation. */
HRESULT AgxD3d12OwnerPreparePresent(AGX_D3D12_OWNER *Owner, void *Presentation, UINT *Allocation);
/* C8b: a GPU copy between texture regions of the same format class
 * (resource_copy_region); Layers counts array layers from each layer start. */
HRESULT AgxD3d12OwnerCopyTextureRegion(AGX_D3D12_OWNER *Owner, const AGX_D3D12_BUFFER *Dst, unsigned DstLevel,
                                       unsigned DstLayer, unsigned DstX, unsigned DstY, const AGX_D3D12_BUFFER *Src,
                                       unsigned SrcLevel, unsigned SrcLayer, unsigned SrcX, unsigned SrcY,
                                       unsigned Width, unsigned Height, unsigned Layers);
/* C6f: clears depth (Flags bit 0) and/or stencil (bit 1) of a depth view */
HRESULT AgxD3d12OwnerClearDepthStencil(AGX_D3D12_OWNER *Owner, const AGX_D3D12_BUFFER *Texture, DXGI_FORMAT ViewFormat,
                                       unsigned Level, unsigned FirstLayer, unsigned Layers, unsigned Flags,
                                       float Depth, unsigned Stencil, const RECT *Rects, unsigned RectCount);
/* C6b (D3D12-C6-graphics.md): a graphics pipeline. State fields carry the
 * D3D10/11/12 DDI numeric values (D3D12DDI_BLEND_* == D3D10_DDI_BLEND_*,
 * comparison/stencil/cull/fill likewise); Vs/Ps are tokenized programs. */
#define AGX_D3D12_MAX_ELEMENTS 16
/* C39: a pipeline's stream-output declaration as the DDI passes it
 * (D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY: Register 0xffffffff is a
 * hole; Mask is xyzw in its 4 LSBs). Rasterized 0xffffffff: no stream is
 * rasterized. Strides in bytes per output slot (NumStrides of them). */
#define AGX_D3D12_SO_ENTRIES 64
typedef struct AGX_D3D12_SO_DECL {
  unsigned Entries, Rasterized, NumStrides, Strides[4];
  struct {
    unsigned Stream, Slot, Register, Mask;
  } Entry[AGX_D3D12_SO_ENTRIES];
} AGX_D3D12_SO_DECL;
typedef struct {
  const UINT *Vs;
  const UINT *Ps; /* NULL = no pixel shader */
  const UINT *Gs; /* C27: NULL = no geometry shader */
  /* C37: hull and domain shaders (both or neither); the hull shader's
   * output-signature registers (and which hold SV_Position) that a
   * pass-through control point phase copies */
  const UINT *Hs, *Ds;
  unsigned HsPassthroughRegisters, HsPassthroughPositions;
  /* C39: stream output of the geometry shader (or, without one, of the
   * vertex shader); NULL = none */
  const struct AGX_D3D12_SO_DECL *So;
  int AlphaToCoverage, IndependentBlend;
  struct {
    int BlendEnable;
    unsigned Src, Dst, Op, SrcAlpha, DstAlpha, OpAlpha, WriteMask;
  } Rt[8];
  unsigned SampleMask;
  unsigned SampleCount; /* C32: the PSO's SampleDesc.Count */
  unsigned FillMode, CullMode;
  int FrontCounterClockwise, DepthBias, DepthClipEnable;
  float DepthBiasClamp, SlopeScaledDepthBias;
  int DepthEnable, DepthWrite, StencilEnable;
  unsigned DepthFunc, StencilReadMask, StencilWriteMask;
  struct {
    unsigned Fail, DepthFail, Pass, Func;
  } Front, Back;
  unsigned NumElements;
  struct {
    unsigned Slot, Offset, Register, PerInstance, StepRate;
    DXGI_FORMAT Format;
  } Element[AGX_D3D12_MAX_ELEMENTS];
} AGX_D3D12_GRAPHICS_DESC;
typedef struct {
  void *Vs, *Fs, *Blend, *DepthStencil, *Rasterizer;
  void *Gs; /* C27 */
  void *Hs, *Ds; /* C37: TCS and TES */
  int Xfb; /* C39: the last vertex stage captures stream output */
  unsigned SampleMask; /* C32 */
  unsigned NumElements;
  struct {
    unsigned Slot, Offset, Register, Divisor, Format; /* pipe format */
  } Element[AGX_D3D12_MAX_ELEMENTS];
  /* the vertex-elements CSO of the last strides drawn (D3D12 strides come
   * from IASetVertexBuffers) */
  void *Velems;
  unsigned VelemsStrides[16];
  struct dxbc_binding_map VsBindings, PsBindings, GsBindings, HsBindings, DsBindings;
  uint8_t VsSrvDimensions[DXBC_RESOURCE_SLOTS], PsSrvDimensions[DXBC_RESOURCE_SLOTS], GsSrvDimensions[DXBC_RESOURCE_SLOTS],
          HsSrvDimensions[DXBC_RESOURCE_SLOTS], DsSrvDimensions[DXBC_RESOURCE_SLOTS];
  uint8_t VsUavDimensions[DXBC_UAV_DIMENSION_SLOTS], PsUavDimensions[DXBC_UAV_DIMENSION_SLOTS],
          GsUavDimensions[DXBC_UAV_DIMENSION_SLOTS], HsUavDimensions[DXBC_UAV_DIMENSION_SLOTS],
          DsUavDimensions[DXBC_UAV_DIMENSION_SLOTS];
} AGX_D3D12_GRAPHICS;
HRESULT AgxD3d12OwnerCreateGraphics(AGX_D3D12_OWNER *Owner, const AGX_D3D12_GRAPHICS_DESC *Desc,
                                    AGX_D3D12_GRAPHICS *Graphics);
void AgxD3d12OwnerDestroyGraphics(AGX_D3D12_OWNER *Owner, AGX_D3D12_GRAPHICS *Graphics);
#define AGX_D3D12_VB_SLOTS 16
typedef struct {
  AGX_D3D12_GRAPHICS *Pipeline;
  unsigned NumRtvs;
  struct {
    const AGX_D3D12_BUFFER *Texture; /* NULL = unbound slot */
    DXGI_FORMAT Format;
    unsigned Level, FirstLayer, Layers;
  } Rtv[8];
  struct {
    const AGX_D3D12_BUFFER *Texture; /* C6f: NULL = no depth-stencil view */
    DXGI_FORMAT Format;
    unsigned Level, FirstLayer, Layers;
  } Dsv;
  float Viewport[6]; /* TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth */
  RECT Scissor;
  unsigned Topology; /* D3D_PRIMITIVE_TOPOLOGY (C37: 33..64 = 1..32 control point patch lists) */
  struct {
    const AGX_D3D12_BUFFER *Buffer; /* NULL = unbound */
    unsigned long long Offset;
    unsigned Stride;
  } Vb[AGX_D3D12_VB_SLOTS];
  float BlendFactor[4];
  unsigned StencilRef;
  unsigned VertexCount, InstanceCount, StartVertex, StartInstance;
  /* C6d: an indexed draw (IndexSize 2 or 4): VertexCount/StartVertex count
   * indices; RestartIndex 0 = no strip cut */
  const AGX_D3D12_BUFFER *IndexBuffer;
  unsigned long long IndexOffset;
  unsigned IndexSize;
  int BaseVertex;
  unsigned RestartIndex;
  /* C6c: per-stage constant buffers and buffer SRVs: arrays of
   * AGX_D3D12_CBV_SLOTS, _SRV_SLOTS, _SAMPLER_SLOTS (C6e) and _UAV_SLOTS
   * (C13: graphics UAVs, u# in every stage) records the caller keeps alive
   * for the call (EXP1838: referenced, not copied into the draw) */
  struct {
    const AGX_D3D12_CBV *Cbvs;
    const AGX_D3D12_SRV *Srvs;
    const AGX_D3D12_SAMPLER *Samplers;
    const AGX_D3D12_UAV *Uavs;
  } Vs, Ps, Gs, Hs, Ds; /* C27: Gs only when the pipeline has a geometry shader; C37: Hs/Ds likewise */
  /* C14: indirect draws (Indirect.Buffer NULL = direct); an indexed indirect
   * draw needs IndexOffset 0 (Gallium takes the start index from the
   * arguments) */
  AGX_D3D12_INDIRECT Indirect;
  /* C39: stream-output targets (Buffer NULL = unbound slot); the filled
   * size (bytes written from Offset) lives at Filled + FilledOffset */
  struct {
    const AGX_D3D12_BUFFER *Buffer, *Filled;
    unsigned long long Offset, Size, FilledOffset;
  } So[4];
} AGX_D3D12_DRAW;
/* Records one draw; bindings that reference heaps are dropped by the next
 * AgxD3d12OwnerSubmitAndWait. */
HRESULT AgxD3d12OwnerDraw(AGX_D3D12_OWNER *Owner, const AGX_D3D12_DRAW *Draw);
/* Submits the recorded work and waits for it (C4b orders it before the
 * runtime's later queue signal by completing it first). */
HRESULT AgxD3d12OwnerSubmitAndWait(AGX_D3D12_OWNER *Owner);

#ifdef __cplusplus
}
#endif
#endif
