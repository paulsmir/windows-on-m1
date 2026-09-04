#ifndef APPLE_AGX_ADMISSION_H
#define APPLE_AGX_ADMISSION_H

/* WDK display headers require the base NT, Win32, and GDI types first. */
// clang-format off
#include <ntddk.h>
#include <windef.h>
#include <winerror.h>
#include <wingdi.h>
#include <ntddvdeo.h>
#include <d3dkmddi.h>
#include <d3dkmthk.h>
#include <dispmprt.h>
// clang-format on

#define APPLE_AGX_ADMISSION_POOL_TAG 'mAgA'

typedef struct _APPLE_AGX_ADMISSION_CONTEXT {
  PDEVICE_OBJECT PhysicalDeviceObject;
} APPLE_AGX_ADMISSION_CONTEXT;

DXGKDDI_ADD_DEVICE AppleAgxAdmissionAddDevice;
DXGKDDI_START_DEVICE AppleAgxAdmissionStartDevice;
DXGKDDI_STOP_DEVICE AppleAgxAdmissionStopDevice;
DXGKDDI_REMOVE_DEVICE AppleAgxAdmissionRemoveDevice;
DXGKDDI_DISPATCH_IO_REQUEST AppleAgxAdmissionDispatchIoRequest;
DXGKDDI_INTERRUPT_ROUTINE AppleAgxAdmissionInterruptRoutine;
DXGKDDI_DPC_ROUTINE AppleAgxAdmissionDpcRoutine;
DXGKDDI_QUERY_CHILD_RELATIONS AppleAgxAdmissionQueryChildRelations;
DXGKDDI_QUERY_CHILD_STATUS AppleAgxAdmissionQueryChildStatus;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR AppleAgxAdmissionQueryDeviceDescriptor;
DXGKDDI_SET_POWER_STATE AppleAgxAdmissionSetPowerState;
DXGKDDI_RESET_DEVICE AppleAgxAdmissionResetDevice;
DXGKDDI_UNLOAD AppleAgxAdmissionUnload;

VOID AppleAgxAdmissionRecord(_In_ PDEVICE_OBJECT PhysicalDeviceObject,
                             _In_ PCWSTR StageName, _In_ ULONG Stage,
                             _In_opt_ PCWSTR StatusName,
                             _In_ NTSTATUS Status);

#endif
