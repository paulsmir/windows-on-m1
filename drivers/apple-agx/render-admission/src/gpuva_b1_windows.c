#include "render_admission.h"
#include "apple_agx_gpuva_b1_roots.h"
#include "apple_agx_gpuva_b1_status.h"

#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
#define B1_TAG '1BGA'
#define B1_SLOT 1u
#define B1_OWNED_PAGES 6u
#define B1_OUTPUT_VA 0x15001d0000ULL
#define B1_OUTPUT_BYTES 0x4000u

typedef struct _ADMISSION_B1_STATE {
  APPLE_AGX_MEMORY_IO Io;
  APPLE_AGX_GPUVA_V5_CLIENT Client;
  APPLE_AGX_MEMORY_OBJECT Owned[2][B1_OWNED_PAGES];
  ULONGLONG Ipa[2][B1_OWNED_PAGES];
  APPLE_AGX_GPUVA_B1_PAGE Graph[2][APPLE_AGX_GPUVA_B1_GRAPH_PAGES];
  APPLE_AGX_GPUVA_B1_ROOT Roots[2];
  ULONGLONG LeaseToken;
  ULONG LeaseOwner;
  BOOLEAN JobInFlight;
  BOOLEAN Uncertain;
  ULONG Stage, CompletedJobs, BrokerStatus;
  ULONG Precheck, ProbeStatus;
  ULONGLONG ProbeEpoch;
} ADMISSION_B1_STATE;

static NTSTATUS AdmissionB1AllocatePage(
    ADMISSION_B1_STATE *state, ULONG owner, ULONG index) {
  APPLE_AGX_MEMORY_OBJECT *object;
  ADMISSION_PHYSICAL_ALLOCATION *allocation;
  ULONGLONG offset;
  if (state == NULL || owner >= 2u || index >= B1_OWNED_PAGES)
    return STATUS_INVALID_PARAMETER;
  object = &state->Owned[owner][index];
  if (AppleAgxMemoryAllocateAligned(&state->Io, B1_OUTPUT_BYTES,
                                    B1_OUTPUT_BYTES, object) !=
      AppleAgxMemoryResultOk)
    return STATUS_INSUFFICIENT_RESOURCES;
  allocation = (ADMISSION_PHYSICAL_ALLOCATION *)object->AllocationHandle;
  if (allocation == NULL || object->CpuAddress == NULL ||
      object->AllocationCpuBase == NULL ||
      (PUCHAR)object->CpuAddress < (PUCHAR)object->AllocationCpuBase ||
      object->DeviceAddress == 0ULL ||
      (object->DeviceAddress & (B1_OUTPUT_BYTES - 1u)))
    return STATUS_INVALID_ADDRESS;
  offset = (ULONGLONG)((PUCHAR)object->CpuAddress -
                       (PUCHAR)object->AllocationCpuBase);
  if (allocation->GuestIpaBase > MAXULONGLONG - offset)
    return STATUS_INTEGER_OVERFLOW;
  state->Ipa[owner][index] = allocation->GuestIpaBase + offset;
  if ((state->Ipa[owner][index] & (B1_OUTPUT_BYTES - 1u)) != 0u)
    return STATUS_INVALID_ADDRESS;
  RtlZeroMemory(object->CpuAddress, B1_OUTPUT_BYTES);
  return STATUS_SUCCESS;
}

static BOOLEAN AdmissionB1BrokerCall(ADMISSION_B1_STATE *state,
                                     AGX_GPUVA_V5_REQUEST *request,
                                     AGX_GPUVA_V5_RESPONSE *response) {
  if (!AppleAgxGpuvaV5ClientCall(&state->Client, request, response)) {
    state->BrokerStatus = MAXULONG;
    state->Uncertain = TRUE;
    return FALSE;
  }
  state->BrokerStatus = response->Status;
  if (response->Flags != 0u || response->Status == 6u)
    state->Uncertain = TRUE;
  return response->Status == 0u && response->Flags == 0u;
}

