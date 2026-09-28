/* WDK26100 field subset: descriptor5 deliberately has no CommitLimit. */
#define _Out_
#define PAGE_SIZE 4096
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xc0000023)
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define ADMISSION_GPUVA_G1B_PAGE_PROFILE 16
#define ADMISSION_G1B_USE64K 0
#define ADMISSION_G1B_SLAB_SIZE 16384
#define DXGK_PAGESIZE_4KB 4096
#define DXGK_SEGMENTTYPE_SYSMEM 1
#define DXGK_SEGMENTTYPE_LOCAL 2
typedef unsigned UINT;
typedef struct { unsigned Aperture,Use64KBPages,CpuVisible,PopulatedFromSystemMemory,PopulatedByReservedDDRByFirmware,SupportsCpuHostAperture; } DXGK_SEGMENTFLAGS;
typedef struct { DXGK_SEGMENTFLAGS Flags; PHYSICAL_ADDRESS BaseAddress; SIZE_T Size,CommitLimit; PHYSICAL_ADDRESS CpuTranslatedAddress; } DXGK_SEGMENTDESCRIPTOR4;
typedef struct { unsigned SegmentType; DXGK_SEGMENTFLAGS Flags; PHYSICAL_ADDRESS BaseAddress; ULONGLONG Size; unsigned SlabSize; PHYSICAL_ADDRESS CpuTranslatedAddress; } DXGK_SEGMENTDESCRIPTOR5;
typedef struct { UINT PhysicalAdapterIndex; } DXGK_QUERYSEGMENTIN4;
typedef struct { UINT PhysicalAdapterIndex,Reserved; } DXGK_QUERYSEGMENTIN5;
typedef struct { UINT NbSegment; DXGK_SEGMENTDESCRIPTOR4 *pSegmentDescriptor; SIZE_T SegmentDescriptorStride; UINT PagingBufferSegmentId,PagingBufferSize,PagingBufferPrivateDataSize; } DXGK_QUERYSEGMENTOUT4;
typedef struct { DXGK_SEGMENTDESCRIPTOR5 *SegmentDescriptors; unsigned Reserved[4]; } DXGK_QUERYSEGMENTOUT5;
typedef struct { void *pInputData; SIZE_T InputDataSize; void *pOutputData; SIZE_T OutputDataSize; } DXGKARG_QUERYADAPTERINFO;
/* PRODUCTION_SEGMENT_QUERIES */
static void replay_segment_queries(ADMISSION_CONTEXT *context) {
 DXGK_SEGMENTDESCRIPTOR4 d4[2]; DXGK_SEGMENTDESCRIPTOR5 d5[2];
 DXGK_QUERYSEGMENTIN4 i4={0}; DXGK_QUERYSEGMENTIN5 i5={0};
 DXGK_QUERYSEGMENTOUT4 o4={.NbSegment=2,.pSegmentDescriptor=d4,.SegmentDescriptorStride=sizeof(d4[0])};
 DXGK_QUERYSEGMENTOUT5 o5={.SegmentDescriptors=d5};
 DXGKARG_QUERYADAPTERINFO q4={&i4,sizeof(i4),&o4,sizeof(o4)},q5={&i5,sizeof(i5),&o5,sizeof(o5)};
 context->Memory.PagingReady=APPLE_AGX_TRUE;
 assert(AdmissionDdiQuerySegment4(context,&q4)==STATUS_SUCCESS);
 assert(AdmissionDdiQuerySegment5(context,&q5)==STATUS_SUCCESS);
 assert(d4[1].Size==0x3e800000 && d4[1].CommitLimit==0x3e800000 && d5[1].Size==0x3e800000);
 assert(d4[1].CpuTranslatedAddress.QuadPart==(LONGLONG)context->LocalReserveReceipt.GuestIpa);
 assert(d5[1].CpuTranslatedAddress.QuadPart==d4[1].CpuTranslatedAddress.QuadPart);
 assert(d4[1].Flags.CpuVisible && d5[1].Flags.CpuVisible);
 assert(!d4[1].Flags.SupportsCpuHostAperture && !d5[1].Flags.SupportsCpuHostAperture);
 assert(!d4[1].Flags.PopulatedFromSystemMemory && !d5[1].Flags.PopulatedFromSystemMemory);
 assert(d4[1].Flags.PopulatedByReservedDDRByFirmware && d5[1].Flags.PopulatedByReservedDDRByFirmware);
 assert(d4[1].BaseAddress.QuadPart==0x1500000000ULL && d5[1].BaseAddress.QuadPart==0x1500000000ULL);
}
