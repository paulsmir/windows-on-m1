#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

#include "apple_input_hw.h"
#include "apple_input_ioctl.h"
#include "apple_spihid.h"
#ifndef AI_ENABLE_TRACKPAD_CAPTURE
#define AI_ENABLE_TRACKPAD_CAPTURE 0
#endif
#if AI_ENABLE_TRACKPAD_CAPTURE
#include "apple_input_capture.h"
#endif

enum AI_VHF_STATE {
    AiVhfAbsent,
    AiVhfDescriptorsReady,
    AiVhfStarting,
    AiVhfRunning,
    AiVhfStopping,
};

typedef struct _AI_DEVICE_CONTEXT {
    WDFDEVICE Device;
    WDFWAITLOCK FrontendLock;
#if AI_ENABLE_TRACKPAD_CAPTURE
    WDFWAITLOCK CaptureLock;
#endif
    VHFHANDLE KeyboardVhf;
    enum AI_VHF_STATE KeyboardVhfState;
    PHYSICAL_ADDRESS MemoryBase[3];
    ULONG MemoryLength[3];
    ULONG InterruptVector;
    PUCHAR SpiRegisters;
    PUCHAR ApGpioRegisters;
    PUCHAR NubGpioRegisters;
    WDFINTERRUPT Interrupt;
    WDFQUEUE DiagnosticQueue;
    struct ai_transport_queue TransportQueue;
    struct ai_discovery Discovery;
    struct ai_reassembler Reassembler;
    struct ai_descriptor_store Descriptors;
    struct ai_hid_input_contract KeyboardInputContract;
    AI_DIAGNOSTIC_SNAPSHOT_V3 Diagnostics;
#if AI_ENABLE_TRACKPAD_CAPTURE
    AI_TRACKPAD_CAPTURE_BLOB TrackpadCapture;
#endif
    UCHAR ReceivePacket[AI_PACKET_SIZE];
    UCHAR TransmitPacket[AI_PACKET_SIZE];
    UCHAR ZeroTransmit[AI_PACKET_SIZE];
    UCHAR StatusBytes[AI_SPI_WRITE_STATUS_SIZE];
    UCHAR MessageId;
    BOOLEAN ResourcesValidated;
    BOOLEAN HardwareStarted;
    BOOLEAN Stopping;
    BOOLEAN TransportOnly;
} AI_DEVICE_CONTEXT, *PAI_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AI_DEVICE_CONTEXT, AiGetDeviceContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AppleInputEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE AppleInputEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE AppleInputEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY AppleInputEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED
    AppleInputEvtDeviceD0EntryPostInterruptsEnabled;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED
    AppleInputEvtDeviceD0ExitPreInterruptsDisabled;
EVT_WDF_INTERRUPT_ISR AiInputInterruptIsr;
EVT_WDF_INTERRUPT_WORKITEM AiTransportWorker;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL AiDiagnosticsEvtIoDeviceControl;

NTSTATUS AppleInputCreateDevice(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit);
NTSTATUS AiDeviceParseResources(WDFCMRESLIST Raw, WDFCMRESLIST Translated,
                                PAI_DEVICE_CONTEXT Context);
NTSTATUS AiSpiValidateReadOnly(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiGpioValidateReadOnly(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiSpiInitialize(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiSpiTransfer(PAI_DEVICE_CONTEXT Context, const UCHAR *Tx, UCHAR *Rx,
                       SIZE_T Length, ULONGLONG DeadlineQpc);
NTSTATUS AiSpiWritePacketReadStatus(PAI_DEVICE_CONTEXT Context,
                                    const UCHAR Packet[AI_PACKET_SIZE],
                                    UCHAR Status[AI_SPI_WRITE_STATUS_SIZE],
                                    ULONGLONG DeadlineQpc);
NTSTATUS AiGpioEnableInputInterrupt(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiGpioResetInputController(PAI_DEVICE_CONTEXT Context);
BOOLEAN AiGpioInputAsserted(PAI_DEVICE_CONTEXT Context);
VOID AiGpioAcknowledge(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiTransportStart(PAI_DEVICE_CONTEXT Context);
VOID AiTransportStop(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiCaptureDiscoveryDescriptor(
    PAI_DEVICE_CONTEXT Context, enum ai_discovery_phase Phase,
    const struct ai_protocol_message *Message);
NTSTATUS AiDiagnosticsInitialize(WDFDEVICE Device, PAI_DEVICE_CONTEXT Context);
VOID AiDiagnosticsRecordHeader(PAI_DEVICE_CONTEXT Context,
                               const struct ai_packet_view *Packet,
                               enum ai_status Result);
VOID AiDiagnosticsRecordMessage(PAI_DEVICE_CONTEXT Context,
                                const struct ai_protocol_message *Message);
VOID AiDiagnosticsRecordDescriptor(
    PAI_DEVICE_CONTEXT Context, const struct ai_descriptor_slot *Descriptor);
VOID AiDiagnosticsPublish(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiKeyboardVhfStart(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiKeyboardVhfSubmit(PAI_DEVICE_CONTEXT Context,
                             const UCHAR *Report, SIZE_T Length);
VOID AiKeyboardVhfStop(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiVhfFrontendStart(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiVhfFrontendSubmitKeyboard(PAI_DEVICE_CONTEXT Context,
                                     const UCHAR *Report, SIZE_T Length);
VOID AiVhfFrontendStop(PAI_DEVICE_CONTEXT Context);
#if AI_ENABLE_TRACKPAD_CAPTURE
NTSTATUS AiTrackpadCaptureInitialize(WDFDEVICE Device,
                                     PAI_DEVICE_CONTEXT Context);
VOID AiTrackpadCaptureCancel(PAI_DEVICE_CONTEXT Context);
VOID AiTrackpadCaptureRecord(PAI_DEVICE_CONTEXT Context, UCHAR Device,
                             const UCHAR *Report, SIZE_T Length);
BOOLEAN AiTrackpadCaptureIoctl(PAI_DEVICE_CONTEXT Context,
                               WDFREQUEST Request, ULONG IoControlCode);
#endif
