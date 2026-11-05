#ifndef APPLE_AGX_WIN32_DEVICE_INFO_H
#define APPLE_AGX_WIN32_DEVICE_INFO_H

#include "apple_agx_win32_abi.h"

#define AGX_WIN32_DEVICE_INFO_MAGIC 0x49445741u /* AWDI */
#define AGX_WIN32_DEVICE_INFO_VERSION 1u
#define AGX_WIN32_BUFFER_CLASS_COUNT 3u

typedef enum _AGX_WIN32_GPU_VARIANT {
  AgxWin32GpuG13G = 1u,
} AGX_WIN32_GPU_VARIANT;

typedef enum _AGX_WIN32_BUFFER_CLASS {
  AgxWin32BufferClassGeneral = 1u,
  AgxWin32BufferClassShader = 2u,
  AgxWin32BufferClassEncoder = 3u,
} AGX_WIN32_BUFFER_CLASS;

typedef enum _AGX_WIN32_BUFFER_FLAGS {
  AppleAgxWin32BufferCpuRead = 0x1u,
  AppleAgxWin32BufferCpuWrite = 0x2u,
  AppleAgxWin32BufferGpuRead = 0x4u,
  AppleAgxWin32BufferGpuWrite = 0x8u,
  AppleAgxWin32BufferShareable = 0x10u,
} AGX_WIN32_BUFFER_FLAGS;

typedef struct _AGX_WIN32_BUFFER_CLASS_INFO {
  APPLE_AGX_U32 ClassId;
  APPLE_AGX_U32 MinimumAlignment;
  APPLE_AGX_U64 MaximumBytes;
  APPLE_AGX_U32 Flags;
} AGX_WIN32_BUFFER_CLASS_INFO;

typedef struct _AGX_WIN32_DEVICE_INFO {
  APPLE_AGX_U32 Magic, Version, Bytes, BootGeneration;
  APPLE_AGX_U32 GpuGeneration, GpuVariant, PageBytes, ClassCount;
  AGX_WIN32_BUFFER_CLASS_INFO Classes[AGX_WIN32_BUFFER_CLASS_COUNT];
} AGX_WIN32_DEVICE_INFO;

#endif
