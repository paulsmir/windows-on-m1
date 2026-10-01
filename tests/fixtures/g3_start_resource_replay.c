#include <stdint.h>
#include <assert.h>
#include "render_memory.h"
#include "apple_agx_fixed_panel.h"
#include "hv_agx_scanout_broker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "j313_agx_g2.generated.h"
#include "apple_agx_local_reserve_abi.h"
#include "apple_agx_residency.h"
#include "apple_agx_uat_publication.h"

typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef uint64_t ULONG64;
typedef int64_t LONGLONG;
typedef uint8_t UCHAR, *PUCHAR;
typedef uint16_t USHORT;
typedef size_t SIZE_T;
typedef void *PVOID;
typedef int BOOLEAN;
typedef int32_t NTSTATUS;
typedef struct { int64_t QuadPart; } PHYSICAL_ADDRESS;
enum { CmResourceTypeMemory = 3, CmResourceTypeInterrupt = 2,
       CmResourceTypeDevicePrivate = 129, CmResourceShareDeviceExclusive = 1,
       CM_RESOURCE_INTERRUPT_LATCHED = 1 };
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define STATUS_DEVICE_CONFIGURATION_ERROR ((NTSTATUS)0xc0000182)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xc0000184)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xc000009a)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)0xc0000141)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xc0000095)
#define STATUS_PENDING ((NTSTATUS)0x00000103)
#define MAXULONGLONG UINT64_MAX
#define _In_
#define _Inout_
#define _Use_decl_annotations_
#define VOID void
/* PRODUCTION_MEMORY_CONSTANTS */
#define NT_SUCCESS(status) ((status) >= 0)
#define TRUE 1
#define FALSE 0
#define POOL_FLAG_NON_PAGED 1
#define ADMISSION_PHYSICAL_TAG 0
#define PAGE_READWRITE 1
#define PAGE_NOCACHE 2
#define PAGE_WRITECOMBINE 4
#define RtlZeroMemory(pointer, bytes) memset((pointer), 0, (bytes))
#define RTL_NUMBER_OF(array) (sizeof(array) / sizeof((array)[0]))
#define READ_REGISTER_ULONG(address) (*(address))
#define READ_REGISTER_ULONG64(address) (*(address))
#define InterlockedIncrement(address) (++*(address))
#define InterlockedDecrement(address) (--*(address))
#define ExAcquireFastMutex(address) ((void)(address))
#define ExReleaseFastMutex(address) ((void)(address))
#define RtlSecureZeroMemory(pointer, bytes) memset((pointer), 0, (bytes))

typedef struct { void *hPhysicalMemoryObject, *pBaseAddress; SIZE_T Size; }
    DXGKARGCB_UNMAP_PHYSICAL_MEMORY;
typedef struct { void *hAdapterMemoryObject, *pAdl; } DXGKARGCB_FREE_ADL;
typedef struct { void *hPhysicalMemoryObject, *hAdapterMemoryObject; }
    DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT;
typedef struct {
  void *DeviceHandle;
  NTSTATUS (*DxgkCbSynchronizeExecution)(void *, BOOLEAN (*)(void *), void *, ULONG, BOOLEAN *);
  void *DxgkCbMapMemory, *DxgkCbUnmapMemory;
  void (*DxgkCbUnmapPhysicalMemory)(DXGKARGCB_UNMAP_PHYSICAL_MEMORY *);
  void (*DxgkCbFreeAdl)(DXGKARGCB_FREE_ADL *);
  void (*DxgkCbDestroyPhysicalMemoryObject)(
      DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT *);
} DXGKRNL_INTERFACE, *PDXGKRNL_INTERFACE;

typedef struct {
  UCHAR Type, ShareDisposition;
  USHORT Flags;
  union {
    struct { PHYSICAL_ADDRESS Start; ULONG Length; } Memory;
    struct { ULONG Level, Vector; ULONGLONG Affinity; } Interrupt;
    struct { ULONG Data[3]; } DevicePrivate;
  } u;
} CM_PARTIAL_RESOURCE_DESCRIPTOR, *PCM_PARTIAL_RESOURCE_DESCRIPTOR;
typedef struct {
  ULONG Count;
  CM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptors[16];
} CM_PARTIAL_RESOURCE_LIST;
typedef struct { CM_PARTIAL_RESOURCE_LIST PartialResourceList; }
    CM_FULL_RESOURCE_DESCRIPTOR, *PCM_FULL_RESOURCE_DESCRIPTOR;
typedef struct { ULONG Count; CM_FULL_RESOURCE_DESCRIPTOR List[1]; }
    CM_RESOURCE_LIST, *PCM_RESOURCE_LIST;
