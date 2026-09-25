#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include "render_allocation.h"
#include "render_win32_transport.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define ADAPTER_LIMIT 16u
#define DEVICE_PARAMETERS L"SYSTEM\\CurrentControlSet\\Enum\\ACPI\\APPL0002\\0\\Device Parameters"

typedef struct _G3_ALLOCATION_RECEIPT {
  ULONG Version, Bytes, Sequence, Kind, Status, Count, Flags;
  ULONG PrivateBytes, FirstPrivateBytes, InputPresent, OutputPresent;
  ULONGLONG FirstSize, SystemTime;
} G3_ALLOCATION_RECEIPT;

static ULONG ReadRing(ULONG after) {
  HKEY key = NULL;
  G3_ALLOCATION_RECEIPT entries[16] = {0};
  ULONG latest = after;
  UINT index;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, DEVICE_PARAMETERS, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    wprintf(L"RING unavailable\n");
    return after;
  }
  for (index = 0; index < 16u; ++index) {
    wchar_t name[32];
    DWORD type = 0, bytes = sizeof(entries[index]);
    swprintf_s(name, 32, L"Wom1G3DmaOp%02u", index);
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&entries[index], &bytes) != ERROR_SUCCESS ||
        type != REG_BINARY || bytes != sizeof(entries[index]) ||
        entries[index].Version != 1u || entries[index].Bytes != sizeof(entries[index]))
      entries[index].Sequence = 0;
    if (entries[index].Sequence > latest) latest = entries[index].Sequence;
  }
  RegCloseKey(key);
  for (ULONG sequence = after + 1; sequence != 0 && sequence <= latest; ++sequence) {
    for (index = 0; index < 16u; ++index) {
      const G3_ALLOCATION_RECEIPT *entry = &entries[index];
      if (entry->Sequence == sequence)
        wprintf(L"RING seq=%lu kind=%lu status=0x%08lx count=%lu private=%lu/%lu input=%lu output=%lu size=%llu\n",
                entry->Sequence, entry->Kind, entry->Status, entry->Count,
                entry->PrivateBytes, entry->FirstPrivateBytes,
                entry->InputPresent, entry->OutputPresent, entry->FirstSize);
    }
  }
  fflush(stdout);
  return latest;
}

static BOOL Describe(ADMISSION_WIN32_ALLOCATION_CREATE *native,
                     ADMISSION_ALLOCATION_DESCRIPTION *control,
                     UINT classId, UINT bytes, UINT cpuVisible) {
  ADMISSION_ALLOCATION_DESCRIPTION *description = classId ? &native->Allocation : control;
  ZeroMemory(native, sizeof(*native));
  ZeroMemory(control, sizeof(*control));
  if (!AdmissionAllocationDescribe(bytes, 1u, 1u,
          ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
          ADMISSION_WIN32_ALLOCATION_FORMAT_A8, cpuVisible, description)) return FALSE;
  if (classId) {
    native->Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
    native->Version = ADMISSION_WIN32_ALLOCATION_VERSION;
    native->Bytes = sizeof(*native);
    native->ClassId = classId;
    native->Flags = AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  }
  return TRUE;
}

static ULONG Probe(D3DKMT_HANDLE device, ULONG sequence, UINT classId,
                   UINT bytes, UINT cpuVisible, BOOL resource) {
  ADMISSION_WIN32_ALLOCATION_CREATE native;
  ADMISSION_ALLOCATION_DESCRIPTION control;
  D3DDDI_ALLOCATIONINFO2 info = {0};
  D3DKMT_CREATEALLOCATION create = {0};
  D3DKMT_DESTROYALLOCATION2 destroy = {0};
  D3DKMT_HANDLE handle = 0;
  NTSTATUS status;
  if (!Describe(&native, &control, classId, bytes, cpuVisible)) return sequence;
  info.pPrivateDriverData = classId ? (void *)&native : (void *)&control;
  info.PrivateDriverDataSize = classId ? sizeof(native) : sizeof(control);
  create.hDevice = device;
  create.NumAllocations = 1u;
  create.pAllocationInfo2 = &info;
  create.Flags.CreateResource = resource ? 1u : 0u;
  status = D3DKMTCreateAllocation2(&create);
  wprintf(L"ROW class=%u bytes=%u cpu=%u resource=%u status=0x%08lx allocation=%u hResource=%u\n",
          classId, bytes, cpuVisible, resource, (ULONG)status,
          info.hAllocation, create.hResource);
  fflush(stdout);
  sequence = ReadRing(sequence);
  if (NT_SUCCESS(status) && info.hAllocation) {
    handle = info.hAllocation;
    destroy.hDevice = device;
    destroy.phAllocationList = &handle;
    destroy.AllocationCount = 1u;
    destroy.Flags.SynchronousDestroy = 1u;
    status = D3DKMTDestroyAllocation2(&destroy);
    wprintf(L"DESTROY class=%u status=0x%08lx\n", classId, (ULONG)status);
    fflush(stdout);
  }
  return sequence;
}

