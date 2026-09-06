#ifndef APPLE_AGX_RENDER_SUBMIT_TRACE_H
#define APPLE_AGX_RENDER_SUBMIT_TRACE_H

#define ADMISSION_SUBMIT_TRACE_TAG 0x5070000000000000ULL
#define ADMISSION_GDI_SUBMIT_TRACE_TAG 0x5090000000000000ULL
#define ADMISSION_UMD_RENDER_TRACE_TAG 0x5120000000000000ULL
#define ADMISSION_OPEN_ALLOCATION_TRACE_TAG 0x5130000000000000ULL
#define ADMISSION_PAGING_BUILD_TRACE_TAG 0x5140000000000000ULL
#define ADMISSION_SUBMIT_RENDER_GUARD_TAG 0x5280000000000000ULL
#define ADMISSION_SUBMIT_FENCE_DETAIL_TAG 0x5320000000000000ULL
#define ADMISSION_PATCH_RENDER_GUARD_TAG 0x5330000000000000ULL
#define ADMISSION_PREPATCH_ADOPT_GUARD_TAG 0x5350000000000000ULL
#define ADMISSION_SUBMIT_PACKET_GUARD_TAG 0x5390000000000000ULL
#define ADMISSION_SUBMIT_TRACE_FIELD_SHIFT 32u
#define ADMISSION_SUBMIT_TRACE_FIELD_MASK 0xffffu

typedef enum _ADMISSION_SUBMIT_TRACE_FIELD {
  AdmissionSubmitTraceVersion = 1u,
  AdmissionSubmitTraceIrql = 2u,
  AdmissionSubmitTraceArgsLow = 3u,
  AdmissionSubmitTraceArgsHigh = 4u,
  AdmissionSubmitTraceFlags = 5u,
  AdmissionSubmitTraceNode = 6u,
  AdmissionSubmitTraceEngine = 7u,
  AdmissionSubmitTraceFence = 8u,
  AdmissionSubmitTraceContextLow = 9u,
  AdmissionSubmitTraceContextHigh = 10u,
  AdmissionSubmitTraceDmaSegment = 11u,
  AdmissionSubmitTraceDmaPhysicalLow = 12u,
  AdmissionSubmitTraceDmaPhysicalHigh = 13u,
  AdmissionSubmitTraceDmaSize = 14u,
  AdmissionSubmitTraceDmaStart = 15u,
  AdmissionSubmitTraceDmaEnd = 16u,
  AdmissionSubmitTracePrivateLow = 17u,
  AdmissionSubmitTracePrivateHigh = 18u,
  AdmissionSubmitTracePrivateSize = 19u,
  AdmissionSubmitTracePrivateStart = 20u,
  AdmissionSubmitTracePrivateEnd = 21u,
  AdmissionSubmitTraceDmaVirtualLow = 22u,
  AdmissionSubmitTraceDmaVirtualHigh = 23u,
  AdmissionSubmitTraceVidPnSource = 24u,
  AdmissionSubmitTraceFlipInterval = 25u,
  AdmissionSubmitTracePrivateStage = 26u,
  AdmissionSubmitTraceCommandContextLow = 27u,
  AdmissionSubmitTraceCommandContextHigh = 28u,
  AdmissionSubmitTraceSourceLocationLow = 29u,
  AdmissionSubmitTraceSourceLocationHigh = 30u,
  AdmissionSubmitTraceDestinationLocationLow = 31u,
  AdmissionSubmitTraceDestinationLocationHigh = 32u,
  AdmissionSubmitTraceRoute = 39u,
  AdmissionSubmitTracePresentGuard = 40u,
  AdmissionSubmitTraceStatus = 41u,
} ADMISSION_SUBMIT_TRACE_FIELD;

