#include "render_objects.h"

#include <assert.h>
#include <string.h>

static void test_device_and_context_lifetime(void) {
  ADMISSION_OBJECT_ADAPTER adapter;
  ADMISSION_OBJECT_DEVICE device;
  ADMISSION_OBJECT_CONTEXT context;

  memset(&adapter, 0xa5, sizeof(adapter));
  AdmissionObjectsInitializeAdapter(&adapter);
  assert(!AdmissionObjectsCreateDevice(&adapter, (void *)1, 0u, &device));
  assert(AdmissionObjectsStartAdapter(&adapter));
  assert(AdmissionObjectsCreateDevice(&adapter, (void *)1,
                                      ADMISSION_DEVICE_GDI, &device));
  assert(adapter.DeviceCount == 1u);

  assert(AdmissionObjectsCreateContext(
      &device, (void *)2, 0u, 1u, ADMISSION_CONTEXT_GDI, &context));
  assert(device.ContextCount == 1u);
  assert(!AdmissionObjectsDestroyDevice(&device));
  context.FenceOutstanding = 1u;
  assert(!AdmissionObjectsDestroyContext(&context));
  context.FenceOutstanding = 0u;
  assert(AdmissionObjectsDestroyContext(&context));
  assert(device.ContextCount == 0u);
  assert(AdmissionObjectsDestroyDevice(&device));
  assert(adapter.DeviceCount == 0u);
  assert(AdmissionObjectsStopAdapter(&adapter));
}

static void test_invalid_inputs_do_not_mutate_storage(void) {
  ADMISSION_OBJECT_ADAPTER adapter;
  ADMISSION_OBJECT_DEVICE device;
  ADMISSION_OBJECT_DEVICE beforeDevice;
  ADMISSION_OBJECT_CONTEXT context;
  ADMISSION_OBJECT_CONTEXT beforeContext;

  AdmissionObjectsInitializeAdapter(&adapter);
  assert(AdmissionObjectsStartAdapter(&adapter));
  memset(&device, 0x5a, sizeof(device));
  beforeDevice = device;
  assert(!AdmissionObjectsCreateDevice(NULL, (void *)1, 0u, &device));
  assert(memcmp(&device, &beforeDevice, sizeof(device)) == 0);
  assert(!AdmissionObjectsCreateDevice(&adapter, NULL, 0u, &device));
  assert(memcmp(&device, &beforeDevice, sizeof(device)) == 0);
  assert(!AdmissionObjectsCreateDevice(&adapter, (void *)1, 0x80000000u,
                                       &device));
  assert(memcmp(&device, &beforeDevice, sizeof(device)) == 0);

  assert(AdmissionObjectsCreateDevice(&adapter, (void *)1, 0u, &device));
  memset(&context, 0x5a, sizeof(context));
  beforeContext = context;
  assert(!AdmissionObjectsCreateContext(&device, (void *)2, 1u, 1u, 0u,
                                        &context));
  assert(memcmp(&context, &beforeContext, sizeof(context)) == 0);
  assert(!AdmissionObjectsCreateContext(&device, (void *)2, 0u, 2u, 0u,
                                        &context));
  assert(memcmp(&context, &beforeContext, sizeof(context)) == 0);
  assert(!AdmissionObjectsCreateContext(&device, (void *)2, 0u, 1u,
                                        0x80000000u, &context));
  assert(memcmp(&context, &beforeContext, sizeof(context)) == 0);
  assert(AdmissionObjectsDestroyDevice(&device));
  assert(AdmissionObjectsStopAdapter(&adapter));
}

/* The Windows scheduler creates its system device with no runtime handle.
 * Rejecting that input prevents paging-process initialization. */
