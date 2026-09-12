#ifndef AGX_WIN32_ASAHI_BO_H
#define AGX_WIN32_ASAHI_BO_H

#include "agx_win32_native_device.h"
#include "agx_win32_reloc_capture.h"

struct agx_device;
struct agx_bo;
typedef struct {
  int (*Enter)(void *, AGX_WIN32_SCREEN *);
  void (*Leave)(void *);
  int (*Associate)(void *, APPLE_AGX_U64, const void *, APPLE_AGX_U64);
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
  int Failed;
} AGX_WIN32_ASAHI_BACKEND;

int AgxWin32AsahiAttach(AGX_WIN32_ASAHI_BACKEND *, struct agx_device *,
    AGX_WIN32_SCREEN *, const AGX_WIN32_ASAHI_OWNER_OPS *, void *, APPLE_AGX_U64);
int AgxWin32AsahiCollect(AGX_WIN32_ASAHI_BACKEND *);
int AgxWin32AsahiDetach(AGX_WIN32_ASAHI_BACKEND *);
int AgxWin32AsahiIdentity(AGX_WIN32_ASAHI_BACKEND *, struct agx_bo *,
    AGX_WIN32_RELOC_ALLOCATION *);

#endif