typedef enum _ADMISSION_UMD_RENDER_TRACE_FIELD {
  AdmissionUmdRenderTraceVersion = 1u,
  AdmissionUmdRenderTraceIrql = 2u,
  AdmissionUmdRenderTraceContextFlags = 3u,
  AdmissionUmdRenderTraceCommandLength = 4u,
  AdmissionUmdRenderTraceDmaSize = 5u,
  AdmissionUmdRenderTracePrivateSize = 6u,
  AdmissionUmdRenderTraceAllocationCount = 7u,
  AdmissionUmdRenderTracePatchInCount = 8u,
  AdmissionUmdRenderTracePatchOutCount = 9u,
  AdmissionUmdRenderTraceMultipass = 10u,
  AdmissionUmdRenderTraceCommandMagic = 11u,
  AdmissionUmdRenderTraceCommandVersion = 12u,
  AdmissionUmdRenderTraceCommandBytes = 13u,
  AdmissionUmdRenderTraceCommandOpcode = 14u,
  AdmissionUmdRenderTraceDestinationIndex = 15u,
  AdmissionUmdRenderTraceColor = 16u,
  AdmissionUmdRenderTraceRop = 17u,
  AdmissionUmdRenderTraceRop3 = 18u,
  AdmissionUmdRenderTraceGuard = 19u,
  AdmissionUmdRenderTraceStatus = 20u,
} ADMISSION_UMD_RENDER_TRACE_FIELD;

typedef enum _ADMISSION_UMD_RENDER_GUARD {
  AdmissionUmdRenderGuardAccepted = 0u,
  AdmissionUmdRenderGuardContext = 1u,
  AdmissionUmdRenderGuardDevice = 2u,
  AdmissionUmdRenderGuardSystem = 3u,
  AdmissionUmdRenderGuardInactive = 4u,
  AdmissionUmdRenderGuardArgs = 5u,
  AdmissionUmdRenderGuardCommandPointer = 6u,
  AdmissionUmdRenderGuardCommandLength = 7u,
  AdmissionUmdRenderGuardDma = 8u,
  AdmissionUmdRenderGuardPrivate = 9u,
  AdmissionUmdRenderGuardAllocations = 10u,
  AdmissionUmdRenderGuardPatchIn = 11u,
  AdmissionUmdRenderGuardPatchOut = 12u,
  AdmissionUmdRenderGuardMultipass = 13u,
  AdmissionUmdRenderGuardUserCopy = 14u,
  AdmissionUmdRenderGuardCommand = 15u,
  AdmissionUmdRenderGuardOpenedAllocation = 16u,
  AdmissionUmdRenderGuardBounds = 17u,
  AdmissionUmdRenderGuardPrivateVirgin = 18u,
  AdmissionUmdRenderGuardPrepare = 19u,
  AdmissionUmdRenderGuardShadow = 20u,
} ADMISSION_UMD_RENDER_GUARD;

typedef enum _ADMISSION_SUBMIT_RENDER_GUARD {
  AdmissionSubmitRenderGuardAccepted = 0u,
  AdmissionSubmitRenderGuardArgs = 1u,
  AdmissionSubmitRenderGuardStarted = 2u,
  AdmissionSubmitRenderGuardRuntime = 3u,
  AdmissionSubmitRenderGuardFlags = 4u,
  AdmissionSubmitRenderGuardFenceArgument = 5u,
  AdmissionSubmitRenderGuardNodeArgument = 6u,
  AdmissionSubmitRenderGuardEngineArgument = 7u,
  AdmissionSubmitRenderGuardContextArgument = 8u,
  AdmissionSubmitRenderGuardPrivateArgument = 9u,
  AdmissionSubmitRenderGuardPrivateSize = 10u,
  AdmissionSubmitRenderGuardPrivateRange = 11u,
  AdmissionSubmitRenderGuardDmaRange = 12u,
  AdmissionSubmitRenderGuardContextMagic = 13u,
  AdmissionSubmitRenderGuardContextDevice = 14u,
  AdmissionSubmitRenderGuardAdapter = 15u,
  AdmissionSubmitRenderGuardSystem = 16u,
  AdmissionSubmitRenderGuardSchedulerInactive = 17u,
  AdmissionSubmitRenderGuardNode = 18u,
  AdmissionSubmitRenderGuardEngine = 19u,
  AdmissionSubmitRenderGuardFence = 20u,
  AdmissionSubmitRenderGuardShadow = 21u,
  AdmissionSubmitRenderGuardPacket = 22u,
} ADMISSION_SUBMIT_RENDER_GUARD;