static void test_system_device_without_runtime_handle(void) {
  ADMISSION_OBJECT_ADAPTER adapter;
  ADMISSION_OBJECT_DEVICE device;
  ADMISSION_OBJECT_CONTEXT context;

  AdmissionObjectsInitializeAdapter(&adapter);
  assert(AdmissionObjectsStartAdapter(&adapter));
  assert(AdmissionObjectsCreateDevice(&adapter, NULL,
                                      ADMISSION_DEVICE_SYSTEM, &device));
  assert(device.RuntimeHandle == NULL);
  assert(device.Flags == ADMISSION_DEVICE_SYSTEM);
  assert(device.Adapter == &adapter);
  assert(adapter.DeviceCount == 1u);
  assert(AdmissionObjectsCreateContext(&device, (void *)2, 0u, 1u,
                                       ADMISSION_CONTEXT_SYSTEM, &context));
  assert(!AdmissionObjectsDestroyDevice(&device));
  assert(!AdmissionObjectsStopAdapter(&adapter));
  assert(AdmissionObjectsDestroyContext(&context));
  assert(AdmissionObjectsDestroyDevice(&device));
  assert(adapter.DeviceCount == 0u);
  assert(!AdmissionObjectsCreateDevice(&adapter, NULL,
                                       ADMISSION_DEVICE_GDI, &device));
  assert(!AdmissionObjectsCreateDevice(&adapter, NULL,
                                       ADMISSION_DEVICE_SYSTEM | 0x80000000u,
                                       &device));
  assert(adapter.DeviceCount == 0u);
  assert(AdmissionObjectsStopAdapter(&adapter));
}

static void test_adapter_stop_requires_no_live_devices(void) {
  ADMISSION_OBJECT_ADAPTER adapter;
  ADMISSION_OBJECT_DEVICE device;

  AdmissionObjectsInitializeAdapter(&adapter);
  assert(AdmissionObjectsStartAdapter(&adapter));
  assert(AdmissionObjectsCreateDevice(&adapter, (void *)1, 0u, &device));
  assert(!AdmissionObjectsStopAdapter(&adapter));
  assert(AdmissionObjectsDestroyDevice(&device));
  assert(AdmissionObjectsStopAdapter(&adapter));
  assert(!AdmissionObjectsStopAdapter(&adapter));
}

static void test_paging_context_without_runtime_handle(void) {
  ADMISSION_OBJECT_ADAPTER adapter;
  ADMISSION_OBJECT_DEVICE device;
  ADMISSION_OBJECT_CONTEXT context;

  AdmissionObjectsInitializeAdapter(&adapter);
  assert(AdmissionObjectsStartAdapter(&adapter));
  assert(AdmissionObjectsCreateDevice(&adapter, NULL,
                                      ADMISSION_DEVICE_SYSTEM, &device));
  assert(AdmissionObjectsCreateContext(&device, NULL, 0u, 1u,
                                       ADMISSION_CONTEXT_SYSTEM, &context));
  assert(context.RuntimeHandle == NULL);
  assert(context.Device == &device);
  assert(device.ContextCount == 1u);
  assert(!AdmissionObjectsDestroyDevice(&device));
  assert(AdmissionObjectsDestroyContext(&context));
  assert(device.ContextCount == 0u);
  assert(!AdmissionObjectsCreateContext(&device, NULL, 0u, 1u, 0u, &context));
  assert(!AdmissionObjectsCreateContext(&device, NULL, 0u, 1u,
                                        ADMISSION_CONTEXT_GDI, &context));
  assert(!AdmissionObjectsCreateContext(&device, NULL, 1u, 1u,
                                        ADMISSION_CONTEXT_SYSTEM, &context));
  assert(device.ContextCount == 0u);
  assert(AdmissionObjectsDestroyDevice(&device));
  assert(AdmissionObjectsStopAdapter(&adapter));
}

int main(void) {
  test_paging_context_without_runtime_handle();
  test_system_device_without_runtime_handle();
  test_device_and_context_lifetime();
  test_invalid_inputs_do_not_mutate_storage();
  test_adapter_stop_requires_no_live_devices();
  return 0;
}