int wmain(void) {
  D3DKMT_ADAPTERINFO adapters[ADAPTER_LIMIT] = {0};
  D3DKMT_ENUMADAPTERS3 enumeration = {0};
  D3DKMT_OPENADAPTERFROMLUID open = {0};
  D3DKMT_CREATEDEVICE device = {0};
  D3DKMT_CREATECONTEXTVIRTUAL context = {0};
  ADMISSION_WIN32_CONTEXT_CREATE win32Context = {0};
  D3DKMT_DESTROYCONTEXT destroyContext = {0};
  D3DKMT_DESTROYDEVICE destroyDevice = {0};
  D3DKMT_CLOSEADAPTER close = {0};
  PFND3DKMT_ENUMADAPTERS3 enumerate;
  HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
  ULONG selected = ADAPTER_LIMIT, matches = 0, sequence = 0;
  NTSTATUS status;
  UINT index, classId, sizeIndex, cpu, resource;
  const UINT sizes[] = {0x4000u, 0x10000u, 0x100000u};
  if (!gdi || !(enumerate = (PFND3DKMT_ENUMADAPTERS3)GetProcAddress(gdi, "D3DKMTEnumAdapters3"))) return 2;
  enumeration.NumAdapters = ADAPTER_LIMIT;
  enumeration.pAdapters = adapters;
  status = enumerate(&enumeration);
  if (!NT_SUCCESS(status)) { wprintf(L"ENUM status=0x%08lx\n", (ULONG)status); return 3; }
  for (index = 0; index < enumeration.NumAdapters; ++index) {
    D3DKMT_ADAPTERTYPE type = {0};
    D3DKMT_QUERYADAPTERINFO query = {0};
    query.hAdapter = adapters[index].hAdapter;
    query.Type = KMTQAITYPE_ADAPTERTYPE;
    query.pPrivateDriverData = &type;
    query.PrivateDriverDataSize = sizeof(type);
    status = D3DKMTQueryAdapterInfo(&query);
    if (NT_SUCCESS(status) && type.RenderSupported && type.DisplaySupported &&
        type.PostDevice && !type.SoftwareDevice && !type.ComputeOnly) {
      selected = index;
      ++matches;
    }
  }
  if (matches != 1u) { wprintf(L"ADAPTER matches=%lu\n", matches); return 4; }
  open.AdapterLuid = adapters[selected].AdapterLuid;
  wprintf(L"APPLE_LUID high=%ld low=%lu\n", open.AdapterLuid.HighPart, open.AdapterLuid.LowPart);
  for (index = 0; index < enumeration.NumAdapters; ++index) {
    close.hAdapter = adapters[index].hAdapter;
    if (close.hAdapter) (void)D3DKMTCloseAdapter(&close);
  }
  status = D3DKMTOpenAdapterFromLuid(&open);
  wprintf(L"OPEN status=0x%08lx adapter=%u\n", (ULONG)status, open.hAdapter);
  if (!NT_SUCCESS(status) || !open.hAdapter) return 5;
  device.hAdapter = open.hAdapter;
  status = D3DKMTCreateDevice(&device);
  wprintf(L"DEVICE status=0x%08lx device=%u\n", (ULONG)status, device.hDevice);
  if (!NT_SUCCESS(status) || !device.hDevice) goto close_adapter;
  context.hDevice = device.hDevice;
  context.NodeOrdinal = 0u;
  context.EngineAffinity = 1u;
  context.ClientHint = D3DKMT_CLIENTHINT_UNKNOWN;
  win32Context.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;
  win32Context.Version = ADMISSION_WIN32_CONTEXT_VERSION;
  win32Context.Bytes = sizeof(win32Context);
  win32Context.Generation = 0x01030001u;
  context.pPrivateDriverData = &win32Context;
  context.PrivateDriverDataSize = sizeof(win32Context);
  status = D3DKMTCreateContextVirtual(&context);
  wprintf(L"VIRTUAL_CONTEXT status=0x%08lx context=%u\n", (ULONG)status, context.hContext);
  sequence = ReadRing(0);
  sequence = Probe(device.hDevice, sequence, 0u, 0x10000u, 1u, FALSE);
  for (classId = AgxWin32BufferClassGeneral; classId <= AgxWin32BufferClassEncoder; ++classId)
    for (sizeIndex = 0; sizeIndex < ARRAYSIZE(sizes); ++sizeIndex)
      for (cpu = 0; cpu <= 1u; ++cpu)
        for (resource = 0; resource <= 1u; ++resource)
          sequence = Probe(device.hDevice, sequence, classId, sizes[sizeIndex], cpu, resource != 0u);
  if (context.hContext) {
    destroyContext.hContext = context.hContext;
    (void)D3DKMTDestroyContext(&destroyContext);
  }
  destroyDevice.hDevice = device.hDevice;
  (void)D3DKMTDestroyDevice(&destroyDevice);
close_adapter:
  close.hAdapter = open.hAdapter;
  (void)D3DKMTCloseAdapter(&close);
  return 0;
}
