#ifndef APPLE_AGX_RENDER_ALLOCATION_H
#define APPLE_AGX_RENDER_ALLOCATION_H

#define ADMISSION_ALLOCATION_MAGIC 0x414d4152u /* "RAMA" */
#define ADMISSION_ALLOCATION_VERSION 1u
#define ADMISSION_ALLOCATION_OBJECT_MAGIC 0x4f4d4152u /* "RAMO" */
#define ADMISSION_ALLOCATION_ALIGNMENT 0x10000ULL

/* PITCH-1 (investigation/analysis/PITCH-1-aligned-display-pitch.md): the
 * row pitch of a linear allocation.  The display class -- a GPU-only 32-bpp
 * GDI surface texture, the only class the KMD marks ScanoutCapable -- gets
 * a pitch that is a multiple of 64 bytes, the stride rule of the scanout
 * broker (m1n1 hv_agx_scanout_broker.c layer_validate), of DCP (Asahi
 * plane.c: linear pitches 64-byte aligned) and of the Check3 table
 * (mpo_contract.h ADMISSION_MPO_STRIDE_ALIGNMENT).  Every other allocation
 * keeps the 16-byte pitch (Asahi AIL's minimum linear stride).  No other
 * site computes a pitch: create, open, the Asahi import, the plane input
 * and the broker record all take Description->Pitch. */
#define ADMISSION_ALLOCATION_PITCH_ALIGNMENT 16u
#define ADMISSION_ALLOCATION_DISPLAY_PITCH_ALIGNMENT 64u
/* D3DKMDT_GDISURFACE_TEXTURE (d3dkmdt.h); allocation_windows.c C_ASSERTs it. */
#define ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE 1u

typedef struct _ADMISSION_ALLOCATION_DESCRIPTION {
  unsigned int Magic;
  unsigned int Version;
  unsigned int Type;
  unsigned int Format;
  unsigned int Width;
  unsigned int Height;
  unsigned int Pitch;
  unsigned int BytesPerPixel;
  unsigned long long Size;
  unsigned int CpuVisible;
  unsigned int Reserved;
} ADMISSION_ALLOCATION_DESCRIPTION;

/* Per-resource data survives a shared OpenResource independently of each
 * allocation's placement description. Never infer displayability from BGRA.
 * Version 3 (EXP1327): Format is the DXGI_FORMAT the resource was created
 * with, since D3D10DDIARG_OPENRESOURCE carries none and several DXGI formats
 * share one storage format (A8_UNORM is stored as RGBA8); the KMD treats it
 * as opaque. */
#define ADMISSION_PRESENT_RESOURCE_MAGIC 0x52505241u
/* Version 4 marks converted presentation storage. The D3D11 consumer still
 * accepts v3; D3D12 open requires v4 because v3 cannot distinguish a resolved
 * display allocation from the original multisampled or converted texture. */
#define ADMISSION_PRESENT_RESOURCE_VERSION 4u
#define ADMISSION_PRESENT_RESOURCE_WRITTEN_PRIMARY 1u
/* Auxiliary presentation storage is not the original D3D12 resource.
 * D3D11/DWM can still consume it as the described linear display surface. */
#define ADMISSION_PRESENT_RESOURCE_CONVERTED 2u
typedef struct _ADMISSION_PRESENT_RESOURCE_DATA {
  unsigned int Magic, Version, Bytes, Flags, Format;
  /* DXGI_DDI_PRIMARY_DESC.ModeDesc.RefreshRate of a primary; 0/0 otherwise. */
  unsigned int RefreshNumerator, RefreshDenominator;
} ADMISSION_PRESENT_RESOURCE_DATA;
int AdmissionPresentResourceDataValid(
    const ADMISSION_PRESENT_RESOURCE_DATA *Data);
/* The DxgkDdiDescribeAllocation RefreshRate: the created-with rate, or 0/1. */
void AdmissionPresentResourceRefreshRate(
    const ADMISSION_PRESENT_RESOURCE_DATA *Data, unsigned int *Numerator,
    unsigned int *Denominator);

typedef struct _ADMISSION_ALLOCATION_OBJECT {
  unsigned int Magic;
  unsigned int OpenCount;
  ADMISSION_ALLOCATION_DESCRIPTION Description;
} ADMISSION_ALLOCATION_OBJECT;

/* 1 when an allocation of this type can become a scan-out plane class. */
int AdmissionAllocationDisplayClass(unsigned int Type,
                                    unsigned int BytesPerPixel,
                                    unsigned int CpuVisible);
/* The row-pitch alignment of that allocation class (64 or 16). */
unsigned int AdmissionAllocationPitchAlignment(unsigned int Type,
                                               unsigned int BytesPerPixel,
                                               unsigned int CpuVisible);
/* The smallest pitch of the class alignment holding Width * BytesPerPixel;
 * 0 on a zero width or byte count, or a pitch beyond 32 bits. */
int AdmissionAllocationPitch(unsigned int Width, unsigned int BytesPerPixel,
                             unsigned int Type, unsigned int CpuVisible,
                             unsigned int *Pitch);
int AdmissionAllocationDescribe(unsigned int Width, unsigned int Height,
                                unsigned int BytesPerPixel,
                                unsigned int Type, unsigned int Format,
                                unsigned int CpuVisible,
                                ADMISSION_ALLOCATION_DESCRIPTION *Description);
int AdmissionAllocationDescriptionValid(
    const ADMISSION_ALLOCATION_DESCRIPTION *Description);
int AdmissionAllocationContainsView(
    const ADMISSION_ALLOCATION_DESCRIPTION *Description,
    unsigned int Width, unsigned int Height, unsigned int Pitch,
    unsigned long long ReferencedBytes);
int AdmissionAllocationAlign64K(unsigned long long Size,
                                unsigned long long *AlignedSize);
unsigned long long AdmissionAllocationPitchAlignedSize(
    unsigned long long Size, unsigned int PitchAlignedSegmentSupported);
int AdmissionAllocationCreate(
    const ADMISSION_ALLOCATION_DESCRIPTION *Description,
    ADMISSION_ALLOCATION_OBJECT *Allocation);
int AdmissionAllocationOpen(ADMISSION_ALLOCATION_OBJECT *Allocation);
int AdmissionAllocationClose(ADMISSION_ALLOCATION_OBJECT *Allocation);
int AdmissionAllocationDestroy(ADMISSION_ALLOCATION_OBJECT *Allocation);

#endif /* APPLE_AGX_RENDER_ALLOCATION_H */
