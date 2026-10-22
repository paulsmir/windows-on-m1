/* D3D9 route probe (x86): which runtime path a 32-bit D3D9 application takes
 * on the AGX adapter -- Direct3DCreate9 (default) and Direct3DCreate9On12
 * (explicit D3D9On12) -- with the adapter identity, device creation, a clear,
 * a readback of the back buffer and a present. The UMD trace
 * (APPLE_AGX_UMD_TRACE_FILE) shows which driver entry each path opened. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <stdio.h>

static HWND make_window(void) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "agx_d3d9_probe";
    RegisterClassA(&wc);
    return CreateWindowA("agx_d3d9_probe", "agx_d3d9_probe", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL,
                         wc.hInstance, NULL);
}

static int run(const char *tag, IDirect3D9 *d3d, HWND window) {
    D3DADAPTER_IDENTIFIER9 id = {0};
    D3DPRESENT_PARAMETERS pp = {0};
    IDirect3DDevice9 *device = NULL;
    IDirect3DSurface9 *back = NULL, *sys = NULL;
    D3DLOCKED_RECT lr = {0};
    DWORD pixel = 0;
    HRESULT hr = IDirect3D9_GetAdapterIdentifier(d3d, D3DADAPTER_DEFAULT, 0, &id);
    printf("%s adapter hr=0x%08lx vendor 0x%04lx device 0x%04lx driver %s desc %s\n", tag, (unsigned long)hr,
           (unsigned long)id.VendorId, (unsigned long)id.DeviceId, id.Driver, id.Description);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.hDeviceWindow = window;
    hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                 D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    printf("%s CreateDevice hr=0x%08lx\n", tag, (unsigned long)hr);
    if (FAILED(hr)) {
        printf("AGX_D3D9_%s: FAIL\n", tag);
        return 1;
    }
    hr = IDirect3DDevice9_Clear(device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 255, 0), 1.0f, 0);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_GetBackBuffer(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    if (SUCCEEDED(hr))
        hr = IDirect3DDevice9_CreateOffscreenPlainSurface(device, 64, 64, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_GetRenderTargetData(device, back, sys);
    if (SUCCEEDED(hr)) hr = IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        pixel = ((const DWORD *)lr.pBits)[32 * (lr.Pitch / 4) + 32] & 0xffffffu;
        IDirect3DSurface9_UnlockRect(sys);
    }
    printf("%s clear+readback hr=0x%08lx pixel 0x%06lx\n", tag, (unsigned long)hr, (unsigned long)pixel);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL);
    printf("%s present hr=0x%08lx\n", tag, (unsigned long)hr);
    if (sys) IDirect3DSurface9_Release(sys);
    if (back) IDirect3DSurface9_Release(back);
    IDirect3DDevice9_Release(device);
    printf("AGX_D3D9_%s: %s\n", tag, SUCCEEDED(hr) && pixel == 0x00ff00 ? "PASS" : "FAIL");
    return !(SUCCEEDED(hr) && pixel == 0x00ff00);
}

int main(void) {
    HWND window = make_window();
    int failures = 0;
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    printf("DEFAULT Direct3DCreate9 %p\n", (void *)d3d);
    if (d3d) {
        failures += run("DEFAULT", d3d, window);
        IDirect3D9_Release(d3d);
    } else {
        printf("AGX_D3D9_DEFAULT: FAIL\n");
        ++failures;
    }
    D3D9ON12_ARGS args = {0};
    args.Enable9On12 = TRUE;
    d3d = Direct3DCreate9On12(D3D_SDK_VERSION, &args, 1);
    printf("ON12 Direct3DCreate9On12 %p\n", (void *)d3d);
    if (d3d) {
        failures += run("ON12", d3d, window);
        IDirect3D9_Release(d3d);
    } else {
        printf("AGX_D3D9_ON12: FAIL\n");
        ++failures;
    }
    DestroyWindow(window);
    return failures;
}
