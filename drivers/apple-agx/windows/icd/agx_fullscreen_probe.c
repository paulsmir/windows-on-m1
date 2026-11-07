/* D3D-FS1 checkpoint 2: a fullscreen flip swap chain in the requested format
 * on the AGX output. Prints the HRESULT of every step; presents a solid
 * colour for 10 s so a screenshot can check the channel order.
 * Usage: agx_fullscreen_probe [rgba|bgra] */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <string.h>

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    return DefWindowProcW(hwnd, msg, w, l);
}

int main(int argc, char **argv) {
    const int bgra = argc > 1 && !strcmp(argv[1], "bgra");
    const DXGI_FORMAT format = bgra ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"AgxFullscreenProbe";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"AgxFullscreenProbe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 640, 400,
                              NULL, NULL, wc.hInstance, NULL);
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGIDevice *dxgi = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGISwapChain1 *chain = NULL;
    D3D_FEATURE_LEVEL level = 0;
    HRESULT hr = hwnd ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, &level, &context);
    printf("FS device hr=0x%08lx level 0x%x\n", (unsigned long)hr, (unsigned)level);
    if (SUCCEEDED(hr)) hr = ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi);
    if (SUCCEEDED(hr)) hr = IDXGIDevice_GetAdapter(dxgi, &adapter);
    if (SUCCEEDED(hr)) {
        DXGI_ADAPTER_DESC desc;
        IDXGIAdapter_GetDesc(adapter, &desc);
        printf("FS adapter %ls\n", desc.Description);
        hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);
    }
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC1 sd = {0};
        sd.Width = 2560;
        sd.Height = 1600;
        sd.Format = format;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fd = {0};
        fd.RefreshRate.Numerator = 60;
        fd.RefreshRate.Denominator = 1;
        fd.Windowed = TRUE;
        hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)device, hwnd, &sd, &fd, NULL, &chain);
        printf("FS swapchain %s hr=0x%08lx\n", bgra ? "B8G8R8A8" : "R8G8B8A8", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        /* games pick a mode of the swap chain's format before going
         * fullscreen; the AGX output's only mode, in that format */
        DXGI_MODE_DESC mode = {0};
        mode.Width = 2560;
        mode.Height = 1600;
        mode.RefreshRate.Numerator = 266630000;
        mode.RefreshRate.Denominator = 4443844;
        mode.Format = format;
        HRESULT rt = IDXGISwapChain1_ResizeTarget(chain, &mode);
        printf("FS ResizeTarget hr=0x%08lx\n", (unsigned long)rt);
    }
    if (SUCCEEDED(hr)) {
        /* DXGI_ERROR_NOT_CURRENTLY_AVAILABLE unless the window is in the
         * foreground (a scheduled-task process starts in the background):
         * an Alt press lets SetForegroundWindow take effect */
        for (int attempt = 0; attempt < 10; ++attempt) {
            keybd_event(VK_MENU, 0, 0, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            hr = IDXGISwapChain1_SetFullscreenState(chain, TRUE, NULL);
            printf("FS SetFullscreenState(TRUE) attempt %d foreground %d hr=0x%08lx\n", attempt,
                   GetForegroundWindow() == hwnd, (unsigned long)hr);
            if (hr != DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) break;
            Sleep(500);
        }
    }
    if (SUCCEEDED(hr)) {
        hr = IDXGISwapChain1_ResizeBuffers(chain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
        printf("FS ResizeBuffers hr=0x%08lx\n", (unsigned long)hr);
        IDXGIOutput *output = NULL;
        if (SUCCEEDED(IDXGISwapChain1_GetContainingOutput(chain, &output))) {
            DXGI_MODE_DESC want = {0}, got = {0};
            want.Width = 2560; want.Height = 1600; want.Format = format;
            HRESULT mh = IDXGIOutput_FindClosestMatchingMode(output, &want, &got, (IUnknown *)device);
            printf("FS closest mode hr=0x%08lx %ux%u format %u refresh %u/%u\n", (unsigned long)mh, got.Width, got.Height,
                   (unsigned)got.Format, got.RefreshRate.Numerator, got.RefreshRate.Denominator);
            IDXGIOutput_Release(output);
        }
    }
    UINT presented = 0, failed = 0, reentries = 0;
    HRESULT last = S_OK;
    if (SUCCEEDED(hr)) {
        /* red; a channel swap shows blue */
        const FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        const DWORD end = GetTickCount() + 10000;
        while ((LONG)(end - GetTickCount()) > 0) {
            ID3D11Texture2D *back = NULL;
            ID3D11RenderTargetView *rtv = NULL;
            HRESULT fh = IDXGISwapChain1_GetBuffer(chain, 0, &IID_ID3D11Texture2D, (void **)&back);
            if (SUCCEEDED(fh)) fh = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)back, NULL, &rtv);
            if (SUCCEEDED(fh)) {
                ID3D11DeviceContext_ClearRenderTargetView(context, rtv, red);
                fh = IDXGISwapChain1_Present(chain, 1, 0);
            }
            if (rtv) ID3D11RenderTargetView_Release(rtv);
            if (back) ID3D11Texture2D_Release(back);
            if (SUCCEEDED(fh)) {
                ++presented;
            } else {
                if (!failed) printf("FS first present failure hr=0x%08lx after %u\n", (unsigned long)fh, presented);
                ++failed;
                last = fh;
                /* a focus change ends exclusive fullscreen; flip-model chains
                 * then need ResizeBuffers before the next Present */
                BOOL now = FALSE;
                IDXGISwapChain1_GetFullscreenState(chain, &now, NULL);
                if (!now && reentries < 20) {
                    ++reentries;
                    SetForegroundWindow(hwnd);
                    HRESULT sh = IDXGISwapChain1_SetFullscreenState(chain, TRUE, NULL);
                    HRESULT rh = IDXGISwapChain1_ResizeBuffers(chain, 0, 0, 0, DXGI_FORMAT_UNKNOWN,
                                                               DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
                    printf("FS re-enter %u after %u presents: SetFullscreenState 0x%08lx ResizeBuffers 0x%08lx\n",
                           reentries, presented, (unsigned long)sh, (unsigned long)rh);
                }
            }
            if (failed > 1000) break;
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        }
        BOOL fullscreen = FALSE;
        IDXGISwapChain1_GetFullscreenState(chain, &fullscreen, NULL);
        printf("FS presents %u failed %u reentries %u last 0x%08lx fullscreen %d\n", presented, failed, reentries, (unsigned long)last, fullscreen);
        IDXGISwapChain1_SetFullscreenState(chain, FALSE, NULL);
    }
    printf("AGX_FULLSCREEN_%s: %s\n", bgra ? "BGRA" : "RGBA", SUCCEEDED(hr) && presented > 100 ? "PASS" : "FAIL");
    if (chain) IDXGISwapChain1_Release(chain);
    if (factory) IDXGIFactory2_Release(factory);
    if (adapter) IDXGIAdapter_Release(adapter);
    if (dxgi) IDXGIDevice_Release(dxgi);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
    return 0;
}
