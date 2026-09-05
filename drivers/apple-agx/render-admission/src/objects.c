#include "render_objects.h"

#define ADMISSION_OBJECT_NULL ((void *)0)

void AdmissionObjectsInitializeAdapter(ADMISSION_OBJECT_ADAPTER *Adapter) {
  if (Adapter == ADMISSION_OBJECT_NULL)
    return;
  Adapter->Magic = ADMISSION_OBJECT_ADAPTER_MAGIC;
  Adapter->Started = 0u;
  Adapter->DeviceCount = 0u;
}

int AdmissionObjectsStartAdapter(ADMISSION_OBJECT_ADAPTER *Adapter) {
  if (Adapter == ADMISSION_OBJECT_NULL ||
      Adapter->Magic != ADMISSION_OBJECT_ADAPTER_MAGIC ||
      Adapter->Started != 0u || Adapter->DeviceCount != 0u)
    return 0;
  Adapter->Started = 1u;
  return 1;
}

int AdmissionObjectsStopAdapter(ADMISSION_OBJECT_ADAPTER *Adapter) {
  if (Adapter == ADMISSION_OBJECT_NULL ||
      Adapter->Magic != ADMISSION_OBJECT_ADAPTER_MAGIC ||
      Adapter->Started == 0u || Adapter->DeviceCount != 0u)
    return 0;
  Adapter->Started = 0u;
  return 1;
}

int AdmissionObjectsCreateDevice(ADMISSION_OBJECT_ADAPTER *Adapter,
                                 void *RuntimeHandle, unsigned int Flags,
                                 ADMISSION_OBJECT_DEVICE *Device) {
  ADMISSION_OBJECT_DEVICE candidate;

  if (Adapter == ADMISSION_OBJECT_NULL ||
      Adapter->Magic != ADMISSION_OBJECT_ADAPTER_MAGIC ||
      Adapter->Started == 0u ||
      Adapter->DeviceCount >= ADMISSION_OBJECT_MAX_DEVICES ||
      (RuntimeHandle == ADMISSION_OBJECT_NULL &&
       (Flags & ADMISSION_DEVICE_SYSTEM) == 0u) ||
      Device == ADMISSION_OBJECT_NULL ||
      (Flags & ~ADMISSION_DEVICE_VALID_FLAGS) != 0u)
    return 0;
  candidate.Magic = ADMISSION_OBJECT_DEVICE_MAGIC;
  candidate.Flags = Flags;
  candidate.Adapter = Adapter;
  candidate.RuntimeHandle = RuntimeHandle;
  candidate.ContextCount = 0u;
  candidate.AllocationCount = 0u;
  *Device = candidate;
  ++Adapter->DeviceCount;
  return 1;
}

int AdmissionObjectsDestroyDevice(ADMISSION_OBJECT_DEVICE *Device) {
  ADMISSION_OBJECT_ADAPTER *adapter;

  if (Device == ADMISSION_OBJECT_NULL ||
      Device->Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      Device->Adapter == ADMISSION_OBJECT_NULL ||
      Device->Adapter->Magic != ADMISSION_OBJECT_ADAPTER_MAGIC ||
      Device->Adapter->DeviceCount == 0u || Device->ContextCount != 0u ||
      Device->AllocationCount != 0u)
    return 0;
  adapter = Device->Adapter;
  Device->Magic = 0u;
  Device->Adapter = ADMISSION_OBJECT_NULL;
  Device->RuntimeHandle = ADMISSION_OBJECT_NULL;
  --adapter->DeviceCount;
  return 1;
}

int AdmissionObjectsCreateContext(ADMISSION_OBJECT_DEVICE *Device,
                                  void *RuntimeHandle,
                                  unsigned int NodeOrdinal,
                                  unsigned int EngineAffinity,
                                  unsigned int Flags,
                                  ADMISSION_OBJECT_CONTEXT *Context) {
  ADMISSION_OBJECT_CONTEXT candidate;

  if (Device == ADMISSION_OBJECT_NULL ||
      Device->Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      Device->Adapter == ADMISSION_OBJECT_NULL ||
      Device->Adapter->Magic != ADMISSION_OBJECT_ADAPTER_MAGIC ||
      Device->Adapter->Started == 0u || RuntimeHandle == ADMISSION_OBJECT_NULL ||
      Context == ADMISSION_OBJECT_NULL || NodeOrdinal != 0u ||
      EngineAffinity != 1u ||
      (Flags & ~ADMISSION_CONTEXT_VALID_FLAGS) != 0u ||
      Device->ContextCount >= ADMISSION_OBJECT_MAX_CONTEXTS_PER_DEVICE)
    return 0;
  candidate.Magic = ADMISSION_OBJECT_CONTEXT_MAGIC;
  candidate.Flags = Flags;
  candidate.Device = Device;
  candidate.RuntimeHandle = RuntimeHandle;
  candidate.NodeOrdinal = NodeOrdinal;
  candidate.EngineAffinity = EngineAffinity;
  candidate.FenceOutstanding = 0u;
  *Context = candidate;
  ++Device->ContextCount;
  return 1;
}

int AdmissionObjectsDestroyContext(ADMISSION_OBJECT_CONTEXT *Context) {
  ADMISSION_OBJECT_DEVICE *device;

  if (Context == ADMISSION_OBJECT_NULL ||
      Context->Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      Context->Device == ADMISSION_OBJECT_NULL ||
      Context->Device->Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      Context->Device->ContextCount == 0u || Context->FenceOutstanding != 0u)
    return 0;
  device = Context->Device;
  Context->Magic = 0u;
  Context->Device = ADMISSION_OBJECT_NULL;
  Context->RuntimeHandle = ADMISSION_OBJECT_NULL;
  --device->ContextCount;
  return 1;
}
