/* Dedicated bounded multi-process cache-pressure checkpoint.
 * Reuse the established pixel-checked triangle without changing its admission
 * or the ordinary probe. Compile/link with the ordinary probe's SDK/libraries
 * plus advapi32.lib. All devices remain alive until the final receipt check. */
#define main agx_ordinary_probe_unused_main
#include "agx_d3d12_probe.c"
#undef main

#define POOL_WORKERS 48
#define POOL_DEVICES 1

static int pool_receipt(DWORD value[12]) {
    HKEY root = NULL;
    DWORD i = 0, bytes, type;
    char instance[256], path[512];
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\ACPI\\APPL0002",
                      0, KEY_READ, &root) != ERROR_SUCCESS) return 0;
    for (;;) {
        HKEY key = NULL;
        DWORD chars = sizeof(instance);
        LONG rc = RegEnumKeyExA(root, i++, instance, &chars, NULL, NULL, NULL, NULL);
        if (rc != ERROR_SUCCESS) break;
        for (unsigned parameter = 0; parameter < 2; ++parameter) {
            snprintf(path, sizeof(path), "%s%s", instance, parameter ? "\\Device Parameters" : "");
            if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
            bytes = 12 * sizeof(DWORD); type = 0;
            rc = RegQueryValueExA(key, "Wom1G3PrivatePressure", NULL, &type, (BYTE *)value, &bytes);
            RegCloseKey(key);
            if (rc == ERROR_SUCCESS && type == REG_BINARY && bytes == 12 * sizeof(DWORD) && value[0] == 1) {
                RegCloseKey(root); return 1;
            }
        }
    }
    RegCloseKey(root); memset(value, 0, 12 * sizeof(DWORD)); return 0;
}

static int pool_worker(const char *release_name, const char *ready_name) {
    ID3D12Device *devices[POOL_DEVICES] = {0};
    ID3D12CommandQueue *queues[POOL_DEVICES] = {0};
    ID3D12Fence *fences[POOL_DEVICES] = {0};
    IDXGIFactory1 *factory = NULL; IDXGIAdapter1 *adapter = NULL;
    HANDLE release = OpenEventA(SYNCHRONIZE, FALSE, release_name);
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, ready_name);
    D3D12_COMMAND_QUEUE_DESC qdesc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    int failed = 1;
    if (!release || !ready || FAILED(hr)) goto done;
    for (UINT n = 0; ; ++n) {
        DXGI_ADAPTER_DESC1 desc;
        if (IDXGIFactory1_EnumAdapters1(factory, n, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc)) && desc.VendorId == 0x4c505041) break;
        IDXGIAdapter1_Release(adapter); adapter = NULL;
    }
    if (!adapter) goto done;
    failed = 0;
    for (unsigned n = 0; n < POOL_DEVICES; ++n) {
        hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&devices[n]);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandQueue(devices[n], &qdesc, &IID_ID3D12CommandQueue, (void **)&queues[n]);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateFence(devices[n], 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fences[n]);
        if (FAILED(hr) || draw_triangle(devices[n], queues[n], fences[n], 0, 0)) { failed = 1; break; }
    }
    printf("KMD_POOL worker pid=%lu devices=%u result=%s\n", GetCurrentProcessId(), POOL_DEVICES, failed ? "FAIL" : "PASS");
    if (!failed) {
        SetEvent(ready);
        if (WaitForSingleObject(release, 160000) != WAIT_OBJECT_0) failed = 1;
    }
done:
    for (unsigned n = 0; n < POOL_DEVICES; ++n) {
        if (fences[n]) ID3D12Fence_Release(fences[n]);
        if (queues[n]) ID3D12CommandQueue_Release(queues[n]);
        if (devices[n]) ID3D12Device_Release(devices[n]);
    }
    if (adapter) IDXGIAdapter1_Release(adapter);
    if (factory) IDXGIFactory1_Release(factory);
    if (release) CloseHandle(release);
    if (ready) CloseHandle(ready);
    return failed;
}

int main(int argc, char **argv) {
    PROCESS_INFORMATION workers[POOL_WORKERS] = {{0}};
    DWORD before[12] = {0}, after[12] = {0}, count = 0;
    HANDLE release; char executable[MAX_PATH], release_name[96], ready_name[96], command[2048];
    int failed = 0, witnessed = 0;
    ULONGLONG started = GetTickCount64();
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 4 && !strcmp(argv[1], "--worker")) return pool_worker(argv[2], argv[3]);
    GetModuleFileNameA(NULL, executable, sizeof(executable));
    snprintf(release_name, sizeof(release_name), "Local\\AgxKmdPoolRelease%lu", GetCurrentProcessId());
    release = CreateEventA(NULL, TRUE, FALSE, release_name);
    if (!release) { printf("AGX_D3D12_KMD_POOL: FAIL event\n"); return 1; }
    pool_receipt(before);
    for (unsigned n = 0; n < POOL_WORKERS && !failed; ++n) {
        STARTUPINFOA startup = {sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        if (GetTickCount64() - started > 110000) { failed = 1; break; }
        HANDLE ready, wait[2];
        snprintf(ready_name, sizeof(ready_name), "Local\\AgxKmdPoolReady%lu_%u", GetCurrentProcessId(), n);
        ready = CreateEventA(NULL, TRUE, FALSE, ready_name);
        snprintf(command, sizeof(command), "\"%s\" --worker %s %s", executable, release_name, ready_name);
        if (!ready || !CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &workers[n])) {
            if (ready) CloseHandle(ready); failed = 1; break;
        }
        ++count; wait[0] = ready; wait[1] = workers[n].hProcess;
        if (WaitForMultipleObjects(2, wait, FALSE, 20000) != WAIT_OBJECT_0) failed = 1;
        CloseHandle(ready);
        if (pool_receipt(after) && after[1] > before[1] && after[2] != after[3] &&
            after[4] <= 1024 && after[4] > after[5] && after[4] - after[5] == after[6] && after[6] &&
            (after[9] || after[10])) witnessed = 1;
        if (witnessed) break; /* The discriminator is reached; do not keep adding load. */
    }
    printf("KMD_POOL receipt version=%lu count=%lu before_count=%lu requester=%lu victim=%lu units=%lu->%lu freed=%lu owner=%lu request_bytes=%lu root=%08lx%08lx largest=%lu\n",
           after[0], after[1], before[1], after[2], after[3], after[4], after[5], after[6], after[7], after[8], after[10], after[9], after[11]);
    SetEvent(release);
    ULONGLONG cleanup_deadline = GetTickCount64() + 20000;
    for (unsigned n = 0; n < count; ++n) {
        DWORD exit_code = 1;
        ULONGLONG now = GetTickCount64();
        DWORD remaining = now < cleanup_deadline ? (DWORD)(cleanup_deadline - now) : 0;
        if (WaitForSingleObject(workers[n].hProcess, remaining) != WAIT_OBJECT_0) {
            TerminateProcess(workers[n].hProcess, 2); failed = 1;

        }
        GetExitCodeProcess(workers[n].hProcess, &exit_code);
        if (exit_code) failed = 1;
        CloseHandle(workers[n].hThread); CloseHandle(workers[n].hProcess);
    }
    CloseHandle(release);
    printf("AGX_D3D12_KMD_POOL: %s workers=%lu foreign_reclaim=%d\n", failed || !witnessed ? "FAIL" : "PASS", count, witnessed);
    return failed || !witnessed;
}
