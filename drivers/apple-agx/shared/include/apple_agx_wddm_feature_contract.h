#ifndef APPLE_AGX_WDDM_FEATURE_CONTRACT_H
#define APPLE_AGX_WDDM_FEATURE_CONTRACT_H

#define APPLE_AGX_WDDM_FEATURE_CONTRACT_VERSION 1u

typedef enum _APPLE_AGX_WDDM_READY_BIT {
  APPLE_AGX_WDDM_READY_WDDM3_IDENTITY = 1u << 0,
  APPLE_AGX_WDDM_READY_ONE_NODE_TOPOLOGY = 1u << 1,
  APPLE_AGX_WDDM_READY_MEMORY_PAGING = 1u << 2,
  APPLE_AGX_WDDM_READY_DEVICE_CONTEXT = 1u << 3,
  APPLE_AGX_WDDM_READY_SCHEDULER = 1u << 4,
  APPLE_AGX_WDDM_READY_DMA_BOUNDARY_PREEMPTION = 1u << 5,
  APPLE_AGX_WDDM_READY_PER_ENGINE_TDR = 1u << 6,
  APPLE_AGX_WDDM_READY_GDI_COMMAND_BUFFER = 1u << 7,
  APPLE_AGX_WDDM_READY_D589_SCANOUT = 1u << 8,
  APPLE_AGX_WDDM_READY_KMD_DIRECT_FLIP = 1u << 9,
  APPLE_AGX_WDDM_READY_UMD_DIRECT_FLIP = 1u << 10,
  APPLE_AGX_WDDM_READY_INDEPENDENT_FLIP = 1u << 11,
  APPLE_AGX_WDDM_READY_NON_VGA_STOP = 1u << 12,
  APPLE_AGX_WDDM_READY_AGX_COMPLETION = 1u << 13,
} APPLE_AGX_WDDM_READY_BIT;

#define APPLE_AGX_WDDM_REQUIRED_READY_MASK                                \
  (APPLE_AGX_WDDM_READY_WDDM3_IDENTITY |                                  \
   APPLE_AGX_WDDM_READY_ONE_NODE_TOPOLOGY |                               \
   APPLE_AGX_WDDM_READY_MEMORY_PAGING |                                   \
   APPLE_AGX_WDDM_READY_DEVICE_CONTEXT |                                  \
   APPLE_AGX_WDDM_READY_SCHEDULER |                                       \
   APPLE_AGX_WDDM_READY_DMA_BOUNDARY_PREEMPTION |                         \
   APPLE_AGX_WDDM_READY_PER_ENGINE_TDR |                                  \
   APPLE_AGX_WDDM_READY_GDI_COMMAND_BUFFER |                              \
   APPLE_AGX_WDDM_READY_D589_SCANOUT |                                    \
   APPLE_AGX_WDDM_READY_KMD_DIRECT_FLIP |                                 \
   APPLE_AGX_WDDM_READY_UMD_DIRECT_FLIP |                                 \
   APPLE_AGX_WDDM_READY_INDEPENDENT_FLIP |                                \
   APPLE_AGX_WDDM_READY_NON_VGA_STOP |                                    \
   APPLE_AGX_WDDM_READY_AGX_COMPLETION)

typedef enum _APPLE_AGX_WDDM_CAP_BIT {
  APPLE_AGX_WDDM_CAP_MULTI_ENGINE_AWARE = 1u << 0,
  APPLE_AGX_WDDM_CAP_PREEMPTION = 1u << 1,
  APPLE_AGX_WDDM_CAP_PER_ENGINE_TDR = 1u << 2,
  APPLE_AGX_WDDM_CAP_GDI_COMMAND_BUFFER = 1u << 3,
  APPLE_AGX_WDDM_CAP_FLIP_ON_VSYNC_MMIO = 1u << 4,
  APPLE_AGX_WDDM_CAP_DIRECT_FLIP = 1u << 5,
  APPLE_AGX_WDDM_CAP_INDEPENDENT_FLIP = 1u << 6,
  APPLE_AGX_WDDM_CAP_NON_VGA = 1u << 7,
} APPLE_AGX_WDDM_CAP_BIT;

#define APPLE_AGX_WDDM_MANDATORY_CAPS_MASK                                \
  (APPLE_AGX_WDDM_CAP_MULTI_ENGINE_AWARE |                                \
   APPLE_AGX_WDDM_CAP_PREEMPTION |                                        \
   APPLE_AGX_WDDM_CAP_PER_ENGINE_TDR |                                    \
   APPLE_AGX_WDDM_CAP_GDI_COMMAND_BUFFER |                                \
   APPLE_AGX_WDDM_CAP_FLIP_ON_VSYNC_MMIO |                                \
   APPLE_AGX_WDDM_CAP_DIRECT_FLIP |                                       \
   APPLE_AGX_WDDM_CAP_INDEPENDENT_FLIP |                                  \
   APPLE_AGX_WDDM_CAP_NON_VGA)

typedef struct _APPLE_AGX_WDDM_FEATURE_INPUT {
  unsigned int Version;
  unsigned int Size;
  unsigned int WddmMajor;
  unsigned int WddmMinor;
  unsigned int NodeCount;
  unsigned int ReadyMask;
  unsigned int Reserved;
} APPLE_AGX_WDDM_FEATURE_INPUT;

typedef struct _APPLE_AGX_WDDM_FEATURE_OUTPUT {
  unsigned int Ready;
  unsigned int RequiredReadyMask;
  unsigned int MissingReadyMask;
  unsigned int PublishCapsMask;
} APPLE_AGX_WDDM_FEATURE_OUTPUT;

typedef enum _APPLE_AGX_WDDM_FEATURE_CONTRACT_RESULT {
  AppleAgxWddmFeatureContractInvalid = 0,
  AppleAgxWddmFeatureContractIncomplete,
  AppleAgxWddmFeatureContractReady,
} APPLE_AGX_WDDM_FEATURE_CONTRACT_RESULT;

APPLE_AGX_WDDM_FEATURE_CONTRACT_RESULT AppleAgxWddmFeatureContractEvaluate(
    const APPLE_AGX_WDDM_FEATURE_INPUT *Input,
    APPLE_AGX_WDDM_FEATURE_OUTPUT *Output);

#endif /* APPLE_AGX_WDDM_FEATURE_CONTRACT_H */
