#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <string.h>
#include <initguid.h>

#include "apple_input_ioctl.h"

static HANDLE open_diagnostic_interface(void)
{
    HDEVINFO set = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_APPLE_INPUT_DIAGNOSTIC, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA interface_data = {0};
    PSP_DEVICE_INTERFACE_DETAIL_DATA detail = NULL;
    DWORD required = 0;
    HANDLE handle = INVALID_HANDLE_VALUE;

    if (set == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    interface_data.cbSize = sizeof(interface_data);
    if (!SetupDiEnumDeviceInterfaces(
            set, NULL, &GUID_DEVINTERFACE_APPLE_INPUT_DIAGNOSTIC, 0,
            &interface_data))
        goto out;
    SetupDiGetDeviceInterfaceDetail(set, &interface_data, NULL, 0,
                                    &required, NULL);
    if (!required)
        goto out;
    detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, required);
    if (!detail)
        goto out;
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetail(set, &interface_data, detail,
                                         required, NULL, NULL))
        goto out;
    handle = CreateFile(detail->DevicePath, GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
out:
    if (detail)
        HeapFree(GetProcessHeap(), 0, detail);
    SetupDiDestroyDeviceInfoList(set);
    return handle;
}

static void print_snapshot(const AI_DIAGNOSTIC_SNAPSHOT_V2 *s, int json)
{
    ULONG header_count = s->HeaderWriteIndex;
    ULONG first_sequence;

    if (header_count > AI_PACKET_HEADER_RING_CAPACITY)
        header_count = AI_PACKET_HEADER_RING_CAPACITY;
    first_sequence = s->HeaderWriteIndex - header_count;
    if (json) {
        printf("{\"version\":%lu,\"phase\":%lu,\"interrupts\":%llu,"
               "\"workers_queued\":%llu,\"workers_completed\":%llu,"
               "\"spi_transfers\":%llu,\"spi_timeouts\":%llu,"
               "\"packet_crc_failures\":%llu,\"message_crc_failures\":%llu,"
               "\"fragment_failures\":%llu,\"keyboard_reports\":%llu,"
               "\"trackpad_reports\":%llu,\"resets\":%llu,"
               "\"offline\":%llu,\"headers\":[",
               s->Version, s->TransportPhase,
               (unsigned long long)s->InterruptCount,
               (unsigned long long)s->WorkerQueuedCount,
               (unsigned long long)s->WorkerCompletedCount,
               (unsigned long long)s->SpiTransferCount,
               (unsigned long long)s->SpiTimeoutCount,
               (unsigned long long)s->PacketCrcFailureCount,
               (unsigned long long)s->MessageCrcFailureCount,
               (unsigned long long)s->FragmentFailureCount,
               (unsigned long long)s->KeyboardReportCount,
               (unsigned long long)s->TrackpadReportCount,
               (unsigned long long)s->ResetCount,
               (unsigned long long)s->OfflineCount);
        for (ULONG index = 0; index < header_count; ++index) {
            ULONG sequence = first_sequence + index;
            const AI_PACKET_HEADER_V1 *header =
                &s->Headers[sequence % AI_PACKET_HEADER_RING_CAPACITY];

            printf("%s{\"sequence\":%llu,\"result\":%lu,"
                   "\"flags\":%u,\"device\":%u,\"offset\":%u,"
                   "\"remaining\":%u,\"length\":%u}",
                   index ? "," : "",
                   (unsigned long long)header->Sequence, header->Result,
                   header->Flags, header->Device, header->Offset,
                   header->Remaining, header->Length);
        }
        printf("],\"message\":{\"phase\":%lu,\"type\":%u,"
               "\"report\":%u,\"device\":%u,\"id\":%u,"
               "\"response_length\":%u,\"payload_length\":%u}}\n",
               s->MessagePhase, s->MessageType, s->MessageReport,
               s->MessageDevice, s->MessageId, s->MessageResponseLength,
               s->MessagePayloadLength);
        return;
    }
    printf("AppleInput snapshot v%lu phase=%lu\n", s->Version,
           s->TransportPhase);
    printf("irq=%llu worker=%llu/%llu spi=%llu timeout=%llu reset=%llu offline=%llu\n",
           (unsigned long long)s->InterruptCount,
           (unsigned long long)s->WorkerCompletedCount,
           (unsigned long long)s->WorkerQueuedCount,
           (unsigned long long)s->SpiTransferCount,
           (unsigned long long)s->SpiTimeoutCount,
           (unsigned long long)s->ResetCount,
           (unsigned long long)s->OfflineCount);
    printf("crc(packet/message)=%llu/%llu fragments=%llu reports(kbd/tp)=%llu/%llu\n",
           (unsigned long long)s->PacketCrcFailureCount,
           (unsigned long long)s->MessageCrcFailureCount,
           (unsigned long long)s->FragmentFailureCount,
           (unsigned long long)s->KeyboardReportCount,
           (unsigned long long)s->TrackpadReportCount);
    printf("message phase=%lu type=%02x report=%02x device=%02x id=%u response=%u payload=%u\n",
           s->MessagePhase, s->MessageType, s->MessageReport,
           s->MessageDevice, s->MessageId, s->MessageResponseLength,
           s->MessagePayloadLength);
    for (ULONG index = 0; index < header_count; ++index) {
        ULONG sequence = first_sequence + index;
        const AI_PACKET_HEADER_V1 *header =
            &s->Headers[sequence % AI_PACKET_HEADER_RING_CAPACITY];

        printf("header[%llu] result=%lu flags=%02x device=%02x off=%u remain=%u len=%u\n",
               (unsigned long long)header->Sequence, header->Result,
               header->Flags, header->Device, header->Offset,
               header->Remaining, header->Length);
    }
}

int wmain(int argc, wchar_t **argv)
{
    AI_DIAGNOSTIC_SNAPSHOT_V2 snapshot = {0};
    DWORD returned = 0;
    int json = argc == 3 && wcscmp(argv[2], L"--json") == 0;
    HANDLE handle;

    if (argc < 2 || wcscmp(argv[1], L"status") != 0 || argc > 3 ||
        (argc == 3 && !json)) {
        fwprintf(stderr, L"usage: AppleInputDiag.exe status [--json]\n");
        return 2;
    }
    handle = open_diagnostic_interface();
    if (handle == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"AppleInput diagnostic interface is unavailable (%lu)\n",
                 GetLastError());
        return 1;
    }
    if (!DeviceIoControl(handle, IOCTL_AI_GET_SNAPSHOT, NULL, 0,
                         &snapshot, sizeof(snapshot), &returned, NULL)) {
        fwprintf(stderr, L"snapshot request failed (%lu)\n", GetLastError());
        CloseHandle(handle);
        return 1;
    }
    CloseHandle(handle);
    if (returned != sizeof(snapshot) ||
        snapshot.Version != AI_DIAGNOSTIC_SNAPSHOT_VERSION_2 ||
        snapshot.Size != sizeof(snapshot)) {
        fwprintf(stderr, L"unsupported snapshot response\n");
        return 1;
    }
    print_snapshot(&snapshot, json);
    return 0;
}