static NTSTATUS AdmissionB1LeaseJob(ADMISSION_CONTEXT *context,
                                    ADMISSION_B1_STATE *state,
                                    ULONG owner, ULONG fence) {
  AGX_GPUVA_V5_REQUEST request = {0};
  AGX_GPUVA_V5_RESPONSE response = {0};
  NTSTATUS status;
  request.Command = AGX_GPUVA_V5_LEASE;
  request.ProcessId = owner + 1u;
  request.ProcessGeneration = 1u;
  request.Slot = B1_SLOT;
  if (!AdmissionB1BrokerCall(state, &request, &response) ||
      response.Token == 0ULL)
    return STATUS_DEVICE_HARDWARE_ERROR;
  state->LeaseOwner = owner + 1u;
  state->LeaseToken = response.Token;
  RtlZeroMemory(&request, sizeof(request));
  RtlZeroMemory(&response, sizeof(response));
  request.Command = AGX_GPUVA_V5_JOB_BEGIN;
  request.Slot = B1_SLOT;
  request.Token = state->LeaseToken;
  if (!AdmissionB1BrokerCall(state, &request, &response)) {
    AdmissionGpuvaB1RecordRetirement(context, owner,
        AdmissionB1RetireJobBegin, STATUS_DEVICE_BUSY,
        state->BrokerStatus, response.Receipt, response.Epoch);
    return STATUS_DEVICE_BUSY;
  }
  AdmissionGpuvaB1RecordRetirement(context, owner,
      AdmissionB1RetireJobBegin, STATUS_SUCCESS,
      state->BrokerStatus, response.Receipt, response.Epoch);
  state->JobInFlight = TRUE;
  status = AdmissionGpuvaB1RunFirmware(
      context, state->Owned[owner][5].CpuAddress,
      state->Owned[owner][5].DeviceAddress, B1_OUTPUT_VA, fence, owner);
  if (!NT_SUCCESS(status))
    return status; /* Submission ownership is uncertain; preserve all pages. */
  request.Command = AGX_GPUVA_V5_JOB_END;
  RtlZeroMemory(&response, sizeof(response));
  if (!AdmissionB1BrokerCall(state, &request, &response)) {
    AdmissionGpuvaB1RecordRetirement(context, owner,
        AdmissionB1RetireJobEnd, STATUS_DEVICE_BUSY,
        state->BrokerStatus, response.Receipt, response.Epoch);
    return STATUS_DEVICE_BUSY;
  }
  AdmissionGpuvaB1RecordRetirement(context, owner,
      AdmissionB1RetireJobEnd, STATUS_SUCCESS,
      state->BrokerStatus, response.Receipt, response.Epoch);
  state->JobInFlight = FALSE;
  request.Command = AGX_GPUVA_V5_RELEASE;
  RtlZeroMemory(&response, sizeof(response));
  if (!AdmissionB1BrokerCall(state, &request, &response)) {
    AdmissionGpuvaB1RecordRetirement(context, owner,
        AdmissionB1RetireRelease, STATUS_DEVICE_BUSY,
        state->BrokerStatus, response.Receipt, response.Epoch);
    return STATUS_DEVICE_BUSY;
  }
  AdmissionGpuvaB1RecordRetirement(context, owner,
      AdmissionB1RetireRelease, STATUS_SUCCESS,
      state->BrokerStatus, response.Receipt, response.Epoch);
  /* A successful RELEASE response follows slot clear and TLB invalidation. */
  AdmissionGpuvaB1RecordRetirement(context, owner,
      AdmissionB1RetireTlbAck, STATUS_SUCCESS,
      state->BrokerStatus, response.Receipt, response.Epoch);
  state->LeaseToken = 0ULL;
  state->LeaseOwner = 0u;
  ++state->CompletedJobs;
  return STATUS_SUCCESS;
}

