#ifndef UMD_ASAHI_OWNER_H
#define UMD_ASAHI_OWNER_H
#include "agx_win32_asahi_bo.h"
#include "agx_win32_asahi_batch.h"
struct _ADMISSION_UMD_DEVICE;
typedef struct {
  struct _ADMISSION_UMD_DEVICE *Device;
  AGX_WIN32_ASAHI_BACKEND *Backend;
} ADMISSION_UMD_ASAHI_OWNER;
const AGX_WIN32_ASAHI_BATCH_OPS *AdmissionUmdAsahiBatchOperations(void);
void AdmissionUmdAsahiOwnerOperations(AGX_WIN32_ASAHI_OWNER_OPS *Ops);
#endif
