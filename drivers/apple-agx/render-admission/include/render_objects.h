#ifndef APPLE_AGX_RENDER_OBJECTS_H
#define APPLE_AGX_RENDER_OBJECTS_H

#define ADMISSION_OBJECT_ADAPTER_MAGIC 0x414f4152u /* "RAOA" */
#define ADMISSION_OBJECT_DEVICE_MAGIC 0x444f4152u  /* "RAOD" */
#define ADMISSION_OBJECT_CONTEXT_MAGIC 0x434f4152u /* "RAOC" */

#define ADMISSION_OBJECT_MAX_DEVICES 64u
#define ADMISSION_OBJECT_MAX_CONTEXTS_PER_DEVICE 64u

#define ADMISSION_DEVICE_SYSTEM (1u << 0)
#define ADMISSION_DEVICE_GDI (1u << 1)
#define ADMISSION_DEVICE_VALID_FLAGS                                 \
  (ADMISSION_DEVICE_SYSTEM | ADMISSION_DEVICE_GDI)

#define ADMISSION_CONTEXT_SYSTEM (1u << 0)
#define ADMISSION_CONTEXT_GDI (1u << 1)
#define ADMISSION_CONTEXT_VALID_FLAGS                                \
  (ADMISSION_CONTEXT_SYSTEM | ADMISSION_CONTEXT_GDI)

typedef struct _ADMISSION_OBJECT_ADAPTER {
  unsigned int Magic;
  unsigned int Started;
  unsigned int DeviceCount;
} ADMISSION_OBJECT_ADAPTER;

typedef struct _ADMISSION_OBJECT_DEVICE {
  unsigned int Magic;
  unsigned int Flags;
  ADMISSION_OBJECT_ADAPTER *Adapter;
  void *RuntimeHandle;
  unsigned int ContextCount;
  unsigned int AllocationCount;
} ADMISSION_OBJECT_DEVICE;

typedef struct _ADMISSION_OBJECT_CONTEXT {
  unsigned int Magic;
  unsigned int Flags;
  ADMISSION_OBJECT_DEVICE *Device;
  void *RuntimeHandle;
  unsigned int NodeOrdinal;
  unsigned int EngineAffinity;
  unsigned int FenceOutstanding;
} ADMISSION_OBJECT_CONTEXT;

void AdmissionObjectsInitializeAdapter(ADMISSION_OBJECT_ADAPTER *Adapter);
int AdmissionObjectsStartAdapter(ADMISSION_OBJECT_ADAPTER *Adapter);
int AdmissionObjectsStopAdapter(ADMISSION_OBJECT_ADAPTER *Adapter);
int AdmissionObjectsCreateDevice(ADMISSION_OBJECT_ADAPTER *Adapter,
                                 void *RuntimeHandle, unsigned int Flags,
                                 ADMISSION_OBJECT_DEVICE *Device);
int AdmissionObjectsDestroyDevice(ADMISSION_OBJECT_DEVICE *Device);
int AdmissionObjectsCreateContext(ADMISSION_OBJECT_DEVICE *Device,
                                  void *RuntimeHandle,
                                  unsigned int NodeOrdinal,
                                  unsigned int EngineAffinity,
                                  unsigned int Flags,
                                  ADMISSION_OBJECT_CONTEXT *Context);
int AdmissionObjectsDestroyContext(ADMISSION_OBJECT_CONTEXT *Context);

#endif /* APPLE_AGX_RENDER_OBJECTS_H */
