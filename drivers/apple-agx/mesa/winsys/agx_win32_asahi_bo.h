#ifndef AGX_WIN32_ASAHI_BO_H
#define AGX_WIN32_ASAHI_BO_H

#include "agx_win32_native_device.h"
#include "agx_win32_reloc_capture.h"
#include <stdint.h>
#ifdef APPLE_AGX_GPUVA_WINSYS
#include "agx_win32_gpuva.h"
#endif

struct agx_device;
struct agx_bo;
struct pipe_screen;
struct pipe_context;
struct _AGX_WIN32_ASAHI_CAPTURE;
typedef int (*AGX_WIN32_ASAHI_MAP_RELEASE)(
    const void *NativeBo, const void *ExpectedAddress, int Commit);
typedef struct {
  int (*Enter)(void *, AGX_WIN32_SCREEN *);
  void (*Leave)(void *);
  int (*Associate)(void *, APPLE_AGX_U64, const void *, APPLE_AGX_U64,
                   AGX_WIN32_ASAHI_MAP_RELEASE);
  int (*Detach)(void *, APPLE_AGX_U64, const void *, APPLE_AGX_U64);
  int (*Identity)(void *, const void *, APPLE_AGX_U64, AGX_WIN32_RELOC_ALLOCATION *);
  const void *(*NextBo)(void *, APPLE_AGX_U32 *);
} AGX_WIN32_ASAHI_OWNER_OPS;

/* One caller-serialized native device. Screen/owner storage must outlive it.
 * The Windows owner registry is authoritative; this is not another BO table. */
typedef struct {
  struct agx_device *Native;
  AGX_WIN32_NATIVE_DEVICE Buffers;
  AGX_WIN32_ASAHI_OWNER_OPS Ops;
  void *Owner;
  APPLE_AGX_U32 LiveBos;
  void *UnpublishedBo; /* single failed-create rollback, never exposed to Mesa */
  struct _AGX_WIN32_ASAHI_CAPTURE *ActiveCapture;
  void *ActiveEmission;
  const void *BatchOps; /* existing UMD transaction callbacks, caller-owned */
  void *BatchOwner;
  int EncoderAllocationIntent;
  int Failed;
  struct pipe_context *(*ContextCreate)(struct pipe_screen *, void *, unsigned);
  void (*ContextDestroy)(struct pipe_context *);
  APPLE_AGX_U32 ContextCount;
  int Closing;
#ifdef APPLE_AGX_GPUVA_WINSYS
  AGX_WIN32_GPUVA_SPACE Gpuva;
  int GpuvaReady;
  /* InitBM owns these TVB lists and blocks for the queue lifetime. */
#endif
} AGX_WIN32_ASAHI_BACKEND;


int AgxWin32AsahiAttach(AGX_WIN32_ASAHI_BACKEND *, struct agx_device *,
    AGX_WIN32_SCREEN *, const AGX_WIN32_ASAHI_OWNER_OPS *, void *, APPLE_AGX_U64);
int AgxWin32AsahiCollect(AGX_WIN32_ASAHI_BACKEND *);
int AgxWin32AsahiDetach(AGX_WIN32_ASAHI_BACKEND *);
int AgxWin32AsahiIdentity(AGX_WIN32_ASAHI_BACKEND *, struct agx_bo *,
    AGX_WIN32_RELOC_ALLOCATION *);
/* Caller-serialized native construction lookup; never a PA/GPUVA resolver.
 * Returned BO is borrowed until caller immediately registers/retains it. */
int AgxWin32AsahiFindAddress(AGX_WIN32_ASAHI_BACKEND *, APPLE_AGX_U64 Owner,
    APPLE_AGX_U32 Generation, APPLE_AGX_U64 Address, APPLE_AGX_U64 Bytes,
    struct agx_bo **Bo, APPLE_AGX_U64 *Offset);
/* Validates an exact mapped CPU interval against the same current owner and
 * returns its construction coordinate. It never scans emitted command bytes. */
int AgxWin32AsahiFindCpuAddress(AGX_WIN32_ASAHI_BACKEND *, APPLE_AGX_U64 Owner,
    APPLE_AGX_U32 Generation, const void *Cpu, APPLE_AGX_U64 Bytes,
    struct agx_bo **Bo, APPLE_AGX_U64 *Address, APPLE_AGX_U64 *Offset);
/* Explicit source-level intent for native initial VDM/CDM allocation. This
 * must be called only by the transformed agx_encoder_allocate site. */
struct agx_bo *AgxWin32AsahiEncoderCreate(struct agx_device *, size_t,
    unsigned, const char *);
int AgxWin32AsahiClass(AGX_WIN32_ASAHI_BACKEND *, struct agx_bo *,
    APPLE_AGX_U32 *ClassId);
struct agx_bo *AgxWin32AsahiImportBo(
    AGX_WIN32_ASAHI_BACKEND *, const AGX_WIN32_SCREEN_BUFFER *,
    const char *Label);
#ifdef APPLE_AGX_GPUVA_WINSYS
const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(
    AGX_WIN32_ASAHI_BACKEND *, struct agx_bo *);
#endif
struct agx_bo *AgxWin32AsahiLookupBo(struct agx_device *, uint32_t);

#endif