typedef struct { PCM_RESOURCE_LIST TranslatedResourceList; } DXGK_DEVICE_INFO;
typedef struct {
  ULONG Type, ShareDisposition, Flags, Length;
  ULONGLONG Address;
  ULONG Vector, Reserved;
} ADMISSION_RESOURCE_ENTRY;
typedef struct {
  ULONG Version, Bytes, FullCount, PartialCount, CapturedCount, Truncated;
  ADMISSION_RESOURCE_ENTRY Entries[32];
} ADMISSION_RESOURCE_LIST_RECEIPT;
typedef struct {
  PDXGKRNL_INTERFACE Interface;
  void *PhysicalMemoryObject, *AdapterMemoryObject;
  struct { struct { unsigned Contiguous; } Flags; } *Adl;
  PVOID MappedBase;
  SIZE_T MappedSize;
  PUCHAR CpuBase;
  SIZE_T Size;
  ULONGLONG GuestIpaBase;
  ULONGLONG HostPhysicalBase;
  BOOLEAN BorrowedFirmwareReserve;
} ADMISSION_PHYSICAL_ALLOCATION;
typedef struct {
  PDXGKRNL_INTERFACE Interface;
  ULONG LastAllocateStep;
  ULONGLONG LastAllocateBytes;
  NTSTATUS LastAllocateStatus;
  int AllocationCount;
  BOOLEAN Initialized;
  int Lock;
  ADMISSION_PHYSICAL_ALLOCATION *Scratch;
  void *Request; ULONGLONG RequestIpa;
} ADMISSION_PHYSICAL_OWNER;
typedef struct {
  DXGK_DEVICE_INFO DeviceInformation;
  APPLE_AGX_LOCAL_RESERVE_RECEIPT LocalReserveReceipt;
  void *MemoryRuntime;
  ADMISSION_MEMORY_CONTRACT Memory;
  void *ScanoutRuntime, *BrokerBase;
  BOOLEAN DisplayActive, SourceVisible;
  DXGKRNL_INTERFACE Interface;
  ULONG Win32BootGeneration;
  BOOLEAN InterfaceValid; void *PhysicalDeviceObject;
  int MemoryStartStage,MemoryStartStatus;
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY *ApertureEntries;
} ADMISSION_CONTEXT;
/* PRODUCTION_VIEW_TYPE */
typedef struct {
  PVOID CpuAddress;
  ULONGLONG GuestIpaAddress, HostPhysicalAddress, GpuVirtualAddress, Bytes;
} ADMISSION_BACKEND_MEMORY_VIEW;
typedef struct { PDXGKRNL_INTERFACE Interface; } ADMISSION_UAT_WINDOWS_IO;
typedef struct {
  ADMISSION_PHYSICAL_OWNER PhysicalOwner;
  APPLE_AGX_MEMORY_IO MemoryIo;
  APPLE_AGX_RESIDENCY_CONTEXT Residency;
  APPLE_AGX_MEMORY_OBJECT *UatObjects;
  APPLE_AGX_UAT_PAGE *UatPages;
  APPLE_AGX_UAT_MAPPING *UatMappings;
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY *ApertureEntries;
  ADMISSION_UAT_WINDOWS_IO Publication;
  APPLE_AGX_UAT_PUBLICATION_IO PublicationIo;
  APPLE_AGX_UAT_PUBLICATION_STATE Published;
  int PagingLock;
  APPLE_AGX_MEMORY_OBJECT LocalObject;
  BOOLEAN PhysicalReady, LocalReady, ResidencyReady, MappingReady, PublicationReady;
} ADMISSION_MEMORY_RUNTIME;

/* PRODUCTION_MEMORY_GET_RUNTIME */
/* PRODUCTION_LOCAL_VIEW */
/* PRODUCTION_SCANOUT_VIEW */
/* PRODUCTION_BACKEND_VIEW */

static UCHAR broker_registers[J313_AGX_G2_POWER_BROKER_SIZE];
static UCHAR *local_mapping;
static unsigned local_unmaps;
static bool fail_local_map,fill_borrowed;
static unsigned char publication_registers[0x4000];
static unsigned dxgk_unmaps, dxgk_adl_frees, dxgk_destroys;

static void dxgk_unmap(DXGKARGCB_UNMAP_PHYSICAL_MEMORY *args) {
  if(fill_borrowed) for(unsigned i=0;i<16;++i) assert(publication_registers[63*16+i]==0);
  ++dxgk_unmaps;
  free(args->pBaseAddress);
}
static void dxgk_free_adl(DXGKARGCB_FREE_ADL *args) {
  ++dxgk_adl_frees;
  free(args->pAdl);
}
static void dxgk_destroy(DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT *args) {
  (void)args;
  ++dxgk_destroys;
}

static PVOID MmMapIoSpaceEx(PHYSICAL_ADDRESS address, SIZE_T bytes, ULONG protection) {
  if ((ULONGLONG)address.QuadPart == J313_AGX_G2_POWER_BROKER_BASE &&
      bytes == J313_AGX_G2_POWER_BROKER_SIZE &&
      protection == (PAGE_READWRITE | PAGE_NOCACHE))
    return broker_registers;
  if ((ULONGLONG)address.QuadPart == UINT64_C(0x8e0000000) &&
      bytes == APPLE_AGX_LOCAL_RESERVE_BYTES &&
      protection == (PAGE_READWRITE | PAGE_WRITECOMBINE)) {
    if (fail_local_map) return NULL;
    local_mapping = calloc(1, bytes);
    if(fill_borrowed && local_mapping) memset(local_mapping,0xa5,bytes);
    return local_mapping;
  }
  return NULL;
}
static void MmUnmapIoSpace(PVOID address, SIZE_T bytes) {
  (void)bytes;
  if (address == local_mapping) {
    if(fill_borrowed) for(unsigned i=0;i<16;++i) assert(publication_registers[63*16+i]==0);
    ++local_unmaps;
    free(local_mapping);
    local_mapping = NULL;
  }
}
static PVOID ExAllocatePool2(ULONG flags, SIZE_T bytes, ULONG tag) {
  (void)flags; (void)tag;
  return calloc(1, bytes);
}
static void ExFreePoolWithTag(PVOID pointer, ULONG tag) {
  (void)tag;
  free(pointer);
}
/* PRODUCTION_RELEASE_RAW */
/* PRODUCTION_PHYSICAL_FREE */
#define PAGE_SIZE 4096
#define MAXULONG UINT32_MAX
#define MAXSIZE_T SIZE_MAX
#define ADMISSION_HVC_PHYSICAL_LIMIT (1ULL << 40)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xc0000483)
typedef unsigned UINT;
typedef struct { void *Context; void (*Invoke)(void); } ADMISSION_HVC_IO;
static void AdmissionPhysicalInvokeHvc(void) {}
static UINT translated_pages;
static UINT broken_translation=UINT32_MAX;
static bool AdmissionHvcTranslatePages(ADMISSION_HVC_IO *io, void *request,
    ULONGLONG request_ipa, ULONGLONG *ipas, UINT count, ULONGLONG *pas) {
  (void)io; (void)request; (void)request_ipa;
  translated_pages=count;
  for(UINT i=0;i<count;++i) pas[i]=ipas[i]+(i==broken_translation ? 0x1000 : 0);
  return true;
}
/* PRODUCTION_PHYSICAL_TRANSLATE */

/* PRODUCTION_BORROW */

/* PRODUCTION_G3_GATE */

/* PRODUCTION_VALIDATOR */

/* PRODUCTION_RESOURCE_CAPTURE */

static void memory(CM_PARTIAL_RESOURCE_DESCRIPTOR *entry,
                   ULONGLONG base, ULONG size) {
  memset(entry, 0, sizeof(*entry));
  entry->Type = CmResourceTypeMemory;
  entry->ShareDisposition = CmResourceShareDeviceExclusive;
  entry->u.Memory.Start.QuadPart = (int64_t)base;
  entry->u.Memory.Length = size;
}

typedef struct {
  unsigned int Count;
  void *Slots[64];
  unsigned char GpuRegisters[J313_AGX_G2_GPU_SIZE];
} START_MEMORY_MODEL;

