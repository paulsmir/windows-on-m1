/* D3D9 route probe (x86): which runtime path a 32-bit D3D9 application takes
 * on the AGX adapter -- Direct3DCreate9 (default) and Direct3DCreate9On12
 * (explicit D3D9On12) -- with the adapter identity, device creation, a clear,
 * a readback of the back buffer and a present. The UMD trace
 * (APPLE_AGX_UMD_TRACE_FILE) shows which driver entry each path opened.
 * Then a fixed-function draw (AGX_D3D9_<path>_DRAW): a pre-transformed
 * (XYZRHW | DIFFUSE) red triangle over the centre, which D3D9On12 turns into
 * its own shaders on the D3D12 UMD, read back at the centre.
 * C66 (AGX_D3D9_<path>_MSAA_PRESENT): a shown window with a 4x back buffer
 * (EXP1899 Portal 2: D3D9On12 presents it through the D3D12 Present DDI,
 * which refused the multisampled source), cleared and presented three
 * times; PASS when every present succeeds and the device is not lost. The
 * window's front-buffer centre is printed (GetFrontBufferData). */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <stdio.h>
#include <stdlib.h>

static HWND make_window(void) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "agx_d3d9_probe";
    RegisterClassA(&wc);
    return CreateWindowA("agx_d3d9_probe", "agx_d3d9_probe", WS_OVERLAPPEDWINDOW, 100, 100, 160, 160, NULL, NULL,
                         wc.hInstance, NULL);
}

struct agx_d3d9_vertex { float x, y, z, rhw; DWORD color; };

static DWORD center_pixel(IDirect3DDevice9 *device, HRESULT *hr) {
    IDirect3DSurface9 *back = NULL, *sys = NULL;
    D3DLOCKED_RECT lr = {0};
    DWORD pixel = 0;
    *hr = IDirect3DDevice9_GetBackBuffer(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    if (SUCCEEDED(*hr))
        *hr = IDirect3DDevice9_CreateOffscreenPlainSurface(device, 64, 64, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL);
    if (SUCCEEDED(*hr)) *hr = IDirect3DDevice9_GetRenderTargetData(device, back, sys);
    if (SUCCEEDED(*hr)) *hr = IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY);
    if (SUCCEEDED(*hr)) {
        pixel = ((const DWORD *)lr.pBits)[32 * (lr.Pitch / 4) + 32] & 0xffffffu;
        IDirect3DSurface9_UnlockRect(sys);
    }
    if (sys) IDirect3DSurface9_Release(sys);
    if (back) IDirect3DSurface9_Release(back);
    return pixel;
}

static int draw(const char *tag, IDirect3DDevice9 *device) {
    const struct agx_d3d9_vertex triangle[3] = {
        {4.0f, 4.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(255, 0, 0)},
        {60.0f, 4.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(255, 0, 0)},
        {32.0f, 60.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(255, 0, 0)}};
    DWORD pixel = 0;
    HRESULT hr = IDirect3DDevice9_Clear(device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_SetRenderState(device, D3DRS_LIGHTING, FALSE);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, D3DZB_FALSE);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_BeginScene(device);
    if (SUCCEEDED(hr)) {
        hr = IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLELIST, 1, triangle, sizeof(triangle[0]));
        HRESULT end = IDirect3DDevice9_EndScene(device);
        if (SUCCEEDED(hr)) hr = end;
    }
    if (SUCCEEDED(hr)) pixel = center_pixel(device, &hr);
    printf("%s draw+readback hr=0x%08lx pixel 0x%06lx\n", tag, (unsigned long)hr, (unsigned long)pixel);
    printf("AGX_D3D9_%s_DRAW: %s\n", tag, SUCCEEDED(hr) && pixel == 0xff0000 ? "PASS" : "FAIL");
    return !(SUCCEEDED(hr) && pixel == 0xff0000);
}

