#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <initguid.h>

#include "apple_input_capture.h"

static HANDLE open_capture_interface(void)
{
    HDEVINFO set = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_APPLE_INPUT_TRACKPAD_CAPTURE, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA interface_data = {0};
    PSP_DEVICE_INTERFACE_DETAIL_DATA detail = NULL;
    DWORD required = 0;
    HANDLE handle = INVALID_HANDLE_VALUE;

    if (set == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    interface_data.cbSize = sizeof(interface_data);
    if (!SetupDiEnumDeviceInterfaces(
            set, NULL, &GUID_DEVINTERFACE_APPLE_INPUT_TRACKPAD_CAPTURE, 0,
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
    handle = CreateFile(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
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
    ULONG index;

    for (index = 0; index < AI_SHA256_DIGEST_SIZE; ++index)
        printf("%02x", digest[index]);
}

static int write_capture(const wchar_t *path,
                         const AI_TRACKPAD_CAPTURE_BLOB *capture)
{
    HANDLE file;
    DWORD written = 0;

    file = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"refusing to overwrite or create output (%lu): %ls\n",
                 GetLastError(), path);
        return 1;
    }
    if (!WriteFile(file, capture, sizeof(*capture), &written, NULL) ||
        written != sizeof(*capture)) {
        fwprintf(stderr, L"capture write failed (%lu)\n", GetLastError());
        CloseHandle(file);
        return 1;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    AI_TRACKPAD_CAPTURE_ARM_REQUEST request = {0};
    AI_TRACKPAD_CAPTURE_BLOB capture = {0};
    const wchar_t *output = NULL;
    DWORD returned = 0;
    DWORD timeout_seconds = 30;
    DWORD elapsed = 0;
    HANDLE handle;
    int count = 0;
    int release_only;
    int index;
    int result = 1;

    release_only = argc >= 2 && wcscmp(argv[1], L"capture-release") == 0;
    if (argc < 4 || (!release_only && wcscmp(argv[1], L"capture") != 0)) {
        fwprintf(stderr, L"usage: AppleInputCapture.exe capture --count N --output PATH [--timeout SECONDS]\n"
                         L"       AppleInputCapture.exe capture-release --output PATH [--timeout SECONDS]\n");
        return 2;
    }
    for (index = 2; index + 1 < argc; index += 2) {
        if (wcscmp(argv[index], L"--count") == 0)
            count = _wtoi(argv[index + 1]);
        else if (wcscmp(argv[index], L"--output") == 0)
            output = argv[index + 1];
        else if (wcscmp(argv[index], L"--timeout") == 0)
            timeout_seconds = (DWORD)_wtoi(argv[index + 1]);
        else {
            fwprintf(stderr, L"unknown argument: %ls\n", argv[index]);
            return 2;
        }
    }
    if (release_only) {
        if (count != 0) {
            fwprintf(stderr, L"capture-release does not accept --count\n");
            return 2;
        }
        count = 1;
        request.Trigger = AI_TRACKPAD_CAPTURE_TRIGGER_RELEASE;
    }
    if (!output || count < 1 || count > AI_TRACKPAD_CAPTURE_MAX_REPORTS ||
        timeout_seconds < 1 || timeout_seconds > 300) {
        fwprintf(stderr, L"count must be 1..16 and timeout 1..300 seconds\n");
        return 2;
    }

    handle = open_capture_interface();
    if (handle == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"capture interface unavailable (%lu); run elevated and use the explicit capture build\n",
                 GetLastError());
        return 1;
    }
    request.Version = AI_TRACKPAD_CAPTURE_VERSION;
    request.ReportLimit = (ULONG)count;
    request.ReportSizeLimit = AI_TRACKPAD_CAPTURE_MAX_REPORT_SIZE;
    if (!DeviceIoControl(handle, IOCTL_AI_TRACKPAD_CAPTURE_ARM,
                         &request, sizeof(request), NULL, 0,
                         &returned, NULL)) {
        fwprintf(stderr, L"capture arm failed (%lu)\n", GetLastError());
        goto out;
    }
    if (release_only)
        printf("armed for one device-2 release candidate; lift the held contact\n");
    else
        printf("armed for %d device-2 reports; perform exactly one controlled gesture\n",
               count);
    while (elapsed < timeout_seconds * 1000u) {
        ZeroMemory(&capture, sizeof(capture));
        if (!DeviceIoControl(handle, IOCTL_AI_TRACKPAD_CAPTURE_READ,
                             NULL, 0, &capture, sizeof(capture),
                             &returned, NULL)) {
            fwprintf(stderr, L"capture read failed (%lu)\n", GetLastError());
            goto cancel;
        }
        if (returned != sizeof(capture) ||
            capture.Version != AI_TRACKPAD_CAPTURE_VERSION ||
            capture.Size != sizeof(capture)) {
            fwprintf(stderr, L"unsupported capture response\n");
            goto cancel;
        }
        if (capture.Complete)
            break;
        Sleep(100);
        elapsed += 100;
    }
    if (!capture.Complete) {
        fwprintf(stderr, L"capture timed out after %lu seconds (%lu reports)\n",
                 timeout_seconds, capture.ReportCount);
        goto cancel;
    }
    if (capture.DroppedCount != 0 || capture.ReportCount != (ULONG)count) {
        fwprintf(stderr, L"capture rejected data: reports=%lu dropped=%lu\n",
                 capture.ReportCount, capture.DroppedCount);
        goto cancel;
    }
    if (write_capture(output, &capture) != 0)
        goto cancel;
    printf("saved %lu reports; descriptor_sha256=",
           capture.ReportCount);
    print_digest(capture.TrackpadDescriptorSha256);
    printf("\n");
    result = 0;
    goto out;

cancel:
    DeviceIoControl(handle, IOCTL_AI_TRACKPAD_CAPTURE_CANCEL,
                    NULL, 0, NULL, 0, &returned, NULL);
out:
    CloseHandle(handle);
    return result;
}