static unsigned char table_allocate(void *opaque, unsigned long long bytes,
                                    void **cpu, unsigned long long *pa,
                                    void **handle) {
  START_MEMORY_MODEL *model = opaque;
  void *storage;
  if (bytes != 0x8000ULL || model->Count >= 64u) return 0;
  storage = aligned_alloc(0x4000, (size_t)bytes);
  if (storage == NULL) return 0;
  memset(storage, 0, (size_t)bytes);
  *cpu = storage;
  *pa = UINT64_C(0x700000000) + (uint64_t)model->Count * 0x10000u;
  *handle = storage;
  model->Slots[model->Count++] = storage;
  return 1;
}

static unsigned char table_free(void *opaque, void *handle) {
  START_MEMORY_MODEL *model = opaque;
  unsigned int index;
  for (index = 0; index < model->Count; ++index) {
    if (model->Slots[index] == handle) {
      model->Slots[index] = NULL;
      free(handle);
      return 1;
    }
  }
  return 0;
}

static unsigned char publish_map(void *opaque, unsigned long long address,
                                 unsigned int length,
                                 volatile unsigned char **mapped) {
  START_MEMORY_MODEL *model = opaque;
  if (address != J313_AGX_G2_GPU_BASE || length != J313_AGX_G2_GPU_SIZE)
    return 0;
  *mapped = model->GpuRegisters;
  return 1;
}

static void publish_barrier(void *opaque) { (void)opaque; }

static unsigned char publish_unmap(void *opaque,
                                   volatile unsigned char *mapped) {
  START_MEMORY_MODEL *model = opaque;
  return mapped == model->GpuRegisters;
}

static int replay_memory_stages(ADMISSION_PHYSICAL_ALLOCATION *borrowed) {
  START_MEMORY_MODEL *model = calloc(1, sizeof(*model));
  APPLE_AGX_MEMORY_IO memory_io = {0};
  APPLE_AGX_MEMORY_OBJECT local = {0};
  APPLE_AGX_MEMORY_OBJECT pages[64] = {{0}};
  APPLE_AGX_UAT_PAGE uat_pages[64] = {{0}};
  APPLE_AGX_UAT_MAPPING mappings[8] = {{0}};
  APPLE_AGX_RESIDENCY_CONTEXT residency = {0};
  APPLE_AGX_RESIDENCY_STATUS status = {0};
  APPLE_AGX_UAT_TTBR_PAIR pair = {0};
  APPLE_AGX_CONFIG_SNAPSHOT snapshot = {0};
  APPLE_AGX_UAT_PUBLICATION_IO publication = {0};
  APPLE_AGX_UAT_PUBLICATION_STATE published = {0};
  unsigned long long physical = 0, descriptor = 0;
  int failure = 1;
  if (model == NULL) return 1;
  memory_io.Context = model;
  memory_io.AllocateContiguous = table_allocate;
  memory_io.FreeContiguous = table_free;
  local.AllocationCpuBase = borrowed->CpuBase;
  local.CpuAddress = borrowed->CpuBase;
  local.AllocationHandle = borrowed;
  local.AllocationDeviceBase = borrowed->HostPhysicalBase;
  local.DeviceAddress = borrowed->HostPhysicalBase;
  local.AllocationLength = borrowed->Size;
  local.Length = borrowed->Size;
  local.State = AppleAgxMemoryCpuOwned;
  memset(local.CpuAddress, 0, (size_t)local.Length);
  if (AppleAgxMemoryMarkCpuWritten(&local) != AppleAgxMemoryResultOk ||
      AppleAgxMemoryMarkPrepared(&local) != AppleAgxMemoryResultOk)
    goto done;
  if (!AppleAgxResidencyContextCreate(&residency, 63u, &memory_io,
                                      pages, 64u, uat_pages, 64u,
                                      mappings, 8u, &status))
    goto done;
  if (!AppleAgxResidencyMap64K(&local, 63u, &residency.Roots,
                               UINT64_C(0x1500000000),
                               &residency.Allocator, &residency.Inventory,
                               &status))
    goto done;
  if (AppleAgxUatMap(63u, &residency.Roots, UINT64_C(0x1100020000),
                     borrowed->HostPhysicalBase + ADMISSION_BACKEND_OFFSET,
                     0x40000u, AppleAgxUatGpuPipelineShared,
                     &residency.Allocator, &residency.Inventory) !=
          AppleAgxUatResultOk ||
      AppleAgxUatMap(63u, &residency.Roots, UINT64_C(0x1100010000),
                     borrowed->HostPhysicalBase + (ADMISSION_BACKEND_OFFSET + 0x40000),
                     0x4000u, AppleAgxUatGpuPipelineShared,
                     &residency.Allocator, &residency.Inventory) !=
          AppleAgxUatResultOk)
    goto done;
  if (AppleAgxUatResolvePage(63u, &residency.Roots,
                             UINT64_C(0x1500000000), &residency.Inventory,
                             &physical, &descriptor) != AppleAgxUatResultOk ||
      physical != borrowed->HostPhysicalBase)
    goto done;
  assert(residency.Inventory.PageCount == 36); /* 32 local leaves + two roots + middle + alias leaf */
  assert(AppleAgxUatResolvePage(63u, &residency.Roots,
      UINT64_C(0x1500000000) + 0x3fffc000, &residency.Inventory,
      &physical, &descriptor) == AppleAgxUatResultOk);
  assert(physical == borrowed->HostPhysicalBase + 0x3fffc000);
  assert(UINT64_C(0x1500000000) + local.Length <= UINT64_C(0x1600000000));
  if (AppleAgxUatEncodeTtbrPair(63u, &residency.Roots, &pair) !=
      AppleAgxUatResultOk)
    goto done;
  snapshot.GpuRegionBase = J313_AGX_G2_GPU_BASE;
  publication.Context = model;
  publication.Map = publish_map;
  publication.Barrier = publish_barrier;
  publication.Unmap = publish_unmap;
  if (AppleAgxUatPublishJ313Context(&snapshot, 63u, &pair,
                                    &publication, &published) !=
          AppleAgxUatPublicationResultOk ||
      published.Context != 63u || published.PublishedTtbr0 != pair.Ttbr0 ||
      published.PublishedTtbr1 != pair.Ttbr1)
    goto done;
  failure = 0;
done:
  if (failure)
    fprintf(stderr, "EXP831 memory replay failed: stage published=%u pages=%u mappings=%u result=%u\n",
            published.Active, residency.Inventory.PageCount,
            residency.Inventory.MappingCount, status.UatResult);
  if (published.Active)
    (void)AppleAgxUatUnpublishJ313(&publication, &published);
  free(model);
  return failure;
}