static BOOLEAN AdmissionB1OutputsValid(const ADMISSION_B1_STATE *state) {
  const ULONG *a = (const ULONG *)state->Owned[0][5].CpuAddress;
  const ULONG *b = (const ULONG *)state->Owned[1][5].CpuAddress;
  ULONG i;
  if (a == NULL || b == NULL ||
      state->Ipa[0][5] == state->Ipa[1][5] ||
      state->Owned[0][5].DeviceAddress ==
          state->Owned[1][5].DeviceAddress)
    return FALSE;
  for (i = 0u; i < 16u * 16u; ++i)
    if (a[i] != APPLE_AGX_EXP208_GDI_COLOR ||
        b[i] != APPLE_AGX_EXP208_GDI_COLOR)
      return FALSE;
  return TRUE;
}

static BOOLEAN AdmissionB1Cleanup(ADMISSION_B1_STATE *state) {
  LONG owner;
  LONG page;
  if (state->LeaseToken || state->JobInFlight)
    return FALSE;
  if (state->Uncertain)
    return FALSE;
  for (owner = 1; owner >= 0; --owner)
    if (!AppleAgxGpuvaB1DestroyRoot(&state->Client,
                                    &state->Roots[owner]))
      return FALSE;
  for (owner = 1; owner >= 0; --owner)
    for (page = B1_OWNED_PAGES - 1; page >= 0; --page) {
      APPLE_AGX_MEMORY_OBJECT *object = &state->Owned[owner][page];
      if (object->AllocationHandle != NULL &&
          AppleAgxMemoryRelease(&state->Io, object) !=
              AppleAgxMemoryResultOk)
        return FALSE;
    }
  return TRUE;
}