typedef enum _ADMISSION_PATCH_RENDER_GUARD {
  AdmissionPatchRenderGuardAccepted = 0u,
  AdmissionPatchRenderGuardArguments = 1u,
  AdmissionPatchRenderGuardContext = 2u,
  AdmissionPatchRenderGuardShadow = 3u,
  AdmissionPatchRenderGuardLocation = 4u,
  AdmissionPatchRenderGuardTranslate = 5u,
  AdmissionPatchRenderGuardSeal = 6u,
  AdmissionPatchRenderGuardPrepare = 7u,
} ADMISSION_PATCH_RENDER_GUARD;

typedef enum _ADMISSION_PREPATCH_ADOPT_GUARD {
  AdmissionPrepatchAdoptGuardAccepted = 0u,
  AdmissionPrepatchAdoptGuardArguments = 1u,
  AdmissionPrepatchAdoptGuardPending = 2u,
  AdmissionPrepatchAdoptGuardShadow = 3u,
  AdmissionPrepatchAdoptGuardPrepare = 4u,
} ADMISSION_PREPATCH_ADOPT_GUARD;

typedef enum _ADMISSION_SUBMIT_PACKET_GUARD {
  AdmissionSubmitPacketGuardAccepted = 0u,
  AdmissionSubmitPacketGuardState = 1u,
  AdmissionSubmitPacketGuardFence = 2u,
  AdmissionSubmitPacketGuardContext = 3u,
  AdmissionSubmitPacketGuardPrivate = 4u,
  AdmissionSubmitPacketGuardPrivateEnd = 5u,
  AdmissionSubmitPacketGuardDmaStart = 6u,
  AdmissionSubmitPacketGuardDmaEnd = 7u,
  AdmissionSubmitPacketGuardBind = 8u,
  AdmissionSubmitPacketGuardScheduler = 9u,
  AdmissionSubmitPacketGuardQueue = 10u,
} ADMISSION_SUBMIT_PACKET_GUARD;

typedef enum _ADMISSION_OPEN_ALLOCATION_TRACE_FIELD {
  AdmissionOpenAllocationTraceVersion = 1u,
  AdmissionOpenAllocationTraceIrql = 2u,
  AdmissionOpenAllocationTraceDeviceFlags = 3u,
  AdmissionOpenAllocationTraceCount = 4u,
  AdmissionOpenAllocationTraceFlags = 5u,
  AdmissionOpenAllocationTraceSubresource = 6u,
  AdmissionOpenAllocationTraceHandle = 7u,
  AdmissionOpenAllocationTracePrivateSize = 8u,
  AdmissionOpenAllocationTraceDeviceSpecificPresent = 9u,
  AdmissionOpenAllocationTraceGuard = 10u,
  AdmissionOpenAllocationTraceStatus = 11u,
} ADMISSION_OPEN_ALLOCATION_TRACE_FIELD;

typedef enum _ADMISSION_OPEN_ALLOCATION_GUARD {
  AdmissionOpenAllocationGuardAccepted = 0u,
  AdmissionOpenAllocationGuardDevice = 1u,
  AdmissionOpenAllocationGuardArgs = 2u,
  AdmissionOpenAllocationGuardInterface = 3u,
  AdmissionOpenAllocationGuardPrivate = 4u,
  AdmissionOpenAllocationGuardAcquire = 5u,
  AdmissionOpenAllocationGuardDescription = 6u,
  AdmissionOpenAllocationGuardPool = 7u,
} ADMISSION_OPEN_ALLOCATION_GUARD;

typedef enum _ADMISSION_SUBMIT_TRACE_ROUTE {
  AdmissionSubmitRoutePresent = 1u,
  AdmissionSubmitRouteRender = 2u,
  AdmissionSubmitRoutePaging = 3u,
} ADMISSION_SUBMIT_TRACE_ROUTE;

typedef enum _ADMISSION_PRESENT_PRIVATE_STAGE {
  AdmissionPresentPrivateValid = 0u,
  AdmissionPresentPrivateMissing = 1u,
  AdmissionPresentPrivateShadow = 2u,
  AdmissionPresentPrivateExtent = 3u,
  AdmissionPresentPrivateRecord = 4u,
  AdmissionPresentPrivateCommand = 5u,
} ADMISSION_PRESENT_PRIVATE_STAGE;