static int expect(ADMISSION_CONTEXT *context, NTSTATUS expected,
                  const char *label) {
  NTSTATUS actual = AdmissionPlatformValidateResources(context);
  if (actual == expected) return 0;
  fprintf(stderr, "%s: got %08x, expected %08x\n", label,
          (unsigned)actual, (unsigned)expected);
  return 1;
}

/* Real Start/Stop and fixed-panel client talk to the real broker state machine.
 * Only Windows services and the hardware service completion are simulated. */
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xc00000bb)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xc0000483)
#include "apple_agx_vsync.h"
#define PASSIVE_LEVEL 0
#define KeGetCurrentIrql() PASSIVE_LEVEL
#define ADMISSION_SCANOUT_TAG 0
#define ADMISSION_SCANOUT_TIMEOUT_MS 2000ULL
#define ADMISSION_SCANOUT_MAX_POLLS 40000u
typedef int32_t LONG;
typedef int64_t LONG64;
typedef unsigned KIRQL;
typedef int KTIMER, KDPC, KSPIN_LOCK;
#define KeInitializeSpinLock(p) (*(p)=0)
#define KeInitializeTimer(p) (*(p)=0)
#define KeInitializeDpc(p,f,c) ((void)(p),(void)(f),(void)(c))
#define KeAcquireSpinLock(p,o) ((void)(p),*(o)=0)
#define KeReleaseSpinLock(p,o) ((void)(p),(void)(o))
#define KeCancelTimer(p) ((void)(p),1)
#define KeRemoveQueueDpc(p) ((void)(p),1)
#define KeFlushQueuedDpcs() ((void)0)
/* PRODUCTION_SCANOUT_RUNTIME */
static void AdmissionScanoutTimerDpc(void) {}
static void AdmissionScanoutVsyncRecord(ADMISSION_SCANOUT_RUNTIME *r, ULONG k, NTSTATUS s) {(void)r;(void)k;(void)s;}
static NTSTATUS replay_sync(void *d,BOOLEAN (*f)(void *),void *c,ULONG n,BOOLEAN *out) {(void)d;(void)n;*out=f(c);return 0;}

static int InterlockedExchange(volatile int *value, int next) {
  int previous = *value; *value = next; return previous;
}
static int InterlockedCompareExchange(volatile int *value, int next, int expected) {
  int previous = *value;
  if (previous == expected) *value = next;
  return previous;
}
/* PRODUCTION_VSYNC_CONTROL */
static struct hv_agx_scanout_broker scanout_broker;
static unsigned broker_accesses;
static APPLE_AGX_SCANOUT_U64 AdmissionScanoutNow(void *opaque) {
  (void)opaque; return 0;
}
static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutPause(void *opaque) {
  struct hv_agx_scanout_request request;
  (void)opaque;
  if (hv_agx_scanout_broker_take_pending(&scanout_broker, &request)) {
    if (request.Command == HV_AGX_SCANOUT_CMD_REGISTER_POOL)
      assert(hv_agx_scanout_broker_complete_register(&scanout_broker,
          request.Sequence, HV_AGX_SCANOUT_RESULT_OK, request.PoolIpa, 0x10000000));
    else if (request.Command == HV_AGX_SCANOUT_CMD_RELEASE)
      assert(hv_agx_scanout_broker_complete_release(&scanout_broker,
          request.Sequence, HV_AGX_SCANOUT_RESULT_OK, true));
    else assert(0);
  }
  return 1;
}
static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutRead64(
    void *opaque, APPLE_AGX_SCANOUT_U32 offset, APPLE_AGX_SCANOUT_U64 *value) {
  uint64_t wire = 0; (void)opaque; ++broker_accesses;
  int ok = hv_agx_scanout_broker_mmio(&scanout_broker,
      offset - APPLE_AGX_SCANOUT_MMIO_OFFSET, &wire, false, 3);
  *value = wire; return ok;
}
static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutRead32(
    void *opaque, APPLE_AGX_SCANOUT_U32 offset, APPLE_AGX_SCANOUT_U32 *value) {
  uint64_t wire = 0; (void)opaque; ++broker_accesses;
  int ok = hv_agx_scanout_broker_mmio(&scanout_broker,
      offset - APPLE_AGX_SCANOUT_MMIO_OFFSET, &wire, false, 2);
  *value = (APPLE_AGX_SCANOUT_U32)wire; return ok;
}
static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutWrite64(
    void *opaque, APPLE_AGX_SCANOUT_U32 offset, APPLE_AGX_SCANOUT_U64 value) {
  uint64_t wire = value; (void)opaque; ++broker_accesses;
  return hv_agx_scanout_broker_mmio(&scanout_broker,
      offset - APPLE_AGX_SCANOUT_MMIO_OFFSET, &wire, true, 3);
}
static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutWrite32(
    void *opaque, APPLE_AGX_SCANOUT_U32 offset, APPLE_AGX_SCANOUT_U32 value) {
  uint64_t wire = value; (void)opaque; ++broker_accesses;
  return hv_agx_scanout_broker_mmio(&scanout_broker,
      offset - APPLE_AGX_SCANOUT_MMIO_OFFSET, &wire, true, 2);
}
/* PRODUCTION_SCANOUT_START */
/* PRODUCTION_SCANOUT_STOP */

#define STATUS_INVALID_HANDLE ((NTSTATUS)0xc0000008)
#define STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE ((NTSTATUS)0xc01e0304)
#define D3DDDIFMT_A8R8G8B8 21
#define ADMISSION_ALLOCATION_OBJECT_MAGIC 123
#define InterlockedExchange64(p,v) (*(p)=(v))
typedef int64_t LONG64;
typedef unsigned UINT;
typedef struct { unsigned Width,Height,Pitch,BytesPerPixel,Format; ULONGLONG Size; } ADMISSION_ALLOCATION_DESCRIPTION;
typedef struct { struct { unsigned Magic; ADMISSION_ALLOCATION_DESCRIPTION Description; } Object; } ADMISSION_ALLOCATION_HANDLE;
typedef struct { unsigned VidPnSourceId; void *hAllocation; unsigned PrimarySegment; PHYSICAL_ADDRESS PrimaryAddress; } DXGKARG_SETVIDPNSOURCEADDRESS;
static bool AdmissionAllocationDescriptionValid(const ADMISSION_ALLOCATION_DESCRIPTION *d) { return d->Size != 0; }
/* PRODUCTION_QUEUE_PRESENT */
/* SEGMENT_QUERY_REPLAY */

