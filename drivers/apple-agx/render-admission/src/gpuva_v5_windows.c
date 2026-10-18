#include "render_admission.h"
#include "apple_agx_gpuva_broker_v5_client.h"

static bool AdmissionGpuvaWrite64(void *opaque, unsigned offset, unsigned long long value)
{
  ADMISSION_CONTEXT *context = opaque;
  if (context == NULL || context->BrokerBase == NULL ||
      offset > AGX_GPUVA_V5_OFFSET + AGX_GPUVA_V5_WINDOW - sizeof(value))
    return false;
  WRITE_REGISTER_ULONG64((volatile ULONG64 *)(context->BrokerBase + offset),
                         (ULONG64)value);
  return true;
}

static bool AdmissionGpuvaRead64(void *opaque, unsigned offset, unsigned long long *value)
{
  ADMISSION_CONTEXT *context = opaque;
  if (context == NULL || context->BrokerBase == NULL || value == NULL ||
      offset > AGX_GPUVA_V5_OFFSET + AGX_GPUVA_V5_WINDOW - sizeof(*value))
    return false;
  *value = READ_REGISTER_ULONG64(
      (volatile ULONG64 *)(context->BrokerBase + offset));
  return true;
}

static bool AdmissionGpuvaWrite32(void *opaque, unsigned offset, unsigned int value)
{
  ADMISSION_CONTEXT *context = opaque;
  if (context == NULL || context->BrokerBase == NULL ||
      offset != AGX_GPUVA_V5_OFFSET + AGX_GPUVA_V5_DOORBELL)
    return false;
  WRITE_REGISTER_ULONG((volatile ULONG *)(context->BrokerBase + offset),
                       (ULONG)value);
  return true;
}

static unsigned long long AdmissionGpuvaNow(void *opaque)
{
  UNREFERENCED_PARAMETER(opaque);
  return (unsigned long long)KeQueryPerformanceCounter(NULL).QuadPart;
}

static void AdmissionGpuvaBarrier(void *opaque)
{
  UNREFERENCED_PARAMETER(opaque);
  KeMemoryBarrier();
}

BOOLEAN AdmissionGpuvaV5ClientOpen(ADMISSION_CONTEXT *context,
                                   APPLE_AGX_GPUVA_V5_CLIENT *client)
{
  APPLE_AGX_GPUVA_V5_IO io;
  if (context == NULL || context->BrokerBase == NULL || client == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return FALSE;
  RtlZeroMemory(&io, sizeof(io));
  io.Context = context;
  io.Write64 = AdmissionGpuvaWrite64;
  io.Read64 = AdmissionGpuvaRead64;
  io.Write32 = AdmissionGpuvaWrite32;
  io.Barrier = AdmissionGpuvaBarrier;
  io.Now = AdmissionGpuvaNow; /* EXP1052 receipt-only call timing. */
  return AppleAgxGpuvaV5ClientInit(client, &io) ? TRUE : FALSE;
}