static int msaa_present(const char *tag, IDirect3D9 *d3d, HWND window) {
    D3DPRESENT_PARAMETERS pp = {0};
    IDirect3DDevice9 *device = NULL;
    IDirect3DSurface9 *front = NULL;
    D3DDISPLAYMODE mode = {0};
    POINT origin = {32, 32};
    DWORD pixel = 0;
    unsigned frame;
    HRESULT hr = IDirect3D9_CheckDeviceMultiSampleType(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE,
                                                       D3DMULTISAMPLE_4_SAMPLES, NULL);
    printf("%s msaa check hr=0x%08lx\n", tag, (unsigned long)hr);
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
    pp.hDeviceWindow = window;
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    if (SUCCEEDED(hr))
        hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                     D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    printf("%s msaa CreateDevice hr=0x%08lx\n", tag, (unsigned long)hr);
    for (frame = 0; SUCCEEDED(hr) && frame < 3; ++frame) {
        hr = IDirect3DDevice9_Clear(device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 255), 1.0f, 0);
        if (SUCCEEDED(hr)) hr = IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL);
        printf("%s msaa present %u hr=0x%08lx\n", tag, frame, (unsigned long)hr);
        Sleep(50);
    }
    {
        /* APPLE_AGX_D3D9_HOLD_MS: keep presenting magenta so a desktop
         * screenshot can show whether the window is composed (the front-buffer
         * pixel reads the same on Basic Display and AGX, EXP1906E-basic). */
        char hold_text[16] = {0};
        DWORD hold = GetEnvironmentVariableA("APPLE_AGX_D3D9_HOLD_MS", hold_text, sizeof(hold_text)) ? strtoul(hold_text, NULL, 10) : 0;
        DWORD start = GetTickCount(), held = 0;
        while (SUCCEEDED(hr) && hold && GetTickCount() - start < hold) {
            MSG msg;
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
            hr = IDirect3DDevice9_Clear(device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 255), 1.0f, 0);
            if (SUCCEEDED(hr)) hr = IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL);
            ++held;
            Sleep(16);
        }
        if (hold) printf("%s msaa hold %lu ms presents %lu hr=0x%08lx\n", tag, (unsigned long)hold, (unsigned long)held, (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_TestCooperativeLevel(device);
    printf("%s msaa cooperative hr=0x%08lx\n", tag, (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        HRESULT fr = IDirect3D9_GetAdapterDisplayMode(d3d, D3DADAPTER_DEFAULT, &mode);
        D3DLOCKED_RECT lr = {0};
        ClientToScreen(window, &origin);
        if (SUCCEEDED(fr))
            fr = IDirect3DDevice9_CreateOffscreenPlainSurface(device, mode.Width, mode.Height, D3DFMT_A8R8G8B8,
                                                             D3DPOOL_SYSTEMMEM, &front, NULL);
        if (SUCCEEDED(fr)) fr = IDirect3DDevice9_GetFrontBufferData(device, 0, front);
        if (SUCCEEDED(fr)) fr = IDirect3DSurface9_LockRect(front, &lr, NULL, D3DLOCK_READONLY);
        if (SUCCEEDED(fr) && origin.x >= 0 && origin.y >= 0 && (UINT)origin.x < mode.Width && (UINT)origin.y < mode.Height)
            pixel = ((const DWORD *)lr.pBits)[origin.y * (lr.Pitch / 4) + origin.x] & 0xffffffu;
        if (SUCCEEDED(fr)) IDirect3DSurface9_UnlockRect(front);
        printf("%s msaa front hr=0x%08lx pixel 0x%06lx at %ld,%ld\n", tag, (unsigned long)fr, (unsigned long)pixel,
               origin.x, origin.y);
    }
    if (front) IDirect3DSurface9_Release(front);
    if (device) IDirect3DDevice9_Release(device);
    ShowWindow(window, SW_HIDE);
    printf("AGX_D3D9_%s_MSAA_PRESENT: %s\n", tag, SUCCEEDED(hr) ? "PASS" : "FAIL");
    return FAILED(hr);
}

static int run(const char *tag, IDirect3D9 *d3d, HWND window) {
    D3DADAPTER_IDENTIFIER9 id = {0};
    D3DPRESENT_PARAMETERS pp = {0};
    IDirect3DDevice9 *device = NULL;
    DWORD pixel = 0;
    int failures;
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
    if (SUCCEEDED(hr)) pixel = center_pixel(device, &hr);
    printf("%s clear+readback hr=0x%08lx pixel 0x%06lx\n", tag, (unsigned long)hr, (unsigned long)pixel);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL);
    printf("%s present hr=0x%08lx\n", tag, (unsigned long)hr);
    printf("AGX_D3D9_%s: %s\n", tag, SUCCEEDED(hr) && pixel == 0x00ff00 ? "PASS" : "FAIL");
    failures = !(SUCCEEDED(hr) && pixel == 0x00ff00);
    failures += draw(tag, device);
    IDirect3DDevice9_Release(device);
    return failures + msaa_present(tag, d3d, window);
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