static int replay_scanout_start(ADMISSION_CONTEXT *context) {
  ADMISSION_BACKEND_MEMORY_VIEW backend;
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  ADMISSION_LOCAL_MEMORY_VIEW local;
  ADMISSION_MEMORY_RUNTIME *memory_runtime = context->MemoryRuntime;
  ADMISSION_PHYSICAL_ALLOCATION *allocation =
      memory_runtime->LocalObject.AllocationHandle;
  APPLE_AGX_U64 surface;
  NTSTATUS status;
  context->BrokerBase = &scanout_broker;
  hv_agx_scanout_broker_init_v2(&scanout_broker, true);
  status = AdmissionScanoutStart(context);
  if (status != STATUS_SUCCESS) {
    fprintf(stderr, "EXP855 real ScanoutStart 40/16/8 failed: %08x (MMIO %u)\n",
            (unsigned)status, broker_accesses);
    return 1;
  }
  assert(scanout_broker.registered_pool_size == 0x3800000ULL);
  assert(scanout_broker.pool_pa == context->LocalReserveReceipt.HostPa);
  ADMISSION_ALLOCATION_HANDLE primary={.Object={ADMISSION_ALLOCATION_OBJECT_MAGIC,
    {2560,1600,10240,4,D3DDDIFMT_A8R8G8B8,APPLE_AGX_SCANOUT_J313_SURFACE_SIZE}}};
  DXGKARG_SETVIDPNSOURCEADDRESS args={0,&primary,2,{context->Memory.Topology.Local.Base+0x8000000}};
  context->DisplayActive=context->SourceVisible=TRUE;
  unsigned before_present=broker_accesses;
  assert(AdmissionScanoutQueuePresent(context,&args)==STATUS_INVALID_ADDRESS);
  assert(broker_accesses==before_present);

  assert(context->Memory.LocalAllocationBytes == 0x3b800000ULL);
  assert(context->Memory.PrivateOffset == 0x3b800000ULL);
  assert(context->Memory.PrivateBytes == 0x4000000ULL);
  assert(AdmissionMemoryRuntimeScanoutView(context, &view) == STATUS_SUCCESS);
  assert(view.Bytes == 0x3800000ULL && view.PoolBytes == 0x3800000ULL);
  assert(AdmissionMemoryRuntimeBackendView(context, &backend) == STATUS_SUCCESS);
  assert(backend.GuestIpaAddress == context->LocalReserveReceipt.GuestIpa + 0x3f800000ULL);
  assert(backend.HostPhysicalAddress == view.HostPhysicalAddress + 0x3f800000ULL);
  assert(backend.GpuVirtualAddress == view.GpuVirtualAddress + 0x3f800000ULL);
  assert(backend.CpuAddress == (PUCHAR)view.CpuAddress + 0x3f800000ULL);
  assert(backend.Bytes == 0x800000ULL);
  assert(ADMISSION_BACKEND_OFFSET == context->Memory.BackendOffset);
  assert(AdmissionScanoutStop(context) == STATUS_SUCCESS);
  assert(context->ScanoutRuntime == NULL);
  assert(scanout_broker.registered_pool_size == 0);
  /* Same real bounds used by QueuePresent and local CPU/paging consumers:
   * exact end succeeds; crossing into private/backend storage fails. */
  const ULONGLONG last = view.Bytes - APPLE_AGX_SCANOUT_J313_SURFACE_SIZE;
  assert(AppleAgxLocalSegmentAddressToGpuVa(2, 2,
      context->Memory.Topology.Local.Base, view.Bytes, 0, context->Memory.Topology.Local.Base + last,
      APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, 0, &surface) == AppleAgxLocalSegmentAddressOk);
  const ULONGLONG invalid[] = {last + 0x10000, view.Bytes, view.PoolBytes};
  for (unsigned i = 0; i < RTL_NUMBER_OF(invalid); ++i) {
    assert(AppleAgxLocalSegmentAddressToGpuVa(2, 2,
        context->Memory.Topology.Local.Base, view.Bytes, 0, context->Memory.Topology.Local.Base + invalid[i],
        APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, 0, &surface) != AppleAgxLocalSegmentAddressOk);
    assert(AdmissionMemoryResolveLocalView(&context->Memory, context->Memory.Topology.Local.Base + invalid[i],
        APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, 0, view.CpuAddress,
        view.HostPhysicalAddress, &local));
  }
  assert(AdmissionMemoryRuntimeLocalView(context, &view) == STATUS_SUCCESS);
  assert(view.Bytes == 0x3b800000ULL);
  const ULONGLONG offsets[] = {0, 0x4000000, 0x8000000, 0x3b7f0000};
  for(unsigned i=0;i<RTL_NUMBER_OF(offsets);++i) {
    assert(AdmissionMemoryResolveLocalView(&context->Memory,
      context->Memory.Topology.Local.Base + offsets[i], 0x10000, 0xffff,
      view.CpuAddress, view.HostPhysicalAddress, &local));
    assert(local.HostPhysicalAddress == view.HostPhysicalAddress + offsets[i] + 0xffff);
  }
  assert(!AdmissionMemoryResolveLocalView(&context->Memory,
      context->Memory.Topology.Local.Base + 0x3b7fffff, 2, 0,
      view.CpuAddress, view.HostPhysicalAddress, &local));
  unsigned accesses = broker_accesses;
  memory_runtime->LocalObject.Length = view.Bytes;
  assert(AdmissionScanoutStart(context) == STATUS_INVALID_ADDRESS);
  memory_runtime->LocalObject.Length = ADMISSION_LOCAL_BYTES;
  context->Memory.BackendOffset -= 0x10000;
  assert(AdmissionScanoutStart(context) == STATUS_INVALID_ADDRESS);
  context->Memory.BackendOffset += 0x10000;
  context->Memory.PrivateOffset += 0x10000;
  assert(AdmissionScanoutStart(context) == STATUS_INVALID_ADDRESS);
  context->Memory.PrivateOffset -= 0x10000;
  context->Memory.PrivateBytes -= 0x10000;
  assert(AdmissionScanoutStart(context) == STATUS_INVALID_ADDRESS);
  context->Memory.PrivateBytes += 0x10000;
  /* The old 40-MiB check would allow this offset, but 56 MiB cannot fit. */
  memory_runtime->LocalObject.CpuAddress = allocation->CpuBase + 0x1000000;
  memory_runtime->LocalObject.DeviceAddress = allocation->HostPhysicalBase + 0x1000000;
  assert(AdmissionScanoutStart(context) == STATUS_INVALID_ADDRESS);
  memory_runtime->LocalObject.CpuAddress = allocation->CpuBase;
  memory_runtime->LocalObject.DeviceAddress = allocation->HostPhysicalBase;
  assert(broker_accesses == accesses && context->ScanoutRuntime == NULL);
  /* Recovery ABI v1 must still be refused before REGISTER. */
  hv_agx_scanout_broker_init(&scanout_broker);
  assert(AdmissionScanoutStart(context) == STATUS_NOT_SUPPORTED);
  assert(scanout_broker.accepted_requests == 0 && context->ScanoutRuntime == NULL);
  hv_agx_scanout_broker_init_v2(&scanout_broker, true);
  assert(AdmissionScanoutStart(context) == STATUS_SUCCESS);
  assert(AdmissionScanoutStop(context) == STATUS_SUCCESS);
  return 0;
}

