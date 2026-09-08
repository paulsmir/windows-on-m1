#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "render_allocation.h"
#include "render_umd_command.h"
#include "render_win32_transport.h"
#include "agx_win32_transport.h"
#include "apple_agx_exp208_gdi.h"
#include "apple_agx_scanout.h"
#include "render_qualification.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

static ULONGLONG HashBytes(const void *Data, UINT Bytes) {
  const unsigned char *data = (const unsigned char *)Data;
  ULONGLONG hash = 14695981039346656037ULL;
  UINT index;
  for (index = 0u; index < Bytes; ++index) {
    hash ^= data[index];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static ULONGLONG HashUniformPixel(UINT Pixel, UINT Count) {
  ULONGLONG hash = 14695981039346656037ULL;
  UINT index;
  UINT byteIndex;
  for (index = 0u; index < Count; ++index) {
    for (byteIndex = 0u; byteIndex < 4u; ++byteIndex) {
      hash ^= (unsigned char)(Pixel >> (byteIndex * 8u));
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

static NTSTATUS QueryDeviceExecutionState(
    D3DKMT_HANDLE Device, const wchar_t *Point,
    D3DKMT_DEVICEEXECUTION_STATE *ExecutionState) {
  D3DKMT_GETDEVICESTATE deviceState = {0};
  NTSTATUS status;
  if (ExecutionState == NULL)
    return (NTSTATUS)0xc000000dL;
  deviceState.hDevice = Device;
  deviceState.StateType = D3DKMT_DEVICESTATE_EXECUTION;
  status = D3DKMTGetDeviceState(&deviceState);
  *ExecutionState = NT_SUCCESS(status)
      ? deviceState.ExecutionState : (D3DKMT_DEVICEEXECUTION_STATE)0;
  wprintf(L"DEVICE_STATE point=%ls status=0x%08lx execution=%u\n",
          Point, (ULONG)status, (UINT)*ExecutionState);
  return status;
}

static ADMISSION_PRESENT_WAIT_RESULT WaitForPresentation(
    D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device, D3DKMT_HANDLE Context,
    const ADMISSION_PRESENT_EXPECTATION *Expected,
    ADMISSION_PRESENT_QUERY *Result, NTSTATUS *QueryStatus) {
  ULONGLONG deadline = GetTickCount64() + 15000u;
  NTSTATUS status = (NTSTATUS)0x00000103L;
  ADMISSION_PRESENT_WAIT_RESULT waitResult = AdmissionPresentWaitTimedOut;
  BOOL sawInvalid = FALSE;
  if (Expected == NULL || Result == NULL || QueryStatus == NULL ||
      Expected->Index >= ADMISSION_PRESENT_QUERY_CAPACITY)
    return AdmissionPresentWaitInvalidRecord;
  do {
    D3DKMT_ESCAPE escape = {0};
    ZeroMemory(Result, sizeof(*Result));
    Result->Magic = ADMISSION_PRESENT_QUERY_MAGIC;
    Result->Version = ADMISSION_PRESENT_QUERY_VERSION;
    Result->Index = Expected->Index;
    escape.hAdapter = Adapter;
    escape.hDevice = Device;
    escape.hContext = Context;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = Result;
    escape.PrivateDriverDataSize = sizeof(*Result);
    status = D3DKMTEscape(&escape);
    if (!NT_SUCCESS(status)) {
      waitResult = AdmissionPresentWaitClassify(0, 0, 0, 0);
      break;
    }
    if (AdmissionPresentQueryAccept(Result, Expected)) {
      waitResult = AdmissionPresentWaitClassify(1, 1, 0, 0);
      break;
    }
    if (Result->Valid != 0u ||
        Result->PresentCount >= Expected->Index + 1u)
      sawInvalid = TRUE;
    Sleep(1u);
  } while (GetTickCount64() < deadline);
  if (waitResult != AdmissionPresentWaitCompleted && NT_SUCCESS(status))
    waitResult = AdmissionPresentWaitClassify(1, 0, sawInvalid, 1);
  *QueryStatus = status;
  wprintf(L"PRESENT_RESULT wait=%u index=%u query=0x%08lx build=%u boot=%u "
          L"count=%u purpose=%u destination=%u fence=%u allocation=0x%llx "
          L"status=0x%08x valid=%u color=0x%08x pixels=%u/%u "
          L"format=%u geometry=%ux%u/%u sequence=%llu offset=0x%llx "
          L"physical=0x%llx hash=0x%llx captured=%u published=%u "
          L"exported=%u durable=%u\n",
          waitResult, Expected->Index, (ULONG)status,
          Result->CandidateBuild, Result->BootGeneration,
          Result->PresentCount, Result->Purpose, Result->DestinationIndex,
          Result->Fence, Result->AllocationToken,
          Result->Status, Result->Valid, Result->ExpectedColor,
          Result->PixelsVerified, Result->PixelsExpected,
          Result->Format, Result->Width, Result->Height, Result->Pitch,
          Result->Sequence, Result->ActiveOffset,
          Result->PhysicalAddress, Result->ContentHash,
          Result->Captured, Result->PublishedToQuery,
          Result->Exported, Result->Durable);
  fflush(stdout);
  return waitResult;
}

static NTSTATUS QueryStandardPresentTrace(
    D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device, D3DKMT_HANDLE Context,
    ADMISSION_STANDARD_PRESENT_TRACE_COMMAND Command,
    ADMISSION_STANDARD_PRESENT_TRACE *Trace) {
  D3DKMT_ESCAPE escape = {0};
  if (Trace == NULL)
    return (NTSTATUS)0xc000000dL;
  AdmissionStandardPresentTraceInitialize(
      Trace, Command, 0u, 0u);
  escape.hAdapter = Adapter;
  escape.hDevice = Device;
  escape.hContext = Context;
  escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
  escape.pPrivateDriverData = Trace;
  escape.PrivateDriverDataSize = sizeof(*Trace);
  return D3DKMTEscape(&escape);
}

static void PrintStandardPresentTrace(
    const wchar_t *Point, const ADMISSION_STANDARD_PRESENT_TRACE *Trace,
    NTSTATUS Status) {
  unsigned int index;
  if (Trace == NULL)
    return;
  wprintf(L"STANDARD_TRACE point=%ls query=0x%08lx build=%u boot=%u "
          L"events=%u overflow=%u\n",
          Point, (ULONG)Status, Trace->CandidateBuild,
          Trace->BootGeneration, Trace->EventCount, Trace->Overflow);
  for (index = 0u; index < Trace->EventCount &&
                   index < ADMISSION_STANDARD_PRESENT_TRACE_CAPACITY;
       ++index) {
    const ADMISSION_STANDARD_PRESENT_EVENT *event = &Trace->Events[index];
    wprintf(L"STANDARD_EVENT index=%u valid=%u kind=%u phase=%u sequence=%u "
            L"status=0x%08x irql=%u flags=0x%x context=0x%llx "
            L"allocation=0x%llx source=%u segment=%u address=0x%llx "
            L"src_count=%u dst_count=%u\n",
            index, event->Valid, event->Kind, event->Phase, event->Sequence,
            event->Status, event->Irql, event->Flags, event->ContextToken,
            event->AllocationToken, event->SourceId, event->Segment,
            event->PrimaryAddress, event->NumSrc, event->NumDst);
  }
  fflush(stdout);
}

static NTSTATUS WaitForStandardPresent(
    D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device, D3DKMT_HANDLE Context,
    const ADMISSION_STANDARD_PRESENT_EXPECTATION *Expected,
    ADMISSION_STANDARD_PRESENT_TRACE *Trace) {
  ULONGLONG deadline = GetTickCount64() + 15000u;
  unsigned int presentSequence = 0u;
  unsigned int sourceAddressSequence = 0u;
  NTSTATUS status = (NTSTATUS)0x00000103L;
  do {
    status = QueryStandardPresentTrace(
        Adapter, Device, Context, AdmissionStandardPresentTraceRead, Trace);
    if (!NT_SUCCESS(status))
      break;
    if (AdmissionStandardPresentTraceAccept(
            Trace, Expected, &presentSequence, &sourceAddressSequence)) {
      wprintf(L"STANDARD_CORRELATION present_sequence=%u "
              L"source_address_sequence=%u\n",
              presentSequence, sourceAddressSequence);
      fflush(stdout);
      return (NTSTATUS)0;
    }
    Sleep(1u);
  } while (GetTickCount64() < deadline);
  PrintStandardPresentTrace(L"wait_failure", Trace, status);
  return NT_SUCCESS(status) ? (NTSTATUS)0xc00000b5L : status;
}

static NTSTATUS WaitForStandardPresentDdi(
    D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device, D3DKMT_HANDLE Context,
    const ADMISSION_STANDARD_PRESENT_EXPECTATION *Expected,
    ADMISSION_STANDARD_PRESENT_TRACE *Trace) {
  ULONGLONG deadline = GetTickCount64() + 15000u;
  unsigned int presentSequence = 0u;
  unsigned long long contextToken = 0ULL;
  unsigned long long allocationToken = 0ULL;
  NTSTATUS status = (NTSTATUS)0x00000103L;
  do {
    status = QueryStandardPresentTrace(
        Adapter, Device, Context, AdmissionStandardPresentTraceRead, Trace);
    if (!NT_SUCCESS(status))
      break;
    if (AdmissionStandardPresentTraceAcceptPresent(
            Trace, Expected, &presentSequence,
            &contextToken, &allocationToken)) {
      wprintf(L"STANDARD_DDI_CORRELATION present_sequence=%u "
              L"context=0x%llx allocation=0x%llx\n",
              presentSequence, contextToken, allocationToken);
      fflush(stdout);
      return (NTSTATUS)0;
    }
    Sleep(1u);
  } while (GetTickCount64() < deadline);
  PrintStandardPresentTrace(L"ddi_wait_failure", Trace, status);
  return NT_SUCCESS(status) ? (NTSTATUS)0xc00000b5L : status;
}

static LRESULT CALLBACK StandardPresentWindowProcedure(
    HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
  return DefWindowProcW(Window, Message, WParam, LParam);
}

static HWND CreateStandardPresentWindow(void) {
  static const wchar_t className[] = L"AppleAgxStandardPresentWindow";
  WNDCLASSEXW windowClass;
  HINSTANCE instance = GetModuleHandleW(NULL);
  HWND window;
  ZeroMemory(&windowClass, sizeof(windowClass));
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = StandardPresentWindowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
  windowClass.lpszClassName = className;
  if (RegisterClassExW(&windowClass) == 0u &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return NULL;
  window = CreateWindowExW(
      WS_EX_TOPMOST, className, L"Apple AGX standard BLT present",
      WS_POPUP | WS_VISIBLE, 0, 0,
      APPLE_AGX_SCANOUT_J313_WIDTH, APPLE_AGX_SCANOUT_J313_HEIGHT,
      NULL, NULL, instance, NULL);
  if (window != NULL) {
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
  }
  return window;
}

static void PumpStandardPresentWindow(DWORD DurationMs) {
  ULONGLONG deadline = GetTickCount64() + DurationMs;
  MSG message;
  while (GetTickCount64() < deadline) {
    while (PeekMessageW(&message, NULL, 0u, 0u, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(10u);
  }
}

static NTSTATUS SubmitStandardBlt(
    D3DKMT_HANDLE Context, D3DKMT_HANDLE Source, HWND Window) {
  D3DKMT_PRESENT present = {0};
  RECT rectangle = {0, 0, APPLE_AGX_SCANOUT_J313_WIDTH,
                     APPLE_AGX_SCANOUT_J313_HEIGHT};
  present.hContext = Context;
  present.hWindow = Window;
  present.VidPnSourceId = 0u;
  present.hSource = Source;
  present.hDestination = 0u;
  present.DstRect = rectangle;
  present.SrcRect = rectangle;
  present.SubRectCnt = 1u;
  present.pSrcSubRects = &rectangle;
  present.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
  present.Flags.Blt = 1u;
  present.Flags.SrcRectValid = 1u;
  present.Flags.DstRectValid = 1u;
  return D3DKMTPresent(&present);
}

static NTSTATUS SubmitStandardFlip(
    D3DKMT_HANDLE Context, D3DKMT_HANDLE Source) {
  D3DKMT_PRESENT present = {0};
  present.hContext = Context;
  present.hWindow = NULL;
  present.VidPnSourceId = 0u;
  present.hSource = Source;
  present.hDestination = 0u;
  present.FlipInterval = D3DDDI_FLIPINTERVAL_ONE;
  present.Flags.Flip = 1u;
  return D3DKMTPresent(&present);
}

int __cdecl wmain(int argc, wchar_t **argv) {
  D3DKMT_ENUMADAPTERS3 enumeration = {0};
  D3DKMT_ADAPTERINFO adapters[MAX_ENUM_ADAPTERS] = {0};
  D3DKMT_ADAPTERTYPE adapterType = {0};
  D3DKMT_QUERYADAPTERINFO query = {0};
  D3DKMT_CREATEDEVICE createDevice = {0};
  D3DKMT_CREATEPAGINGQUEUE createPagingQueue = {0};
  D3DKMT_CREATECONTEXT createContext = {0};
  D3DKMT_CREATECONTEXT secondContext = {0};
  D3DKMT_CREATEALLOCATION createAllocation = {0};
  D3DDDI_ALLOCATIONINFO allocationInfo[2] = {0};
  ADMISSION_ALLOCATION_DESCRIPTION allocation[2] = {0};
  ADMISSION_UMD_COLOR_FILL_COMMAND command = {0};
  ADMISSION_WIN32_CONTEXT_CREATE win32Context = {0};
  AGX_WIN32_CLEAR_REQUEST win32Clear = {0};
  D3DKMT_RENDER render = {0};
  D3DKMT_ESCAPE escape = {0};
  D3DKMT_TDRDBGCTRL_ESCAPE tdr = {0};
  D3DDDI_MAKERESIDENT makeResident = {0};
  D3DKMT_DESTROYALLOCATION2 destroy = {0};
  D3DKMT_DESTROYCONTEXT destroyContext = {0};
  D3DKMT_DESTROYDEVICE destroyDevice = {0};
  D3DDDI_DESTROYPAGINGQUEUE destroyPagingQueue = {0};
  D3DKMT_CLOSEADAPTER closeAdapter = {0};
  D3DKMT_SETVIDPNSOURCEOWNER sourceOwner = {0};
  D3DKMT_VIDPNSOURCEOWNER_TYPE sourceOwnerType =
      D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID sourceId = 0u;
  D3DKMT_SETDISPLAYMODE setDisplayMode = {0};
  ADMISSION_STANDARD_PRESENT_TRACE standardTrace = {0};
  ADMISSION_STANDARD_PRESENT_EXPECTATION standardExpected = {0};
  ADMISSION_STANDARD_PRESENT_PRODUCER_STATE standardProducer = {0};
  HWND standardWindow = NULL;
  DWORD processSession = 0xffffffffu;
  DWORD activeConsoleSession = 0xffffffffu;
  D3DKMT_HANDLE allocationHandles[2] = {0};
  PFND3DKMT_ENUMADAPTERS3 enumAdapters3 = NULL;
  HMODULE gdiModule = NULL;
  ULONG selectedAdapter = MAX_ENUM_ADAPTERS;
  ULONG matchingAdapters = 0u;
  ULONG index;
  ULONG pass;
  ULONG targetFrames = 2u;
  D3DKMT_DEVICEEXECUTION_STATE executionState;
  ADMISSION_PRESENT_QUERY presentation[ADMISSION_PRESENT_QUERY_CAPACITY] = {0};
  ADMISSION_PRESENT_EXPECTATION
      presentExpected[ADMISSION_PRESENT_QUERY_CAPACITY] = {0};
  ADMISSION_PRESENT_QUERY
      heldPresentation[ADMISSION_PRESENT_QUERY_CAPACITY] = {0};
  ADMISSION_PRESENT_PRODUCER_STATE producerState = {0};
  ADMISSION_RETIREMENT_QUERY retirement = {0};
  ADMISSION_RETIREMENT_EXPECTATION retirementExpected = {0};
  ADMISSION_PRESENT_PRODUCER_ACTION producerAction =
      AdmissionPresentProducerCleanup;
  ADMISSION_PRESENT_WAIT_RESULT presentWait = AdmissionPresentWaitTimedOut;
  NTSTATUS presentQueryStatus = (NTSTATUS)0xc0000001L;
  LUID selectedLuid = {0};
  ULONG selectedSources = 0u;
  UINT residencyPriority[2] = {
      D3DDDI_ALLOCATIONPRIORITY_NORMAL,
      D3DDDI_ALLOCATIONPRIORITY_NORMAL};
  NTSTATUS openStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS deviceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS pagingQueueStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS contextStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS allocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destinationAllocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS residentStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS renderStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS resetStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyAllocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyContextStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyDeviceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyPagingQueueStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS closeAdapterStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS sourceOwnerStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS setDisplayModeStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS standardPresentStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS standardTraceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS sourceOwnerReleaseStatus = (NTSTATUS)0xc0000001L;
  int result = 1;
  BOOL requestEngineTdr = FALSE;
  BOOL observeOnePass = FALSE;
  BOOL holdNoCleanup = FALSE;
  BOOL retireAfterSignal = FALSE;
  BOOL standardPresentMode = FALSE;
  BOOL standardBltMode = FALSE;
  BOOL win32TransportMode = FALSE;

  (void)setvbuf(stdout, NULL, _IONBF, 0);

  if (argc == 2 && wcscmp(argv[1], L"--engine-tdr") == 0)
    requestEngineTdr = TRUE;
  else if (argc == 2 && wcscmp(argv[1], L"--observe-one-pass") == 0)
    observeOnePass = TRUE;
  else if (argc == 2 && wcscmp(argv[1], L"--hold-no-cleanup") == 0)
    holdNoCleanup = TRUE;
  else if (argc == 2 && wcscmp(argv[1], L"--retire-after-signal") == 0) {
    holdNoCleanup = TRUE;
    retireAfterSignal = TRUE;
  }
  else if (argc == 2 &&
           wcscmp(argv[1], L"--repeat-retire-after-signal") == 0) {
    holdNoCleanup = TRUE;
    retireAfterSignal = TRUE;
    targetFrames = ADMISSION_PRESENT_QUERY_CAPACITY;
  }
  else if (argc == 2 &&
           wcscmp(argv[1], L"--standard-present-hold") == 0) {
    standardPresentMode = TRUE;
    targetFrames = 1u;
  }
  else if (argc == 2 &&
           wcscmp(argv[1], L"--standard-blt-present-hold") == 0) {
    standardBltMode = TRUE;
    targetFrames = 1u;
  }
  else if (argc == 2 &&
           wcscmp(argv[1], L"--win32-transport-two-frame") == 0) {
    win32TransportMode = TRUE;
    holdNoCleanup = TRUE;
    retireAfterSignal = TRUE;
    targetFrames = 2u;
  }
  else if (argc != 1) {
    fwprintf(stderr,
             L"usage: AppleAgxD3dKmRender.exe "
             L"[--engine-tdr|--observe-one-pass|--hold-no-cleanup|"
             L"--retire-after-signal|--repeat-retire-after-signal|"
             L"--standard-present-hold|--standard-blt-present-hold|"
             L"--win32-transport-two-frame]\n");
    return 2;
  }
  if (standardBltMode) {
    activeConsoleSession = WTSGetActiveConsoleSessionId();
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &processSession) ||
        processSession != activeConsoleSession) {
      wprintf(L"PHASE STANDARD_BLT_SESSION_REJECT process=%lu active=%lu\n",
              processSession, activeConsoleSession);
      return 3;
    }
  }
  AdmissionPresentProducerInitialize(
      &producerState, holdNoCleanup, targetFrames);
  AdmissionStandardPresentProducerInitialize(&standardProducer);
  gdiModule = GetModuleHandleW(L"gdi32.dll");
  if (gdiModule == NULL)
    goto cleanup;
  enumAdapters3 = (PFND3DKMT_ENUMADAPTERS3)GetProcAddress(
      gdiModule, "D3DKMTEnumAdapters3");
  if (enumAdapters3 == NULL)
    goto cleanup;
  enumeration.NumAdapters = ARRAYSIZE(adapters);
  enumeration.pAdapters = adapters;
  openStatus = enumAdapters3(&enumeration);
  if (!NT_SUCCESS(openStatus))
    goto cleanup;

  for (index = 0u; index < enumeration.NumAdapters; ++index) {
    NTSTATUS typeStatus;
    ZeroMemory(&adapterType, sizeof(adapterType));
    ZeroMemory(&query, sizeof(query));
    query.hAdapter = adapters[index].hAdapter;
    query.Type = KMTQAITYPE_ADAPTERTYPE;
    query.pPrivateDriverData = &adapterType;
    query.PrivateDriverDataSize = sizeof(adapterType);
    typeStatus = D3DKMTQueryAdapterInfo(&query);
    wprintf(L"ADAPTER index=%lu luid_high=%ld luid_low=%lu sources=%lu "
            L"query=0x%08lx type=0x%08lx\n",
            index, adapters[index].AdapterLuid.HighPart,
            adapters[index].AdapterLuid.LowPart,
            adapters[index].NumOfSources, (ULONG)typeStatus,
            adapterType.Value);
    if (NT_SUCCESS(typeStatus) &&
        adapterType.RenderSupported && adapterType.DisplaySupported &&
        adapterType.PostDevice && !adapterType.SoftwareDevice &&
        !adapterType.ComputeOnly) {
      selectedAdapter = index;
      ++matchingAdapters;
    }
  }
  if (matchingAdapters != 1u || selectedAdapter >= enumeration.NumAdapters)
    goto cleanup;
  selectedLuid = adapters[selectedAdapter].AdapterLuid;
  selectedSources = adapters[selectedAdapter].NumOfSources;

  createDevice.hAdapter = adapters[selectedAdapter].hAdapter;
  deviceStatus = D3DKMTCreateDevice(&createDevice);
  if (!NT_SUCCESS(deviceStatus))
    goto cleanup;

  createPagingQueue.hDevice = createDevice.hDevice;
  createPagingQueue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
  createPagingQueue.PhysicalAdapterIndex = 0u;
  pagingQueueStatus = D3DKMTCreatePagingQueue(&createPagingQueue);
  if (!NT_SUCCESS(pagingQueueStatus) ||
      createPagingQueue.hPagingQueue == 0u ||
      createPagingQueue.FenceValueCPUVirtualAddress == NULL)
    goto cleanup;

  createContext.hDevice = createDevice.hDevice;
  createContext.NodeOrdinal = 0u;
  createContext.EngineAffinity = 1u;
  createContext.Flags.Value = 0u;
  createContext.ClientHint = D3DKMT_CLIENTHINT_UNKNOWN;
  if (win32TransportMode) {
    win32Context.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;
    win32Context.Version = ADMISSION_WIN32_CONTEXT_VERSION;
    win32Context.Bytes = sizeof(win32Context);
    win32Context.Generation = 0x06490001u;
    createContext.pPrivateDriverData = &win32Context;
    createContext.PrivateDriverDataSize = sizeof(win32Context);
  }
  contextStatus = D3DKMTCreateContext(&createContext);
  if (!NT_SUCCESS(contextStatus) || createContext.hContext == 0u ||
      createContext.pCommandBuffer == NULL ||
      createContext.CommandBufferSize <
          (win32TransportMode ? 128u : sizeof(command)) ||
      createContext.pAllocationList == NULL ||
      createContext.AllocationListSize < 2u ||
      createContext.pPatchLocationList == NULL)
    goto cleanup;

  secondContext.hDevice = createDevice.hDevice;
  secondContext.NodeOrdinal = 0u;
  secondContext.EngineAffinity = 1u;
  secondContext.Flags.Value = 0u;
  secondContext.ClientHint = D3DKMT_CLIENTHINT_UNKNOWN;
  if (win32TransportMode) {
    secondContext.pPrivateDriverData = &win32Context;
    secondContext.PrivateDriverDataSize = sizeof(win32Context);
  }
  contextStatus = D3DKMTCreateContext(&secondContext);
  if (!NT_SUCCESS(contextStatus) || secondContext.hContext == 0u ||
      secondContext.pCommandBuffer == NULL ||
      secondContext.CommandBufferSize <
          (win32TransportMode ? 128u : sizeof(command)) ||
      secondContext.pAllocationList == NULL ||
      secondContext.AllocationListSize < 2u ||
      secondContext.pPatchLocationList == NULL)
    goto cleanup;

  if (!AdmissionAllocationDescribe(
          APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH,
          APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT, 4u,
          (unsigned int)D3DKMDT_GDISURFACE_TEXTURE,
          (unsigned int)D3DDDIFMT_A8R8G8B8, 0u, &allocation[0]) ||
      !AdmissionAllocationDescribe(
          APPLE_AGX_SCANOUT_J313_WIDTH,
          APPLE_AGX_SCANOUT_J313_HEIGHT, 4u,
          (unsigned int)D3DKMDT_GDISURFACE_TEXTURE,
          (unsigned int)D3DDDIFMT_A8R8G8B8, 0u, &allocation[1]))
    goto cleanup;
  allocation[0].Reserved = ADMISSION_UMD_CORRELATION_COOKIE;
  allocationInfo[0].pPrivateDriverData = &allocation[0];
  allocationInfo[0].PrivateDriverDataSize = sizeof(allocation[0]);
  allocationInfo[1].pPrivateDriverData = &allocation[1];
  allocationInfo[1].PrivateDriverDataSize = sizeof(allocation[1]);
  if (standardPresentMode) {
    allocationInfo[0].Flags.Primary = 1u;
    allocationInfo[0].VidPnSourceId = 0u;
    allocationInfo[1].Flags.Primary = 1u;
    allocationInfo[1].VidPnSourceId = 0u;
  }
  createAllocation.hDevice = createDevice.hDevice;
  createAllocation.NumAllocations = 1u;
  createAllocation.pAllocationInfo = &allocationInfo[0];
  allocationStatus = D3DKMTCreateAllocation(&createAllocation);
  if (!NT_SUCCESS(allocationStatus) || allocationInfo[0].hAllocation == 0u)
    goto cleanup;
  allocationHandles[0] = allocationInfo[0].hAllocation;

  ZeroMemory(&createAllocation, sizeof(createAllocation));
  createAllocation.hDevice = createDevice.hDevice;
  createAllocation.NumAllocations = 1u;
  createAllocation.pAllocationInfo = &allocationInfo[1];
  destinationAllocationStatus = D3DKMTCreateAllocation(&createAllocation);
  if (!NT_SUCCESS(destinationAllocationStatus) ||
      allocationInfo[1].hAllocation == 0u)
    goto cleanup;
  allocationHandles[1] = allocationInfo[1].hAllocation;

  makeResident.hPagingQueue = createPagingQueue.hPagingQueue;
  makeResident.NumAllocations = ARRAYSIZE(allocationHandles);
  makeResident.AllocationList = allocationHandles;
  makeResident.PriorityList = residencyPriority;
  residentStatus = D3DKMTMakeResident(&makeResident);
  if (!NT_SUCCESS(residentStatus) ||
      makeResident.NumAllocations != ARRAYSIZE(allocationHandles))
    goto cleanup;
  {
    ULONGLONG deadline = GetTickCount64() + 15000u;
    volatile const UINT64 *fence =
        (volatile const UINT64 *)createPagingQueue.FenceValueCPUVirtualAddress;
    while (*fence < makeResident.PagingFenceValue &&
           GetTickCount64() < deadline)
      Sleep(1u);
    if (*fence < makeResident.PagingFenceValue) {
      residentStatus = (NTSTATUS)0x00000102L;
      goto cleanup;
    }
  }

  if (standardPresentMode || standardBltMode) {
    standardTraceStatus = QueryStandardPresentTrace(
        adapters[selectedAdapter].hAdapter, createDevice.hDevice,
        createContext.hContext, AdmissionStandardPresentTraceArm,
        &standardTrace);
    PrintStandardPresentTrace(L"armed", &standardTrace, standardTraceStatus);
    if (!NT_SUCCESS(standardTraceStatus) ||
        standardTrace.CandidateBuild != ADMISSION_EXPECTED_CANDIDATE_BUILD ||
        standardTrace.BootGeneration == 0u)
      goto cleanup;
    standardExpected.CandidateBuild = ADMISSION_EXPECTED_CANDIDATE_BUILD;
    standardExpected.BootGeneration = standardTrace.BootGeneration;
    standardExpected.Flags = standardBltMode ? 0x1u : 0x4u;
    standardExpected.SourceId = 0u;
    standardExpected.Segment = 2u;
    standardExpected.NumSrc = 1u;
    standardExpected.NumDst = standardBltMode ? 1u : 0u;

    if (standardBltMode) {
      standardWindow = CreateStandardPresentWindow();
      wprintf(L"PHASE STANDARD_BLT_WINDOW hwnd=0x%llx process_session=%lu "
              L"active_session=%lu\n",
              (ULONGLONG)(ULONG_PTR)standardWindow,
              processSession, activeConsoleSession);
      fflush(stdout);
      if (standardWindow == NULL)
        goto cleanup;
    }

  }
  if (standardPresentMode) {
    sourceOwner.hDevice = createDevice.hDevice;
    sourceOwner.pType = &sourceOwnerType;
    sourceOwner.pVidPnSourceId = &sourceId;
    sourceOwner.VidPnSourceCount = 1u;
    wprintf(L"PHASE STANDARD_OWNER_ACQUIRE_BEGIN\n");
    sourceOwnerStatus = D3DKMTSetVidPnSourceOwner(&sourceOwner);
    wprintf(L"PHASE STANDARD_OWNER_ACQUIRE_END status=0x%08lx\n",
            (ULONG)sourceOwnerStatus);
    fflush(stdout);
    if (!NT_SUCCESS(sourceOwnerStatus) ||
        !AdmissionStandardPresentProducerAdvance(
            &standardProducer, AdmissionStandardPresentOwnerAcquired))
      goto cleanup;

    setDisplayMode.hDevice = createDevice.hDevice;
    setDisplayMode.hPrimaryAllocation = allocationHandles[0];
    setDisplayMode.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    setDisplayMode.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
    wprintf(L"PHASE STANDARD_MODE_SET_BEGIN allocation=%u\n",
            allocationHandles[0]);
    setDisplayModeStatus = D3DKMTSetDisplayMode(&setDisplayMode);
    wprintf(L"PHASE STANDARD_MODE_SET_END status=0x%08lx attribute=0x%x\n",
            (ULONG)setDisplayModeStatus,
            setDisplayMode.PrivateDriverFormatAttribute);
    fflush(stdout);
    if (!NT_SUCCESS(setDisplayModeStatus) ||
        !AdmissionStandardPresentProducerAdvance(
            &standardProducer, AdmissionStandardPresentModeSet))
      goto cleanup;
  }

  wprintf(L"BUFFERS device_command=%p device_command_bytes=%u "
          L"device_allocations=%p device_allocation_count=%u "
          L"device_patches=%p device_patch_count=%u "
          L"context_command=%p context_command_bytes=%u "
          L"context_allocations=%p context_allocation_count=%u "
          L"context_patches=%p context_patch_count=%u context_gpuva=0x%llx\n",
          createDevice.pCommandBuffer, createDevice.CommandBufferSize,
          createDevice.pAllocationList, createDevice.AllocationListSize,
          createDevice.pPatchLocationList, createDevice.PatchLocationListSize,
          createContext.pCommandBuffer, createContext.CommandBufferSize,
          createContext.pAllocationList, createContext.AllocationListSize,
          createContext.pPatchLocationList, createContext.PatchLocationListSize,
          createContext.CommandBuffer);
  for (pass = 0u; pass < targetFrames; ++pass) {
    D3DKMT_CREATECONTEXT *activeContext =
        (pass & 1u) == 0u ? &createContext : &secondContext;
    UINT commandOffset;
    UINT commandBytes;
    if (pass != 0u) {
      (void)QueryDeviceExecutionState(
          createDevice.hDevice, L"before_pass2", &executionState);
    }
    ZeroMemory(&command, sizeof(command));
    commandOffset = 0u;
    if (win32TransportMode) {
      ZeroMemory(&win32Clear, sizeof(win32Clear));
      win32Clear.Generation = win32Context.Generation;
      win32Clear.AllocationIndex = pass & 1u;
      win32Clear.AllocationBytes =
          APPLE_AGX_EXP208_FRAMEBUFFER_BYTES;
      win32Clear.Format = AppleAgxWin32FormatBgra8Unorm;
      win32Clear.Color = (pass & 1u) == 0u ?
          0xff00ff00u : 0xff0000ffu;
      win32Clear.SurfaceWidth = APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH;
      win32Clear.SurfaceHeight = APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT;
      win32Clear.SurfacePitch = APPLE_AGX_EXP208_FRAMEBUFFER_PITCH;
      win32Clear.Left = 0u;
      win32Clear.Top = (pass & 1u) == 0u ?
          0u : APPLE_AGX_EXP208_FRAMEBUFFER_BAND_TOP;
      win32Clear.Right = APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH;
      win32Clear.Bottom = APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT;
      if (AgxWin32TransportBuildClear(
              &win32Clear, activeContext->pCommandBuffer,
              activeContext->CommandBufferSize, &commandBytes) !=
          AppleAgxWin32AbiSuccess)
        goto cleanup;
      command.Magic = ADMISSION_UMD_COMMAND_MAGIC;
      command.Version = ADMISSION_UMD_COMMAND_VERSION;
      command.Bytes = sizeof(command);
      command.Opcode = AdmissionUmdOpcodeColorFill;
      command.Destination.Top = win32Clear.Top;
      command.Destination.Right = win32Clear.Right;
      command.Destination.Bottom = win32Clear.Bottom;
      command.DestinationAllocationIndex = win32Clear.AllocationIndex;
      command.Color = win32Clear.Color;
      command.Rop = AdmissionUmdRopPatCopy;
    } else {
      command.Magic = ADMISSION_UMD_COMMAND_MAGIC;
      command.Version = ADMISSION_UMD_COMMAND_VERSION;
      command.Bytes = sizeof(command);
      command.Opcode = AdmissionUmdOpcodeColorFill;
      command.Destination.Top = 0u;
      command.Destination.Right = APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH;
      command.Destination.Bottom = APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT;
      command.DestinationAllocationIndex =
          (standardPresentMode || standardBltMode) ? 1u : (pass & 1u);
      command.Color = (pass & 1u) == 0u
          ? APPLE_AGX_EXP208_FRAMEBUFFER_BASE_COLOR
          : APPLE_AGX_EXP208_FRAMEBUFFER_BAND_COLOR;
      command.Rop = AdmissionUmdRopPatCopy;
      if (activeContext->CommandBufferSize < sizeof(command))
        goto cleanup;
      CopyMemory(
          (unsigned char *)activeContext->pCommandBuffer + commandOffset,
          &command, sizeof(command));
      commandBytes = sizeof(command);
    }
    ZeroMemory(activeContext->pAllocationList,
               2u * sizeof(activeContext->pAllocationList[0]));
    activeContext->pAllocationList[0].hAllocation = allocationHandles[0];
    activeContext->pAllocationList[0].WriteOperation = 1u;
    activeContext->pAllocationList[1].hAllocation = allocationHandles[1];
    activeContext->pAllocationList[1].WriteOperation = 1u;
    ZeroMemory(activeContext->pPatchLocationList,
               sizeof(activeContext->pPatchLocationList[0]));
    ZeroMemory(&render, sizeof(render));
    render.hContext = activeContext->hContext;
    render.CommandOffset = commandOffset;
    render.CommandLength = commandBytes;
    render.AllocationCount = ARRAYSIZE(allocationHandles);
    render.PatchLocationCount = 0u;
    wprintf(L"PHASE FRAME%lu_RENDER_BEGIN\n", pass + 1u);
    fflush(stdout);
    wprintf(L"RENDER_IN pass=%lu context=%lu command_offset=%u "
            L"command_length=%u command_capacity=%u "
            L"command_hash=0x%016llx destination_index=%lu\n",
            pass, activeContext->hContext, commandOffset, commandBytes,
            activeContext->CommandBufferSize,
            HashBytes((unsigned char *)activeContext->pCommandBuffer +
                          commandOffset,
                      commandBytes),
            command.DestinationAllocationIndex);
    renderStatus = D3DKMTRender(&render);
    wprintf(L"RENDER_OUT pass=%lu command=%p command_bytes=%u allocations=%p "
            L"allocation_count=%u patches=%p patch_count=%u gpuva=0x%llx "
            L"queued=%u status=0x%08lx\n", pass, render.pNewCommandBuffer,
            render.NewCommandBufferSize, render.pNewAllocationList,
            render.NewAllocationListSize, render.pNewPatchLocationList,
            render.NewPatchLocationListSize, render.NewCommandBuffer,
            render.QueuedBufferCount, (ULONG)renderStatus);
    (void)QueryDeviceExecutionState(
        createDevice.hDevice,
        pass == 0u ? L"after_pass1" : L"after_pass2",
        &executionState);
    if (!NT_SUCCESS(renderStatus))
      goto cleanup;
    if (render.pNewCommandBuffer == NULL ||
        render.NewCommandBufferSize <
            (win32TransportMode ? 128u : sizeof(command)) ||
        render.pNewAllocationList == NULL ||
        render.NewAllocationListSize < 2u ||
        render.pNewPatchLocationList == NULL)
      goto cleanup;
    activeContext->pCommandBuffer = render.pNewCommandBuffer;
    activeContext->CommandBufferSize = render.NewCommandBufferSize;
    activeContext->pAllocationList = render.pNewAllocationList;
    activeContext->AllocationListSize = render.NewAllocationListSize;
    activeContext->pPatchLocationList = render.pNewPatchLocationList;
    activeContext->PatchLocationListSize = render.NewPatchLocationListSize;
    if (standardBltMode) {
      wprintf(L"PHASE STANDARD_BLT_PRESENT_BEGIN source=%u hwnd=0x%llx\n",
              allocationHandles[1], (ULONGLONG)(ULONG_PTR)standardWindow);
      standardPresentStatus = SubmitStandardBlt(
          activeContext->hContext, allocationHandles[1], standardWindow);
      wprintf(L"PHASE STANDARD_BLT_PRESENT_END status=0x%08lx\n",
              (ULONG)standardPresentStatus);
      fflush(stdout);
      if (!NT_SUCCESS(standardPresentStatus))
        goto cleanup;
      standardTraceStatus = WaitForStandardPresentDdi(
          adapters[selectedAdapter].hAdapter, createDevice.hDevice,
          activeContext->hContext, &standardExpected, &standardTrace);
      PrintStandardPresentTrace(
          L"windowed_blt", &standardTrace, standardTraceStatus);
      if (!NT_SUCCESS(standardTraceStatus))
        goto preserve_resources;
      PumpStandardPresentWindow(15000u);
      if (!NT_SUCCESS(QueryDeviceExecutionState(
              createDevice.hDevice, L"standard_blt_hold", &executionState)) ||
          executionState != D3DKMT_DEVICEEXECUTION_ACTIVE)
        goto preserve_resources;
      wprintf(L"PHASE STANDARD_BLT_HOLD_PASS duration_ms=15000\n");
      fflush(stdout);
      result = 0;
      goto cleanup;
    }
    if (standardPresentMode) {
      wprintf(L"PHASE STANDARD_PRESENT_BEGIN source=%u\n",
              allocationHandles[1]);
      standardPresentStatus = SubmitStandardFlip(
          activeContext->hContext, allocationHandles[1]);
      wprintf(L"PHASE STANDARD_PRESENT_END status=0x%08lx\n",
              (ULONG)standardPresentStatus);
      fflush(stdout);
      if (!NT_SUCCESS(standardPresentStatus))
        goto preserve_resources;
      standardTraceStatus = WaitForStandardPresent(
          adapters[selectedAdapter].hAdapter, createDevice.hDevice,
          activeContext->hContext, &standardExpected, &standardTrace);
      PrintStandardPresentTrace(
          L"render_present", &standardTrace, standardTraceStatus);
      if (!NT_SUCCESS(standardTraceStatus) ||
          !AdmissionStandardPresentProducerAdvance(
              &standardProducer, AdmissionStandardPresentFrameConfirmed))
        goto preserve_resources;
      wprintf(L"PHASE STANDARD_PRESENT_PASS boot=%u\n",
              standardTrace.BootGeneration);
      wprintf(L"PHASE STANDARD_PRESENT_HOLD "
              L"signal=C:\\Users\\pavel\\AppleAgx-standard-present-retire.go\n");
      fflush(stdout);
      while (GetFileAttributesW(
                 L"C:\\Users\\pavel\\AppleAgx-standard-present-retire.go") ==
             INVALID_FILE_ATTRIBUTES)
        Sleep(100u);

      standardTraceStatus = QueryStandardPresentTrace(
          adapters[selectedAdapter].hAdapter, createDevice.hDevice,
          activeContext->hContext, AdmissionStandardPresentTraceArm,
          &standardTrace);
      if (!NT_SUCCESS(standardTraceStatus))
        goto preserve_resources;
      standardExpected.BootGeneration = standardTrace.BootGeneration;
      wprintf(L"PHASE STANDARD_FLIPBACK_BEGIN source=%u\n",
              allocationHandles[0]);
      standardPresentStatus = SubmitStandardFlip(
          activeContext->hContext, allocationHandles[0]);
      wprintf(L"PHASE STANDARD_FLIPBACK_END status=0x%08lx\n",
              (ULONG)standardPresentStatus);
      fflush(stdout);
      if (!NT_SUCCESS(standardPresentStatus))
        goto preserve_resources;
      standardTraceStatus = WaitForStandardPresent(
          adapters[selectedAdapter].hAdapter, createDevice.hDevice,
          activeContext->hContext, &standardExpected, &standardTrace);
      PrintStandardPresentTrace(
          L"flip_back", &standardTrace, standardTraceStatus);
      if (!NT_SUCCESS(standardTraceStatus) ||
          !AdmissionStandardPresentProducerAdvance(
              &standardProducer, AdmissionStandardPresentFlipBackConfirmed))
        goto preserve_resources;

      ZeroMemory(&sourceOwner, sizeof(sourceOwner));
      sourceOwner.hDevice = createDevice.hDevice;
      wprintf(L"PHASE STANDARD_OWNER_RELEASE_BEGIN\n");
      sourceOwnerReleaseStatus = D3DKMTSetVidPnSourceOwner(&sourceOwner);
      wprintf(L"PHASE STANDARD_OWNER_RELEASE_END status=0x%08lx\n",
              (ULONG)sourceOwnerReleaseStatus);
      fflush(stdout);
      if (!NT_SUCCESS(sourceOwnerReleaseStatus) ||
          !AdmissionStandardPresentProducerAdvance(
              &standardProducer, AdmissionStandardPresentOwnerReleased))
        goto preserve_resources;
      Sleep(1000u);
      result = 0;
      goto cleanup;
    }
    ZeroMemory(&presentExpected[pass], sizeof(presentExpected[pass]));
    presentExpected[pass].CandidateBuild = ADMISSION_EXPECTED_CANDIDATE_BUILD;
    presentExpected[pass].BootGeneration =
        pass == 0u ? 0u : presentation[0].BootGeneration;
    presentExpected[pass].Index = pass;
    presentExpected[pass].DestinationIndex = pass & 1u;
    presentExpected[pass].ExpectedColor = command.Color;
    presentExpected[pass].PixelsExpected =
        APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH *
        ((win32TransportMode && (pass & 1u) != 0u)
             ? APPLE_AGX_EXP208_FRAMEBUFFER_BAND_HEIGHT
             : APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT);
    presentExpected[pass].Format = (unsigned int)D3DDDIFMT_A8R8G8B8;
    presentExpected[pass].Width = APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH;
    presentExpected[pass].Height = APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT;
    presentExpected[pass].Pitch = APPLE_AGX_EXP208_FRAMEBUFFER_PITCH;
    presentExpected[pass].ExpectedContentHash = HashUniformPixel(
        command.Color, presentExpected[pass].PixelsExpected);
    if (pass != 0u) {
      presentExpected[pass].PreviousFence = presentation[pass - 1u].Fence;
      presentExpected[pass].PreviousAllocationToken =
          presentation[pass - 1u].AllocationToken;
      presentExpected[pass].PreviousSequence =
          presentation[pass - 1u].Sequence;
      presentExpected[pass].PreviousActiveOffset =
          presentation[pass - 1u].ActiveOffset;
      presentExpected[pass].PreviousPhysicalAddress =
          presentation[pass - 1u].PhysicalAddress;
      presentExpected[pass].PreviousContentHash =
          presentation[pass - 1u].ContentHash;
    }
    if (pass >= 2u) {
      ULONG ownerIndex = pass & 1u;
      presentExpected[pass].ExpectedAllocationToken =
          presentation[ownerIndex].AllocationToken;
      presentExpected[pass].ExpectedActiveOffset =
          presentation[ownerIndex].ActiveOffset;
      presentExpected[pass].ExpectedPhysicalAddress =
          presentation[ownerIndex].PhysicalAddress;
    }
    presentWait = WaitForPresentation(
            adapters[selectedAdapter].hAdapter, createDevice.hDevice,
            activeContext->hContext, &presentExpected[pass],
            &presentation[pass], &presentQueryStatus);
    producerAction = AdmissionPresentProducerAfterWait(
        &producerState, presentWait);
    if (presentWait != AdmissionPresentWaitCompleted) {
      wprintf(L"PHASE FRAME%lu_FAIL wait=%u query=0x%08lx action=%u\n",
              pass + 1u, presentWait, (ULONG)presentQueryStatus,
              producerAction);
      fflush(stdout);
      if (producerAction == AdmissionPresentProducerPreserveForRecovery)
        goto preserve_resources;
      goto cleanup;
    }
    wprintf(L"PHASE FRAME%lu_PASS fence=%u sequence=%llu allocation=0x%llx\n",
            pass + 1u, presentation[pass].Fence,
            presentation[pass].Sequence,
            presentation[pass].AllocationToken);
    fflush(stdout);
    if ((pass + 1u < targetFrames &&
         producerAction != AdmissionPresentProducerSubmitNextFrame) ||
        (pass + 1u == targetFrames && holdNoCleanup &&
         producerAction != AdmissionPresentProducerBeginHold) ||
        (pass + 1u == targetFrames && !holdNoCleanup &&
         producerAction != AdmissionPresentProducerCleanup))
      goto preserve_resources;
    if (observeOnePass && pass == 0u) {
      static const DWORD delays[] = {250u, 250u, 500u, 1000u,
                                     3000u, 5000u, 5000u};
      static const wchar_t *points[] = {
          L"pass1_250ms", L"pass1_500ms", L"pass1_1s", L"pass1_2s",
          L"pass1_5s", L"pass1_10s", L"pass1_15s"};
      ULONG sample;
      for (sample = 0u; sample < ARRAYSIZE(delays); ++sample) {
        Sleep(delays[sample]);
        (void)QueryDeviceExecutionState(
            createDevice.hDevice, points[sample], &executionState);
      }
      break;
    }
  }
  if (holdNoCleanup) {
    ULONG holdSample;
    NTSTATUS heldQueryStatus = (NTSTATUS)0xc0000001L;
    wprintf(L"PHASE HOLD_BEGIN duration_ms=15000\n");
    fflush(stdout);
    for (holdSample = 0u; holdSample < 15u; ++holdSample)
      Sleep(1000u);
    for (pass = 0u; pass < targetFrames; ++pass) {
      if (WaitForPresentation(
              adapters[selectedAdapter].hAdapter, createDevice.hDevice,
              (pass & 1u) == 0u
                  ? createContext.hContext
                  : secondContext.hContext,
              &presentExpected[pass], &heldPresentation[pass],
              &heldQueryStatus) != AdmissionPresentWaitCompleted ||
          memcmp(&heldPresentation[pass], &presentation[pass],
                 sizeof(presentation[pass])) != 0) {
        wprintf(L"PHASE HOLD_FAIL index=%lu query=0x%08lx\n",
                pass, (ULONG)heldQueryStatus);
        fflush(stdout);
        goto preserve_resources;
      }
    }
    if (!NT_SUCCESS(QueryDeviceExecutionState(
            createDevice.hDevice, L"hold_15s", &executionState)) ||
        executionState != D3DKMT_DEVICEEXECUTION_ACTIVE) {
      wprintf(L"PHASE HOLD_FAIL device_execution=%u\n", executionState);
      fflush(stdout);
      goto preserve_resources;
    }
    wprintf(L"PHASE HOLD_PASS duration_ms=15000 frames=%lu "
            L"fence_first=%u fence_last=%u sequence_first=%llu "
            L"sequence_last=%llu\n",
            targetFrames, presentation[0].Fence,
            presentation[targetFrames - 1u].Fence,
            presentation[0].Sequence,
            presentation[targetFrames - 1u].Sequence);
    fflush(stdout);
    if (!retireAfterSignal)
      goto preserve_resources;
    wprintf(L"PHASE RETIRE_WAIT signal=C:\\Users\\pavel\\AppleAgx-retire.go\n");
    fflush(stdout);
    while (GetFileAttributesW(L"C:\\Users\\pavel\\AppleAgx-retire.go") ==
           INVALID_FILE_ATTRIBUTES)
      Sleep(100u);
    wprintf(L"PHASE RETIRE_BEGIN previous_sequence=%llu\n",
            presentation[targetFrames - 1u].Sequence);
    fflush(stdout);
    retirement.Magic = ADMISSION_RETIREMENT_QUERY_MAGIC;
    retirement.Version = ADMISSION_RETIREMENT_QUERY_VERSION;
    retirement.Command = AdmissionRetirementCommandExecute;
    ZeroMemory(&escape, sizeof(escape));
    escape.hAdapter = adapters[selectedAdapter].hAdapter;
    escape.hDevice = createDevice.hDevice;
    escape.hContext = secondContext.hContext;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &retirement;
    escape.PrivateDriverDataSize = sizeof(retirement);
    presentQueryStatus = D3DKMTEscape(&escape);
    retirementExpected.CandidateBuild = ADMISSION_EXPECTED_CANDIDATE_BUILD;
    retirementExpected.BootGeneration =
        presentation[targetFrames - 1u].BootGeneration;
    retirementExpected.PreviousSequence =
        presentation[targetFrames - 1u].Sequence;
    retirementExpected.ExpectedPoolPhysical =
        presentation[targetFrames - 1u].PhysicalAddress -
        presentation[targetFrames - 1u].ActiveOffset;
    retirementExpected.RenderAllocation0 = presentation[0].AllocationToken;
    retirementExpected.RenderAllocation1 = presentation[1].AllocationToken;
    wprintf(L"RETIRE_RESULT query=0x%08lx build=%u boot=%u command=%u "
            L"status=0x%08x valid=%u purpose=%u sequence=%llu "
            L"allocation=0x%llx offset=0x%llx physical=0x%llx\n",
            (ULONG)presentQueryStatus, retirement.CandidateBuild,
            retirement.BootGeneration, retirement.Command, retirement.Status,
            retirement.Valid, retirement.Purpose, retirement.Sequence,
            retirement.AllocationToken, retirement.ActiveOffset,
            retirement.PhysicalAddress);
    fflush(stdout);
    if (!NT_SUCCESS(presentQueryStatus) ||
        !AdmissionRetirementQueryAccept(
            &retirement, &retirementExpected) ||
        !AdmissionPresentProducerRetirementComplete(&producerState)) {
      wprintf(L"PHASE RETIRE_FAIL\n");
      fflush(stdout);
      goto preserve_resources;
    }
    wprintf(L"PHASE RETIRE_PASS sequence=%llu allocation=0x%llx\n",
            retirement.Sequence, retirement.AllocationToken);
    fflush(stdout);
    result = 0;
    goto cleanup;
  }
  if (requestEngineTdr) {
    tdr.TdrControl = D3DKMT_TDRDBGCTRLTYPE_ENGINETDR;
    tdr.NodeOrdinal = 0u;
    escape.hAdapter = adapters[selectedAdapter].hAdapter;
    escape.hDevice = createDevice.hDevice;
    escape.hContext = secondContext.hContext;
    escape.Type = D3DKMT_ESCAPE_TDRDBGCTRL;
    escape.pPrivateDriverData = &tdr;
    escape.PrivateDriverDataSize = sizeof(tdr);
    resetStatus = D3DKMTEscape(&escape);
    wprintf(L"ENGINE_TDR status=0x%08lx node=%lu\n",
            (ULONG)resetStatus, tdr.NodeOrdinal);
    if (!NT_SUCCESS(resetStatus))
      goto cleanup;
  }
  result = 0;
  goto cleanup;

preserve_resources:
  for (;;)
    Sleep(1000u);

cleanup:
  if (standardPresentMode && standardProducer.OwnerAcquired != 0u &&
      standardProducer.OwnerReleased == 0u &&
      standardProducer.FrameConfirmed == 0u) {
    ZeroMemory(&sourceOwner, sizeof(sourceOwner));
    sourceOwner.hDevice = createDevice.hDevice;
    sourceOwnerReleaseStatus = D3DKMTSetVidPnSourceOwner(&sourceOwner);
    if (NT_SUCCESS(sourceOwnerReleaseStatus))
      (void)AdmissionStandardPresentProducerAdvance(
          &standardProducer, AdmissionStandardPresentOwnerReleased);
  }
  if (standardPresentMode &&
      !AdmissionStandardPresentProducerCanCleanup(&standardProducer)) {
    wprintf(L"PHASE STANDARD_PRESERVE owner=%u mode=%u frame=%u "
            L"flipback=%u released=%u\n",
            standardProducer.OwnerAcquired, standardProducer.ModeSet,
            standardProducer.FrameConfirmed,
            standardProducer.FlipBackConfirmed,
            standardProducer.OwnerReleased);
    fflush(stdout);
    goto preserve_resources;
  }
  if (!AdmissionPresentProducerCanCleanup(
          &producerState, allocationHandles[0] != 0u)) {
    wprintf(L"PHASE PRESERVE_AFTER_ERROR render=0x%08lx completed=%u "
            L"cleanup_allowed=%u\n", (ULONG)renderStatus,
            producerState.CompletedFrames, producerState.CleanupAllowed);
    fflush(stdout);
    goto preserve_resources;
  }
  if (standardWindow != NULL) {
    DestroyWindow(standardWindow);
    standardWindow = NULL;
  }
  if (allocationHandles[0] != 0u) {
    wprintf(L"PHASE DESTROY_ALLOCATION_BEGIN allowed=%u\n",
            producerState.CleanupAllowed);
    fflush(stdout);
    destroy.hDevice = createDevice.hDevice;
    destroy.phAllocationList = allocationHandles;
    destroy.AllocationCount =
        allocationHandles[1] != 0u ? 2u : 1u;
    destroy.Flags.AssumeNotInUse = 0;
    destroy.Flags.SynchronousDestroy = 1;
    destroyAllocationStatus = D3DKMTDestroyAllocation2(&destroy);
    wprintf(L"PHASE DESTROY_ALLOCATION_END status=0x%08lx\n",
            (ULONG)destroyAllocationStatus);
    fflush(stdout);
    if (!NT_SUCCESS(destroyAllocationStatus))
      result = 1;
  }
  if (createContext.hContext != 0u) {
    if (secondContext.hContext != 0u) {
      ZeroMemory(&destroyContext, sizeof(destroyContext));
      destroyContext.hContext = secondContext.hContext;
      destroyContextStatus = D3DKMTDestroyContext(&destroyContext);
      if (!NT_SUCCESS(destroyContextStatus))
        result = 1;
    }
    ZeroMemory(&destroyContext, sizeof(destroyContext));
    destroyContext.hContext = createContext.hContext;
    destroyContextStatus = D3DKMTDestroyContext(&destroyContext);
    if (!NT_SUCCESS(destroyContextStatus))
      result = 1;
  }
  if (createPagingQueue.hPagingQueue != 0u) {
    destroyPagingQueue.hPagingQueue = createPagingQueue.hPagingQueue;
    destroyPagingQueueStatus = D3DKMTDestroyPagingQueue(&destroyPagingQueue);
    if (!NT_SUCCESS(destroyPagingQueueStatus))
      result = 1;
  }
  if (createDevice.hDevice != 0u) {
    destroyDevice.hDevice = createDevice.hDevice;
    destroyDeviceStatus = D3DKMTDestroyDevice(&destroyDevice);
    if (!NT_SUCCESS(destroyDeviceStatus))
      result = 1;
  }
  for (index = 0u; index < enumeration.NumAdapters; ++index) {
    if (adapters[index].hAdapter != 0u) {
      closeAdapter.hAdapter = adapters[index].hAdapter;
      closeAdapterStatus = D3DKMTCloseAdapter(&closeAdapter);
      if (!NT_SUCCESS(closeAdapterStatus))
        result = 1;
      adapters[index].hAdapter = 0u;
    }
  }
  wprintf(L"{\"enumerated\":%lu,\"matching\":%lu,"
          L"\"luid_high\":%ld,\"luid_low\":%lu,\"sources\":%lu,"
          L"\"open\":\"0x%08lx\",\"device\":\"0x%08lx\","
          L"\"paging_queue\":\"0x%08lx\","
          L"\"context\":\"0x%08lx\",\"allocation\":\"0x%08lx\","
          L"\"destination_allocation\":\"0x%08lx\","
          L"\"resident\":\"0x%08lx\",\"paging_fence\":%llu,"
          L"\"render\":\"0x%08lx\",\"queued\":%u,"
          L"\"engine_tdr\":\"0x%08lx\","
          L"\"source_owner\":\"0x%08lx\","
          L"\"set_display_mode\":\"0x%08lx\","
          L"\"standard_present\":\"0x%08lx\","
          L"\"standard_trace\":\"0x%08lx\","
          L"\"source_owner_release\":\"0x%08lx\","
          L"\"destroy_allocation\":\"0x%08lx\","
          L"\"destroy_context\":\"0x%08lx\","
          L"\"destroy_paging_queue\":\"0x%08lx\","
          L"\"destroy_device\":\"0x%08lx\","
          L"\"close_adapter\":\"0x%08lx\",\"result\":%d}\n",
          enumeration.NumAdapters, matchingAdapters,
          selectedLuid.HighPart, selectedLuid.LowPart, selectedSources,
          (ULONG)openStatus, (ULONG)deviceStatus, (ULONG)pagingQueueStatus,
          (ULONG)contextStatus, (ULONG)allocationStatus,
          (ULONG)destinationAllocationStatus,
          (ULONG)residentStatus, makeResident.PagingFenceValue,
          (ULONG)renderStatus, render.QueuedBufferCount,
          (ULONG)resetStatus,
          (ULONG)sourceOwnerStatus, (ULONG)setDisplayModeStatus,
          (ULONG)standardPresentStatus, (ULONG)standardTraceStatus,
          (ULONG)sourceOwnerReleaseStatus,
          (ULONG)destroyAllocationStatus,
          (ULONG)destroyContextStatus, (ULONG)destroyPagingQueueStatus,
          (ULONG)destroyDeviceStatus,
          (ULONG)closeAdapterStatus, result);
  return result;
}
