#ifndef APPLE_AGX_RENDER_SUBMIT_TRACE_H
#define APPLE_AGX_RENDER_SUBMIT_TRACE_H

#define ADMISSION_SUBMIT_TRACE_TAG 0x5070000000000000ULL
#define ADMISSION_GDI_SUBMIT_TRACE_TAG 0x5090000000000000ULL
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

static inline unsigned int AdmissionSubmitTraceField(unsigned long long Word) {
  return (unsigned int)((Word >> ADMISSION_SUBMIT_TRACE_FIELD_SHIFT) &
                        ADMISSION_SUBMIT_TRACE_FIELD_MASK);
}

static inline unsigned int AdmissionSubmitTraceValue(unsigned long long Word) {
  return (unsigned int)Word;
}

#endif /* APPLE_AGX_RENDER_SUBMIT_TRACE_H */