/* MEMORY_LIFECYCLE_REPLAY */

int main(void) {
  CM_RESOURCE_LIST list = {0};
  ADMISSION_CONTEXT context = {0};
  context.InterfaceValid=TRUE;
  context.Interface.DxgkCbSynchronizeExecution=replay_sync;
  CM_PARTIAL_RESOURCE_DESCRIPTOR *entry = list.List[0].PartialResourceList.PartialDescriptors;
  int failures = 0;
  ADMISSION_PHYSICAL_OWNER owner = { .Initialized = TRUE };
  DXGKRNL_INTERFACE interface = {
      .DxgkCbUnmapPhysicalMemory=dxgk_unmap, .DxgkCbFreeAdl=dxgk_free_adl, .DxgkCbDestroyPhysicalMemoryObject=dxgk_destroy };
  ADMISSION_PHYSICAL_ALLOCATION *allocation = NULL;
  ADMISSION_PHYSICAL_ALLOCATION *old_allocation = NULL;
  ADMISSION_RESOURCE_LIST_RECEIPT resource_receipt = {0};
  ADMISSION_MEMORY_RUNTIME scanout_runtime = {0};
  ADMISSION_SCANOUT_MEMORY_VIEW scanout_view = {0};
  NTSTATUS borrow_status;
  owner.Interface = &interface;
  context.DeviceInformation.TranslatedResourceList = &list;
  memcpy(broker_registers + APPLE_AGX_LOCAL_RESERVE_OFFSET + APPLE_AGX_LOCAL_REG_MAGIC,
         &(uint32_t){APPLE_AGX_LOCAL_RESERVE_MAGIC}, sizeof(uint32_t));
  memcpy(broker_registers + APPLE_AGX_LOCAL_RESERVE_OFFSET + APPLE_AGX_LOCAL_REG_VERSION,
         &(uint32_t){APPLE_AGX_LOCAL_RESERVE_VERSION}, sizeof(uint32_t));
  memcpy(broker_registers + APPLE_AGX_LOCAL_RESERVE_OFFSET + APPLE_AGX_LOCAL_REG_VALID,
         &(uint32_t){1}, sizeof(uint32_t));
  memcpy(broker_registers + APPLE_AGX_LOCAL_RESERVE_OFFSET + APPLE_AGX_LOCAL_REG_GUEST_IPA,
         &(uint64_t){UINT64_C(0x8e0000000)}, sizeof(uint64_t));
  memcpy(broker_registers + APPLE_AGX_LOCAL_RESERVE_OFFSET + APPLE_AGX_LOCAL_REG_HOST_PA,
         &(uint64_t){UINT64_C(0x8e0000000)}, sizeof(uint64_t));
  memcpy(broker_registers + APPLE_AGX_LOCAL_RESERVE_OFFSET + APPLE_AGX_LOCAL_REG_BYTES,
         &(uint64_t){APPLE_AGX_LOCAL_RESERVE_BYTES}, sizeof(uint64_t));
  list.Count = 1;
  list.List[0].PartialResourceList.Count = 11;
  memory(&entry[0], J313_AGX_G2_SGX_MMIO_BASE, J313_AGX_G2_SGX_MMIO_SIZE);
  memory(&entry[2], J313_AGX_G2_GPU_BASE, J313_AGX_G2_GPU_SIZE);
  memory(&entry[4], J313_AGX_G2_HANDOFF_BASE, J313_AGX_G2_HANDOFF_SIZE);
  memory(&entry[6], J313_AGX_G2_POWER_BROKER_BASE, J313_AGX_G2_POWER_BROKER_SIZE);
  memory(&entry[8], UINT64_C(0x8e0000000), UINT32_C(0x40000000));
  for (unsigned index = 1; index < 10; index += 2) {
    entry[index].Type = CmResourceTypeDevicePrivate;
    entry[index].Flags = 24576;
    entry[index].u.DevicePrivate.Data[0] = 3;
    entry[index].u.DevicePrivate.Data[1] = (ULONG)entry[index - 1].u.Memory.Start.QuadPart;
    entry[index].u.DevicePrivate.Data[2] =
        (ULONG)((ULONGLONG)entry[index - 1].u.Memory.Start.QuadPart >> 32);
  }
  entry[10].Type = CmResourceTypeInterrupt;
  entry[10].ShareDisposition = CmResourceShareDeviceExclusive;
  entry[10].Flags = CM_RESOURCE_INTERRUPT_LATCHED;
  entry[10].u.Interrupt.Level = 889;
  entry[10].u.Interrupt.Vector = 2304;
  entry[10].u.Interrupt.Affinity = UINT64_MAX;
  AdmissionFillTranslatedResources(&list, &resource_receipt);
  if (resource_receipt.Version != 1 || resource_receipt.FullCount != 1 ||
      resource_receipt.PartialCount != 11 || resource_receipt.CapturedCount != 11 ||
      resource_receipt.Truncated != 0 ||
      resource_receipt.Entries[0].Type != 3 ||
      resource_receipt.Entries[0].Address != J313_AGX_G2_SGX_MMIO_BASE ||
      resource_receipt.Entries[0].Length != J313_AGX_G2_SGX_MMIO_SIZE ||
      resource_receipt.Entries[1].Type != 129 ||
      resource_receipt.Entries[1].Address != 0 ||
      resource_receipt.Entries[10].Type != 2 ||
      resource_receipt.Entries[10].Vector != 2304) {
    fprintf(stderr, "EXP833 resource receipt lost translated descriptors\n");
    return 1;
  }
  if (!AdmissionG3FirmwareResourcesPresent(&context)) {
    fprintf(stderr, "EXP833 G3 preflight rejected valid resources\n");
    return 1;
  }
  list.List[0].PartialResourceList.Count = 10;
  if (AdmissionG3FirmwareResourcesPresent(&context)) {
    fprintf(stderr, "G3 preflight admitted ordinary four-resource profile\n");
    return 1;
  }
  list.List[0].PartialResourceList.Count = 11;
  memory(&entry[6], UINT64_C(0x300010000), UINT32_C(0x1000));
  if (AdmissionG3FirmwareResourcesPresent(&context)) {
    fprintf(stderr, "G3 preflight admitted absent broker\n");
    return 1;
  }
  memory(&entry[6], J313_AGX_G2_POWER_BROKER_BASE, J313_AGX_G2_POWER_BROKER_SIZE);
  memory(&entry[8], UINT64_C(0x8e0004000), UINT32_C(0x40000000));
  if (AdmissionG3FirmwareResourcesPresent(&context)) {
    fprintf(stderr, "G3 preflight admitted misaligned local range\n");
    return 1;
  }
  memory(&entry[8], UINT64_C(0x8e0000000), UINT32_C(0x40000000));
  fail_local_map = true;
  assert(AdmissionPhysicalBorrowLocal(&owner, &context.DeviceInformation,
      &allocation, &context.LocalReserveReceipt) == STATUS_INSUFFICIENT_RESOURCES);
  assert(allocation == NULL && owner.AllocationCount == 0 && local_mapping == NULL);
  fail_local_map = false;
  CM_PARTIAL_RESOURCE_DESCRIPTOR saved_private = entry[9];
  entry[9] = entry[8];
  assert(AdmissionPhysicalBorrowLocal(&owner, &context.DeviceInformation,
      &allocation, &context.LocalReserveReceipt) == STATUS_INVALID_DEVICE_STATE);
  assert(allocation == NULL && owner.AllocationCount == 0);
  entry[9] = saved_private;
  const UINT bad_pages[]={1,262143};
  for(unsigned i=0;i<RTL_NUMBER_OF(bad_pages);++i) {
    broken_translation=bad_pages[i];
    assert(!NT_SUCCESS(AdmissionPhysicalBorrowLocal(&owner, &context.DeviceInformation,
      &allocation, &context.LocalReserveReceipt)));
    assert(allocation==NULL && local_mapping==NULL && owner.AllocationCount==0);
    assert(translated_pages==262144);
  }
  assert(local_unmaps==2);
  local_unmaps=0;
  broken_translation=UINT32_MAX;
  borrow_status = AdmissionPhysicalBorrowLocal(&owner, &context.DeviceInformation,
                                                 &allocation, &context.LocalReserveReceipt);
  if (borrow_status != STATUS_SUCCESS || allocation == NULL ||
      owner.AllocationCount != 1) {
    fprintf(stderr, "EXP831 production borrow failed: %08x\n", (unsigned)borrow_status);
    return 1;
  }
  if (!allocation->BorrowedFirmwareReserve || allocation->Adl != NULL ||
      allocation->PhysicalMemoryObject != NULL ||
      allocation->AdapterMemoryObject != NULL ||
      allocation->CpuBase != local_mapping ||
      allocation->MappedSize != APPLE_AGX_LOCAL_RESERVE_BYTES) {
    fprintf(stderr, "borrowed allocation impersonated a Dxgk-owned object\n");
    return 1;
  }
  scanout_runtime.LocalObject.AllocationCpuBase = allocation->CpuBase;
  scanout_runtime.LocalObject.CpuAddress = allocation->CpuBase;
  scanout_runtime.LocalObject.AllocationHandle = allocation;
  scanout_runtime.LocalObject.DeviceAddress = allocation->HostPhysicalBase;
  scanout_runtime.LocalObject.GpuVirtualAddress = ADMISSION_LOCAL_GPU_VA;
  scanout_runtime.PhysicalReady = scanout_runtime.LocalReady =
      scanout_runtime.ResidencyReady = scanout_runtime.MappingReady =
      scanout_runtime.PublicationReady = TRUE;
  context.MemoryRuntime = &scanout_runtime;
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY aperture[16] = {{0}};
  assert(AdmissionMemoryInitialize(&context.Memory, aperture, 16,
      0x1600000000ULL, 0x10000ULL, ADMISSION_LOCAL_GPU_VA, ADMISSION_LOCAL_BYTES));
  assert(AdmissionMemoryPartitionLocal(&context.Memory,
      ADMISSION_LOCAL_ALLOCATION_BYTES, ADMISSION_PRIVATE_BYTES, ADMISSION_BACKEND_BYTES));
  context.Memory.UatReady = APPLE_AGX_TRUE;
  scanout_runtime.LocalObject.Length = ADMISSION_LOCAL_BYTES;
  if (AdmissionMemoryRuntimeScanoutView(&context, &scanout_view) !=
          STATUS_SUCCESS ||
      scanout_view.GuestIpaAddress != UINT64_C(0x8e0000000) ||
      scanout_view.HostPhysicalAddress != UINT64_C(0x8e0000000) ||
      scanout_view.Bytes != APPLE_AGX_SCANOUT_J313_POOL_SIZE) {
    fprintf(stderr, "EXP834 borrowed scanout view failed: %08x\n",
            (unsigned)AdmissionMemoryRuntimeScanoutView(&context, &scanout_view));
    return 1;
  }
  replay_segment_queries(&context);
  if (replay_scanout_start(&context)) return 1;
  context.LocalReserveReceipt.HostPa += UINT64_C(0x4000);
  if (AdmissionMemoryRuntimeScanoutView(&context, &scanout_view) !=
      STATUS_INVALID_DEVICE_STATE) {
    fprintf(stderr, "scanout admitted mismatched reserve PA\n");
    return 1;
  }
  context.LocalReserveReceipt.HostPa -= UINT64_C(0x4000);
  scanout_runtime.LocalObject.DeviceAddress += UINT64_C(0x4000);
  if (AdmissionMemoryRuntimeScanoutView(&context, &scanout_view) !=
      STATUS_INVALID_ADDRESS) {
    fprintf(stderr, "scanout admitted mismatched device address\n");
    return 1;
  }
  scanout_runtime.LocalObject.DeviceAddress -= UINT64_C(0x4000);
  scanout_runtime.LocalObject.CpuAddress = allocation->CpuBase + 0x4000;
  scanout_runtime.LocalObject.DeviceAddress += UINT64_C(0x4000);
  assert(AdmissionMemoryRuntimeScanoutView(&context, &scanout_view) == STATUS_INTEGER_OVERFLOW);
  scanout_runtime.LocalObject.CpuAddress = allocation->CpuBase;
  scanout_runtime.LocalObject.DeviceAddress = allocation->HostPhysicalBase;
  failures += replay_memory_stages(allocation);
  failures += expect(&context, STATUS_SUCCESS, "EXP831 exact resources");
  memory(&entry[8], UINT64_C(0x8e0004000), UINT32_C(0x40000000));
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "wrong local IPA");
  memory(&entry[8], UINT64_C(0x8e0000000), UINT32_C(0x2000000));
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "short local resource");
  memory(&entry[8], UINT64_C(0x8e0000000), UINT32_C(0x40000000));
  context.LocalReserveReceipt.Valid = 0;
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "invalid receipt");
  context.LocalReserveReceipt.Valid = 1;
  list.List[0].PartialResourceList.Count = 12;
  memory(&entry[11], UINT64_C(0x8e0000000), UINT32_C(0x40000000));
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "duplicate local");
  list.List[0].PartialResourceList.Count = 11;
  memory(&entry[2], UINT64_C(0x8e0000000), UINT32_C(0x40000000));
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "missing GPU resource");
  memory(&entry[2], J313_AGX_G2_GPU_BASE, J313_AGX_G2_GPU_SIZE);
  entry[10].u.Interrupt.Vector = 0;
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "invalid IRQ");
  entry[10].u.Interrupt.Vector = 2304;
  list.List[0].PartialResourceList.Count = 12;
  entry[11].Type = CmResourceTypeDevicePrivate;
  failures += expect(&context, STATUS_SUCCESS, "system private resource");
  entry[11].Type = 77;
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "unexpected resource type");
  if (failures) return 1;
  if (AdmissionPhysicalFree(&owner, allocation) != STATUS_SUCCESS ||
      owner.AllocationCount != 0 || local_unmaps != 1 ||
      dxgk_unmaps != 0 || dxgk_adl_frees != 0 || dxgk_destroys != 0) {
    fprintf(stderr, "borrowed StopDevice released an owned object or leaked mapping\n");
    return 1;
  }
  old_allocation = calloc(1, sizeof(*old_allocation));
  if (old_allocation == NULL) return 1;
  old_allocation->Interface = &interface;
  old_allocation->PhysicalMemoryObject = (void *)0x1;
  old_allocation->AdapterMemoryObject = (void *)0x2;
  old_allocation->Adl = calloc(1, sizeof(*old_allocation->Adl));
  old_allocation->MappedBase = malloc(APPLE_AGX_LOCAL_RESERVE_BYTES);
  old_allocation->MappedSize = APPLE_AGX_LOCAL_RESERVE_BYTES;
  old_allocation->CpuBase = old_allocation->MappedBase;
  old_allocation->Size = APPLE_AGX_LOCAL_RESERVE_BYTES;
  old_allocation->GuestIpaBase = UINT64_C(0x8f0000000);
  old_allocation->HostPhysicalBase = UINT64_C(0x8f0000000);
  if (old_allocation->Adl != NULL)
    old_allocation->Adl->Flags.Contiguous = 1;
  scanout_runtime.LocalObject.AllocationCpuBase = old_allocation->CpuBase;
  scanout_runtime.LocalObject.CpuAddress = old_allocation->CpuBase;
  scanout_runtime.LocalObject.AllocationHandle = old_allocation;
  scanout_runtime.LocalObject.DeviceAddress = old_allocation->HostPhysicalBase;
  if (AdmissionMemoryRuntimeScanoutView(&context, &scanout_view) !=
          STATUS_SUCCESS ||
      scanout_view.GuestIpaAddress != UINT64_C(0x8f0000000)) {
    fprintf(stderr, "Dxgk-owned contiguous ADL scanout view regressed\n");
    return 1;
  }
  old_allocation->Size = ADMISSION_LOCAL_ALLOCATION_BYTES;
  assert(AdmissionScanoutStart(&context) == STATUS_INVALID_ADDRESS);
  old_allocation->Size = ADMISSION_LOCAL_BYTES;
  ULONGLONG saved_ipa = old_allocation->GuestIpaBase;
  old_allocation->GuestIpaBase = MAXULONGLONG - APPLE_AGX_SCANOUT_J313_POOL_SIZE + 1;
  assert(AdmissionMemoryRuntimeScanoutView(&context, &scanout_view) == STATUS_INTEGER_OVERFLOW);
  old_allocation->GuestIpaBase = saved_ipa;
  ULONGLONG saved_pa = old_allocation->HostPhysicalBase;
  old_allocation->HostPhysicalBase = MAXULONGLONG - APPLE_AGX_SCANOUT_J313_POOL_SIZE + 1;
  scanout_runtime.LocalObject.DeviceAddress = old_allocation->HostPhysicalBase;
  assert(AdmissionMemoryRuntimeScanoutView(&context, &scanout_view) == STATUS_INTEGER_OVERFLOW);
  old_allocation->HostPhysicalBase = saved_pa;
  scanout_runtime.LocalObject.DeviceAddress = saved_pa;
  owner.AllocationCount = 1;
  if (old_allocation->Adl == NULL || old_allocation->MappedBase == NULL ||
      AdmissionPhysicalFree(&owner, old_allocation) != STATUS_SUCCESS ||
      owner.AllocationCount != 0 || local_unmaps != 1 ||
      dxgk_unmaps != 1 || dxgk_adl_frees != 1 || dxgk_destroys != 1) {
    fprintf(stderr, "Dxgk-owned allocation did not release all callbacks\n");
    return 1;
  }
  replay_memory_lifecycle(&context);
  puts("EXP831 exact resources PASS; malformed cases rejected");
  return 0;
}
