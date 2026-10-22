/* EXP1876G follow-up: list the display modes DXGI offers per format on every
 * output (Night Raid fullscreen failed with "Display mode list not found for
 * given format"). Prints MODES <adapter> <output> <format> <flags> <count>
 * and the first mode of each list. */
#define COBJMACROS
#include <windows.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdlib.h>

static const struct { DXGI_FORMAT format; const char *name; } formats[] = {
    {DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM"}, {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, "B8G8R8A8_UNORM_SRGB"},
    {DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM"}, {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "R8G8B8A8_UNORM_SRGB"},
    {DXGI_FORMAT_B8G8R8X8_UNORM, "B8G8R8X8_UNORM"}, {DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2_UNORM"},
    {DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT"},
};

int main(void) {
    IDXGIFactory1 *factory = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    printf("factory hr=0x%08lx\n", (unsigned long)hr);
    for (UINT a = 0; SUCCEEDED(hr); ++a) {
        IDXGIAdapter1 *adapter = NULL;
        if (IDXGIFactory1_EnumAdapters1(factory, a, &adapter) != S_OK) break;
        DXGI_ADAPTER_DESC1 ad;
        IDXGIAdapter1_GetDesc1(adapter, &ad);
        printf("ADAPTER %u vendor 0x%04x device 0x%04x flags 0x%x %ls\n", a, ad.VendorId, ad.DeviceId, ad.Flags, ad.Description);
        for (UINT o = 0;; ++o) {
            IDXGIOutput *output = NULL;
            if (IDXGIAdapter1_EnumOutputs(adapter, o, &output) != S_OK) break;
            DXGI_OUTPUT_DESC od;
            IDXGIOutput_GetDesc(output, &od);
            printf("OUTPUT %u %u %ls attached %d rect %ld,%ld-%ld,%ld rotation %d\n", a, o, od.DeviceName, od.AttachedToDesktop,
                   od.DesktopCoordinates.left, od.DesktopCoordinates.top, od.DesktopCoordinates.right, od.DesktopCoordinates.bottom,
                   (int)od.Rotation);
            for (UINT f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f)
                for (UINT flags = 0; flags <= DXGI_ENUM_MODES_SCALING; flags += DXGI_ENUM_MODES_SCALING) {
                    UINT count = 0;
                    HRESULT mh = IDXGIOutput_GetDisplayModeList(output, formats[f].format, flags, &count, NULL);
                    printf("MODES %u %u %s flags %u hr=0x%08lx count %u", a, o, formats[f].name, flags, (unsigned long)mh, count);
                    if (SUCCEEDED(mh) && count) {
                        DXGI_MODE_DESC *modes = (DXGI_MODE_DESC *)calloc(count, sizeof(*modes));
                        if (modes && SUCCEEDED(IDXGIOutput_GetDisplayModeList(output, formats[f].format, flags, &count, modes)))
                            printf(" first %ux%u %u/%u scanline %d scaling %d", modes[0].Width, modes[0].Height,
                                   modes[0].RefreshRate.Numerator, modes[0].RefreshRate.Denominator,
                                   (int)modes[0].ScanlineOrdering, (int)modes[0].Scaling);
                        free(modes);
                    }
                    printf("\n");
                }
            IDXGIOutput_Release(output);
        }
        IDXGIAdapter1_Release(adapter);
    }
    if (factory) IDXGIFactory1_Release(factory);
    return 0;
}
