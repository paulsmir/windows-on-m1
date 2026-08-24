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

static void print_digest(const UCHAR digest[AI_SHA256_DIGEST_SIZE])
{
    for (ULONG index = 0; index < AI_SHA256_DIGEST_SIZE; ++index)
        printf("%02x", digest[index]);
}

static void print_snapshot(const AI_DIAGNOSTIC_SNAPSHOT_V4 *s, int json)
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
               "\"response_length\":%u,\"payload_length\":%u},"
               "\"keyboard_descriptor_length\":%u,"
               "\"trackpad_descriptor_length\":%u,"
               "\"keyboard_contract_valid\":%u,"
               "\"trackpad_init_phase\":%u,"
               "\"trackpad_init_retries\":%u,"
               "\"trackpad_init_attempts\":%u,"
               "\"descriptor_digest_status\":%lu,"
               "\"keyboard_vhf_state\":%lu,"
               "\"keyboard_reports_accepted\":%llu,"
               "\"keyboard_reports_rejected\":%llu,"
               "\"keyboard_reports_submitted\":%llu,"
               "\"keyboard_vhf_submission_failures\":%llu,"
               "\"keyboard_vhf_start_failures\":%llu,"
               "\"keyboard_vhf_last_status\":%ld,"
               "\"trackpad_axis_x_valid\":%u,"
               "\"trackpad_axis_y_valid\":%u,"
               "\"trackpad_logical_x_minimum\":%ld,"
               "\"trackpad_logical_x_maximum\":%ld,"
               "\"trackpad_logical_y_minimum\":%ld,"
               "\"trackpad_logical_y_maximum\":%ld,"
               "\"trackpad_physical_x_minimum\":%ld,"
               "\"trackpad_physical_x_maximum\":%ld,"
               "\"trackpad_physical_y_minimum\":%ld,"
               "\"trackpad_physical_y_maximum\":%ld,"
               "\"trackpad_unit\":%lu,"
               "\"trackpad_unit_exponent\":%d,"
               "\"trackpad_vhf_state\":%lu,"
               "\"trackpad_reports_decoded\":%llu,"
               "\"trackpad_reports_rejected\":%llu,"
               "\"trackpad_reports_submitted\":%llu,"
               "\"trackpad_vhf_submission_failures\":%llu,"
               "\"trackpad_vhf_start_failures\":%llu,"
               "\"trackpad_last_rejection\":%lu,"
               "\"trackpad_active\":%u,"
               "\"trackpad_admitted\":%u,"
               "\"trackpad_suppressed\":%u,"
               "\"trackpad_get_feature\":%llu,"
               "\"trackpad_set_feature\":%llu,"
               "\"trackpad_feature_last_status\":%ld,"
               "\"trackpad_vhf_last_status\":%ld,"
               "\"keyboard_descriptor_sha256\":\"",
               s->MessagePhase, s->MessageType, s->MessageReport,
               s->MessageDevice, s->MessageId, s->MessageResponseLength,
               s->MessagePayloadLength, s->KeyboardDescriptorLength,
               s->TrackpadDescriptorLength, s->KeyboardContractValid,
               s->TrackpadInitPhase, s->TrackpadInitRetryCount,
               s->TrackpadInitAttemptCount,
               s->DescriptorDigestStatus, s->KeyboardVhfState,
               (unsigned long long)s->KeyboardReportAcceptedCount,
               (unsigned long long)s->KeyboardReportRejectedCount,
               (unsigned long long)s->KeyboardReportSubmittedCount,
               (unsigned long long)s->KeyboardVhfSubmissionFailureCount,
               (unsigned long long)s->KeyboardVhfStartFailureCount,
               s->KeyboardVhfLastStatus,
               s->TrackpadAxisXValid, s->TrackpadAxisYValid,
               s->TrackpadLogicalXMinimum, s->TrackpadLogicalXMaximum,
               s->TrackpadLogicalYMinimum, s->TrackpadLogicalYMaximum,
               s->TrackpadPhysicalXMinimum, s->TrackpadPhysicalXMaximum,
               s->TrackpadPhysicalYMinimum, s->TrackpadPhysicalYMaximum,
               s->TrackpadUnit, s->TrackpadUnitExponent,
               s->TrackpadVhfState,
               (unsigned long long)s->TrackpadReportDecodedCount,
               (unsigned long long)s->TrackpadReportRejectedCount,
               (unsigned long long)s->TrackpadReportSubmittedCount,
               (unsigned long long)s->TrackpadVhfSubmissionFailureCount,
               (unsigned long long)s->TrackpadVhfStartFailureCount,
               s->TrackpadLastRejection,
               s->TrackpadActiveCount, s->TrackpadAdmittedCount,
               s->TrackpadSuppressedCount,
               (unsigned long long)s->TrackpadGetFeatureCount,
               (unsigned long long)s->TrackpadSetFeatureCount,
               s->TrackpadFeatureLastStatus, s->TrackpadVhfLastStatus);
        print_digest(s->KeyboardDescriptorSha256);
        printf("\",\"trackpad_descriptor_sha256\":\"");
        print_digest(s->TrackpadDescriptorSha256);
        printf("\"}\n");
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
    printf("descriptors keyboard=%u trackpad=%u keyboard_contract=%u digest_status=%08lx\n",
           s->KeyboardDescriptorLength, s->TrackpadDescriptorLength,
           s->KeyboardContractValid, s->DescriptorDigestStatus);
    printf("trackpad_init phase=%u retries=%u attempts=%u\n",
           s->TrackpadInitPhase, s->TrackpadInitRetryCount,
           s->TrackpadInitAttemptCount);
    printf("trackpad axes=%u/%u logical=[%ld,%ld]x[%ld,%ld] physical=[%ld,%ld]x[%ld,%ld] unit=%08lx exponent=%d\n",
           s->TrackpadAxisXValid, s->TrackpadAxisYValid,
           s->TrackpadLogicalXMinimum, s->TrackpadLogicalXMaximum,
           s->TrackpadLogicalYMinimum, s->TrackpadLogicalYMaximum,
           s->TrackpadPhysicalXMinimum, s->TrackpadPhysicalXMaximum,
           s->TrackpadPhysicalYMinimum, s->TrackpadPhysicalYMaximum,
           s->TrackpadUnit, s->TrackpadUnitExponent);
    printf("trackpad vhf=%lu decoded=%llu rejected=%llu submitted=%llu submit_failures=%llu start_failures=%llu rejection=%lu active/admitted/suppressed=%u/%u/%u feature(get/set)=%llu/%llu feature_status=%08lx vhf_status=%08lx\n",
           s->TrackpadVhfState,
           (unsigned long long)s->TrackpadReportDecodedCount,
           (unsigned long long)s->TrackpadReportRejectedCount,
           (unsigned long long)s->TrackpadReportSubmittedCount,
           (unsigned long long)s->TrackpadVhfSubmissionFailureCount,
           (unsigned long long)s->TrackpadVhfStartFailureCount,
           s->TrackpadLastRejection,
           s->TrackpadActiveCount, s->TrackpadAdmittedCount,
           s->TrackpadSuppressedCount,
           (unsigned long long)s->TrackpadGetFeatureCount,
           (unsigned long long)s->TrackpadSetFeatureCount,
           (ULONG)s->TrackpadFeatureLastStatus,
           (ULONG)s->TrackpadVhfLastStatus);
    printf("vhf state=%lu accepted=%llu rejected=%llu submitted=%llu submit_failures=%llu start_failures=%llu last_status=%08lx\n",
           s->KeyboardVhfState,
           (unsigned long long)s->KeyboardReportAcceptedCount,
           (unsigned long long)s->KeyboardReportRejectedCount,
           (unsigned long long)s->KeyboardReportSubmittedCount,
           (unsigned long long)s->KeyboardVhfSubmissionFailureCount,
           (unsigned long long)s->KeyboardVhfStartFailureCount,
           (ULONG)s->KeyboardVhfLastStatus);
    printf("keyboard_descriptor_sha256=");
    print_digest(s->KeyboardDescriptorSha256);
    printf("\ntrackpad_descriptor_sha256=");
    print_digest(s->TrackpadDescriptorSha256);
    printf("\n");
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
    AI_DIAGNOSTIC_SNAPSHOT_V4 snapshot = {0};
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
        snapshot.Version != AI_DIAGNOSTIC_SNAPSHOT_VERSION_4 ||
        snapshot.Size != sizeof(snapshot)) {
        fwprintf(stderr, L"unsupported snapshot response\n");
        return 1;
    }
    print_snapshot(&snapshot, json);
    return 0;
}