_Use_decl_annotations_ NTSTATUS AdmissionGpuvaB1Qualify(
    ADMISSION_CONTEXT *context) {
  ADMISSION_B1_STATE *state;
  ADMISSION_BACKEND_MEMORY_VIEW backend;
  APPLE_AGX_GPUVA_B1_ROOT_INPUT input;
  AGX_GPUVA_V5_REQUEST probe = {0};
  AGX_GPUVA_V5_RESPONSE response = {0};
  APPLE_AGX_U32 count;
  NTSTATUS status = STATUS_DEVICE_HARDWARE_ERROR;
  ULONG owner, page;
  if (context == NULL || context->GpuvaB1State != NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  state = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*state), B1_TAG);
  if (state == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(state, sizeof(*state));
  context->GpuvaB1State = state;
  state->Precheck = 1u;
  status = AdmissionMemoryRuntimeBorrowIo(context, &state->Io);
  if (!NT_SUCCESS(status))
    goto Done;
  state->Precheck = 2u;
  status = AdmissionMemoryRuntimeBackendView(context, &backend);
  if (!NT_SUCCESS(status))
    goto Done;
  state->Precheck = 3u;
  if (backend.GuestIpaAddress == 0ULL ||
      (backend.GuestIpaAddress & (B1_OUTPUT_BYTES - 1u))) {
    status = STATUS_INVALID_ADDRESS;
    goto Done;
  }
  state->Precheck = 4u;
  if (!AdmissionGpuvaV5ClientOpen(context, &state->Client)) {
    status = STATUS_INVALID_DEVICE_STATE;
    goto Done;
  }
  state->Precheck = 5u;
  probe.Command = AGX_GPUVA_V5_CREATE;
  if (!AppleAgxGpuvaV5ClientCall(&state->Client, &probe, &response)) {
    status = STATUS_DEVICE_HARDWARE_ERROR;
    goto Done;
  }
  state->ProbeStatus = response.Status;
  state->ProbeEpoch = response.Epoch;
  state->Precheck = 6u;
  if (response.Status != 2u || response.Epoch == 0ULL) {
    status = STATUS_INVALID_DEVICE_STATE;
    goto Done;
  }
  state->Stage = 1u;
  for (owner = 0u; owner < 2u; ++owner)
    for (page = 0u; page < B1_OWNED_PAGES; ++page) {
      status = AdmissionB1AllocatePage(state, owner, page);
      if (!NT_SUCCESS(status))
        goto Done;
    }
  state->Stage = 2u;
  for (owner = 0u; owner < 2u; ++owner) {
    if (!AppleAgxGpuvaB1Graph(
            backend.GuestIpaAddress, state->Ipa[owner][5],
            state->Graph[owner], APPLE_AGX_GPUVA_B1_GRAPH_PAGES,
            &count) || count != APPLE_AGX_GPUVA_B1_GRAPH_PAGES) {
      status = STATUS_INVALID_ADDRESS;
      goto Done;
    }
    RtlZeroMemory(&input, sizeof(input));
    input.ProcessId = owner + 1u;
    input.Generation = 1u;
    input.RootIpa = state->Ipa[owner][0];
    input.L1Ipa = state->Ipa[owner][1];
    input.BackendL2Ipa = state->Ipa[owner][2];
    input.AliasL2Ipa = state->Ipa[owner][3];
    input.OutputL2Ipa = state->Ipa[owner][4];
    input.GraphGeneration = 17u;
    input.OutputGeneration = 27u + owner;
    input.Paging = owner;
    if (!AppleAgxGpuvaB1BuildRoot(
            &state->Client, &input, state->Graph[owner], count,
            &state->Roots[owner])) {
      state->BrokerStatus = state->Roots[owner].LastStatus;
      status = STATUS_DEVICE_HARDWARE_ERROR;
      goto Done;
    }
    state->Stage = 3u + owner;
  }
  RtlFillMemory(state->Owned[0][5].CpuAddress, B1_OUTPUT_BYTES, 0xa5u);
  RtlFillMemory(state->Owned[1][5].CpuAddress, B1_OUTPUT_BYTES, 0x5au);
  status = AdmissionB1LeaseJob(context, state, 0u, 1u);
  if (!NT_SUCCESS(status)) goto Done;
  state->Stage = 5u;
  if (*(ULONG *)state->Owned[0][5].CpuAddress !=
          APPLE_AGX_EXP208_GDI_COLOR) {
    status = STATUS_DEVICE_DATA_ERROR;
    goto Done;
  }
  {
    ULONG *untouched = (ULONG *)state->Owned[1][5].CpuAddress;
    ULONG i;
    for (i = 0u; i < B1_OUTPUT_BYTES / sizeof(ULONG); ++i)
      if (untouched[i] != 0x5a5a5a5au) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto Done;
      }
  }
  status = AdmissionB1LeaseJob(context, state, 1u, 2u);
  if (!NT_SUCCESS(status)) goto Done;
  state->Stage = 6u;
  status = AdmissionB1OutputsValid(state)
               ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
  state->Stage = 7u;
Done:
  {
    APPLE_AGX_GPUVA_B1_STATUS outcome;
    ULONG pixelA = state->Owned[0][5].CpuAddress != NULL ?
        *(ULONG *)state->Owned[0][5].CpuAddress : 0u;
    ULONG pixelB = state->Owned[1][5].CpuAddress != NULL ?
        *(ULONG *)state->Owned[1][5].CpuAddress : 0u;
    BOOLEAN cleaned = AdmissionB1Cleanup(state);
    outcome = AppleAgxGpuvaB1FinalStatus((ULONG)status, cleaned,
                                           (ULONG)STATUS_DEVICE_BUSY);
    AdmissionRecordB1Qualification(
        context, state->Stage, (NTSTATUS)outcome.Terminal,
        (NTSTATUS)outcome.FirstFailure,
        state->Precheck, state->ProbeStatus, state->ProbeEpoch,
        state->CompletedJobs, state->BrokerStatus,
        cleaned ? 0u : 1u, pixelA, pixelB);
    if (!cleaned)
      return (NTSTATUS)outcome.Terminal; /* Preserve owned storage. */
  }
  context->GpuvaB1State = NULL;
  ExFreePoolWithTag(state, B1_TAG);
  return status;
}
#endif
