#include <stdint.h>
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
#define STATUS_PENDING ((NTSTATUS)0x00000103)
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
  void *Interface;
  PVOID MappedBase;
  SIZE_T MappedSize;
  PUCHAR CpuBase;
  SIZE_T Size;
  ULONGLONG GuestIpaBase;
  ULONGLONG HostPhysicalBase;
  BOOLEAN BorrowedFirmwareReserve;
} ADMISSION_PHYSICAL_ALLOCATION;
typedef struct {
  void *Interface;
  ULONG LastAllocateStep;
  ULONGLONG LastAllocateBytes;
  NTSTATUS LastAllocateStatus;
  int AllocationCount;
  BOOLEAN Initialized;
} ADMISSION_PHYSICAL_OWNER;
typedef struct {
  DXGK_DEVICE_INFO DeviceInformation;
  APPLE_AGX_LOCAL_RESERVE_RECEIPT LocalReserveReceipt;
} ADMISSION_CONTEXT;

static UCHAR broker_registers[J313_AGX_G2_POWER_BROKER_SIZE];
static UCHAR *local_mapping;

static PVOID MmMapIoSpaceEx(PHYSICAL_ADDRESS address, SIZE_T bytes, ULONG protection) {
  if ((ULONGLONG)address.QuadPart == J313_AGX_G2_POWER_BROKER_BASE &&
      bytes == J313_AGX_G2_POWER_BROKER_SIZE &&
      protection == (PAGE_READWRITE | PAGE_NOCACHE))
    return broker_registers;
  if ((ULONGLONG)address.QuadPart == UINT64_C(0x8e0000000) &&
      bytes == APPLE_AGX_LOCAL_RESERVE_BYTES &&
      protection == (PAGE_READWRITE | PAGE_WRITECOMBINE)) {
    local_mapping = calloc(1, bytes);
    return local_mapping;
  }
  return NULL;
}
static void MmUnmapIoSpace(PVOID address, SIZE_T bytes) {
  (void)address; (void)bytes;
}
static PVOID ExAllocatePool2(ULONG flags, SIZE_T bytes, ULONG tag) {
  (void)flags; (void)tag;
  return calloc(1, bytes);
}
static void ExFreePoolWithTag(PVOID pointer, ULONG tag) {
  (void)tag;
  free(pointer);
}
static void AdmissionPhysicalReleaseRaw(ADMISSION_PHYSICAL_ALLOCATION *allocation) {
  if (allocation->MappedBase != NULL) free(allocation->MappedBase);
}
static NTSTATUS AdmissionPhysicalTranslate(
    ADMISSION_PHYSICAL_OWNER *owner, ADMISSION_PHYSICAL_ALLOCATION *allocation) {
  (void)owner;
  allocation->HostPhysicalBase = UINT64_C(0x8e0000000);
  allocation->GuestIpaBase = UINT64_C(0x8e0000000);
  return STATUS_SUCCESS;
}

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
                     borrowed->HostPhysicalBase + UINT64_C(0x3800000),
                     0x40000u, AppleAgxUatGpuPipelineShared,
                     &residency.Allocator, &residency.Inventory) !=
          AppleAgxUatResultOk ||
      AppleAgxUatMap(63u, &residency.Roots, UINT64_C(0x1100010000),
                     borrowed->HostPhysicalBase + UINT64_C(0x3840000),
                     0x4000u, AppleAgxUatGpuPipelineShared,
                     &residency.Allocator, &residency.Inventory) !=
          AppleAgxUatResultOk)
    goto done;
  if (AppleAgxUatResolvePage(63u, &residency.Roots,
                             UINT64_C(0x1500000000), &residency.Inventory,
                             &physical, &descriptor) != AppleAgxUatResultOk ||
      physical != borrowed->HostPhysicalBase)
    goto done;
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

int main(void) {
  CM_RESOURCE_LIST list = {0};
  ADMISSION_CONTEXT context = {0};
  CM_PARTIAL_RESOURCE_DESCRIPTOR *entry = list.List[0].PartialResourceList.PartialDescriptors;
  int failures = 0;
  ADMISSION_PHYSICAL_OWNER owner = { .Initialized = TRUE };
  ADMISSION_PHYSICAL_ALLOCATION *allocation = NULL;
  ADMISSION_RESOURCE_LIST_RECEIPT resource_receipt = {0};
  NTSTATUS borrow_status;
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
  memory(&entry[8], UINT64_C(0x8e0000000), UINT32_C(0x4000000));
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
  entry[10].u.Interrupt.Vector = 889;
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
      resource_receipt.Entries[10].Vector != 889) {
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
  memory(&entry[8], UINT64_C(0x8e0004000), UINT32_C(0x4000000));
  if (AdmissionG3FirmwareResourcesPresent(&context)) {
    fprintf(stderr, "G3 preflight admitted misaligned local range\n");
    return 1;
  }
  memory(&entry[8], UINT64_C(0x8e0000000), UINT32_C(0x4000000));
  borrow_status = AdmissionPhysicalBorrowLocal(&owner, &context.DeviceInformation,
                                                 &allocation, &context.LocalReserveReceipt);
  if (borrow_status != STATUS_SUCCESS || allocation == NULL ||
      owner.AllocationCount != 1) {
    fprintf(stderr, "EXP831 production borrow failed: %08x\n", (unsigned)borrow_status);
    return 1;
  }
  failures += replay_memory_stages(allocation);
  failures += expect(&context, STATUS_SUCCESS, "EXP831 exact resources");
  memory(&entry[8], UINT64_C(0x8e0004000), UINT32_C(0x4000000));
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "wrong local IPA");
  memory(&entry[8], UINT64_C(0x8e0000000), UINT32_C(0x2000000));
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "short local resource");
  memory(&entry[8], UINT64_C(0x8e0000000), UINT32_C(0x4000000));
  context.LocalReserveReceipt.Valid = 0;
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "invalid receipt");
  context.LocalReserveReceipt.Valid = 1;
  list.List[0].PartialResourceList.Count = 12;
  memory(&entry[11], UINT64_C(0x8e0000000), UINT32_C(0x4000000));
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "duplicate local");
  list.List[0].PartialResourceList.Count = 11;
  memory(&entry[2], UINT64_C(0x8e0000000), UINT32_C(0x4000000));
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "missing GPU resource");
  memory(&entry[2], J313_AGX_G2_GPU_BASE, J313_AGX_G2_GPU_SIZE);
  entry[10].u.Interrupt.Vector = 0;
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "invalid IRQ");
  entry[10].u.Interrupt.Vector = 889;
  list.List[0].PartialResourceList.Count = 12;
  entry[11].Type = CmResourceTypeDevicePrivate;
  failures += expect(&context, STATUS_SUCCESS, "system private resource");
  entry[11].Type = 77;
  failures += expect(&context, STATUS_DEVICE_CONFIGURATION_ERROR, "unexpected resource type");
  if (failures) return 1;
  AdmissionPhysicalReleaseRaw(allocation);
  ExFreePoolWithTag(allocation, ADMISSION_PHYSICAL_TAG);
  puts("EXP831 exact resources PASS; malformed cases rejected");
  return 0;
}