typedef enum _ADMISSION_PRESENT_SUBMIT_GUARD {
  AdmissionPresentSubmitAccepted = 0u,
  AdmissionPresentSubmitNotStarted = 1u,
  AdmissionPresentSubmitContextMissing = 2u,
  AdmissionPresentSubmitFlags = 3u,
  AdmissionPresentSubmitPresentFlag = 4u,
  AdmissionPresentSubmitFence = 5u,
  AdmissionPresentSubmitNode = 6u,
  AdmissionPresentSubmitEngine = 7u,
  AdmissionPresentSubmitPrivate = 8u,
  AdmissionPresentSubmitResidency = 9u,
  AdmissionPresentSubmitContextToken = 10u,
  AdmissionPresentSubmitDmaStart = 11u,
  AdmissionPresentSubmitDmaEnd = 12u,
  AdmissionPresentSubmitDmaSize = 13u,
  AdmissionPresentSubmitPrivateStart = 14u,
  AdmissionPresentSubmitPrivateEndLow = 15u,
  AdmissionPresentSubmitPrivateEndHigh = 16u,
  AdmissionPresentSubmitObject = 17u,
  AdmissionPresentSubmitDevice = 18u,
  AdmissionPresentSubmitAdapter = 19u,
  AdmissionPresentSubmitInactive = 20u,
} ADMISSION_PRESENT_SUBMIT_GUARD;

static inline unsigned long long AdmissionSubmitTraceWord(
    unsigned int Field, unsigned int Value) {
  return ADMISSION_SUBMIT_TRACE_TAG |
      ((unsigned long long)(Field & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Value;
}

static inline unsigned long long AdmissionGdiSubmitTraceWord(
    unsigned int Field, unsigned int Value) {
  return ADMISSION_GDI_SUBMIT_TRACE_TAG |
      ((unsigned long long)(Field & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Value;
}

static inline unsigned long long AdmissionUmdRenderTraceWord(
    unsigned int Field, unsigned int Value) {
  return ADMISSION_UMD_RENDER_TRACE_TAG |
      ((unsigned long long)(Field & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Value;
}

static inline unsigned long long AdmissionOpenAllocationTraceWord(
    unsigned int Field, unsigned int Value) {
  return ADMISSION_OPEN_ALLOCATION_TRACE_TAG |
      ((unsigned long long)(Field & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Value;
}

static inline unsigned long long AdmissionPagingBuildTraceWord(
    unsigned int Field, unsigned int Value) {
  return ADMISSION_PAGING_BUILD_TRACE_TAG |
      ((unsigned long long)(Field & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Value;
}

static inline unsigned long long AdmissionSubmitRenderGuardWord(
    unsigned int Field, unsigned int Value) {
  return ADMISSION_SUBMIT_RENDER_GUARD_TAG |
      ((unsigned long long)(Field & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Value;
}

static inline unsigned long long AdmissionSubmitFenceDetailWord(
    unsigned int Outstanding, unsigned int Submitted) {
  return ADMISSION_SUBMIT_FENCE_DETAIL_TAG |
      ((unsigned long long)(Outstanding & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Submitted;
}

static inline unsigned long long AdmissionPatchRenderGuardWord(
    unsigned int Guard, unsigned int Status) {
  return ADMISSION_PATCH_RENDER_GUARD_TAG |
      ((unsigned long long)(Guard & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Status;
}

static inline unsigned long long AdmissionPrepatchAdoptGuardWord(
    unsigned int Guard, unsigned int Status) {
  return ADMISSION_PREPATCH_ADOPT_GUARD_TAG |
      ((unsigned long long)(Guard & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Status;
}

static inline unsigned long long AdmissionSubmitPacketGuardWord(
    unsigned int Guard, unsigned int Status) {
  return ADMISSION_SUBMIT_PACKET_GUARD_TAG |
      ((unsigned long long)(Guard & ADMISSION_SUBMIT_TRACE_FIELD_MASK)
       << ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) |
      (unsigned long long)Status;
}

static inline unsigned int AdmissionSubmitTraceField(unsigned long long Word) {
  return (unsigned int)((Word >> ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) &
                        ADMISSION_SUBMIT_TRACE_FIELD_MASK);
}

static inline unsigned int AdmissionSubmitTraceValue(unsigned long long Word) {
  return (unsigned int)Word;
}

#endif /* APPLE_AGX_RENDER_SUBMIT_TRACE_H */
