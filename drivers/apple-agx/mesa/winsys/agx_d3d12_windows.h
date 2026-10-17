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
  /* C6e: a 2D (array) texture view instead (Buffer = the texture) */
  int Texture;
  DXGI_FORMAT Format;
  unsigned FirstLevel, Levels, FirstLayer, Layers;
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
HRESULT AgxD3d12OwnerDispatch(AGX_D3D12_OWNER *Owner, const AGX_D3D12_COMPUTE *Compute,
                              const AGX_D3D12_UAV Uavs[AGX_D3D12_UAV_SLOTS],
                              const AGX_D3D12_CBV Cbvs[AGX_D3D12_CBV_SLOTS],
                              const AGX_D3D12_SRV Srvs[AGX_D3D12_SRV_SLOTS], unsigned X, unsigned Y, unsigned Z);
/* C6a (investigation/analysis/D3D12-C6-graphics.md): textures. Bytes of
 * one texel block of a DXGI format the owner can create (uncompressed,
 * 1x1 blocks), 0 otherwise. */
unsigned AgxD3d12FormatBlockBytes(DXGI_FORMAT Format);
#define AGX_D3D12_BIND_RENDER_TARGET 0x1u
#define AGX_D3D12_BIND_SAMPLER 0x2u
#define AGX_D3D12_BIND_DEPTH_STENCIL 0x4u /* C6f: also translates typeless formats as depth */
/* A 2D texture (array when Layers > 1) of the owner's screen; Texture->Cpu
 * is NULL, Bytes the caller's allocation size. */
HRESULT AgxD3d12OwnerCreateTexture2D(AGX_D3D12_OWNER *Owner, DXGI_FORMAT Format, unsigned Width, unsigned Height,
                                     unsigned Layers, unsigned Levels, unsigned Bind, unsigned long long Bytes,
                                     AGX_D3D12_BUFFER *Texture);
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
typedef struct {
  const UINT *Vs;
  const UINT *Ps; /* NULL = no pixel shader */
  int AlphaToCoverage, IndependentBlend;
  struct {
    int BlendEnable;
    unsigned Src, Dst, Op, SrcAlpha, DstAlpha, OpAlpha, WriteMask;
  } Rt[8];
  unsigned SampleMask;
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
  unsigned NumElements;
  struct {
    unsigned Slot, Offset, Register, Divisor, Format; /* pipe format */
  } Element[AGX_D3D12_MAX_ELEMENTS];
  /* the vertex-elements CSO of the last strides drawn (D3D12 strides come
   * from IASetVertexBuffers) */
  void *Velems;
  unsigned VelemsStrides[16];
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
  unsigned Topology; /* D3D_PRIMITIVE_TOPOLOGY */
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
  /* C6c: per-stage constant buffers and buffer SRVs */
  struct {
    AGX_D3D12_CBV Cbvs[AGX_D3D12_CBV_SLOTS];
    AGX_D3D12_SRV Srvs[AGX_D3D12_SRV_SLOTS];
    AGX_D3D12_SAMPLER Samplers[AGX_D3D12_SAMPLER_SLOTS]; /* C6e */
  } Vs, Ps;
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
