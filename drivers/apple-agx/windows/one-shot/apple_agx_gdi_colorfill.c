/* A Windows-originated GDI routing probe. API success is not GPU completion. */
#include <windows.h>
#include <d3dkmthk.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define DRAW_WIDTH 16
#define DRAW_HEIGHT 16
#define DRAW_PITCH 64
#define HOLD_MS 3000u
#define MAX_DISPLAYS 256u

static ULONGLONG utc_filetime(void)
{
    FILETIME time;
    ULARGE_INTEGER value;
    GetSystemTimePreciseAsFileTime(&time);
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

static void log_event(_Printf_format_string_ const wchar_t *format, ...)
{
    va_list args;
    wprintf(L"utc_filetime=%llu ", utc_filetime());
    va_start(args, format);
    vwprintf(format, args);
    va_end(args);
    wprintf(L"\n");
    fflush(stdout);
}

static BOOL parse_word(const wchar_t *text, DWORD *word)
{
    wchar_t *end;
    unsigned long long value;
    if (!text || !*text || *text == L'-' || *text == L'+' || iswspace(*text))
        return FALSE;
    errno = 0;
    value = wcstoull(text, &end,
                    text[0] == L'0' && (text[1] == L'x' || text[1] == L'X') ? 16 : 10);
    if (errno == ERANGE || end == text || *end || value > 0xffffffffULL)
        return FALSE;
    *word = (DWORD)value;
    return TRUE;
}

static BOOL attached_nonmirror(const DISPLAY_DEVICEW *display)
{
    return (display->StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0 &&
           (display->StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) == 0;
}

static BOOL close_adapter(D3DKMT_HANDLE handle)
{
    D3DKMT_CLOSEADAPTER close = {0};
    NTSTATUS status;
    if (!handle)
        return TRUE;
    close.hAdapter = handle;
    status = D3DKMTCloseAdapter(&close);
    log_event(L"api=D3DKMTCloseAdapter adapter=0x%08lx status=0x%08lx",
              (ULONG)handle, (ULONG)status);
    return NT_SUCCESS(status);
}

static BOOL delete_dc(HDC dc, const wchar_t *kind)
{
    BOOL result;
    if (!dc)
        return TRUE;
    result = DeleteDC(dc);
    log_event(L"api=DeleteDC kind=%ls dc=%p result=%d", kind, (void *)dc, result);
    return result;
}

static BOOL open_display(const wchar_t *name, HDC *dc,
                         D3DKMT_OPENADAPTERFROMHDC *adapter)
{
    NTSTATUS status;
    /* Both parameters name the enumerated device; NULL could choose a primary. */
    *dc = CreateDCW(name, name, NULL, NULL);
    log_event(L"api=CreateDCW display=\"%ls\" dc=%p", name, (void *)*dc);
    if (!*dc)
        return FALSE;
    ZeroMemory(adapter, sizeof(*adapter));
    adapter->hDc = *dc;
    status = D3DKMTOpenAdapterFromHdc(adapter);
    log_event(L"api=D3DKMTOpenAdapterFromHdc display=\"%ls\" status=0x%08lx "
              L"adapter=0x%08lx luid_high=0x%08lx luid_low=0x%08lx source_id=%u",
              name, (ULONG)status, (ULONG)adapter->hAdapter,
              (DWORD)adapter->AdapterLuid.HighPart, adapter->AdapterLuid.LowPart,
              (unsigned int)adapter->VidPnSourceId);
    if (!NT_SUCCESS(status)) {
        /* The handle is an output only on success; do not close an undefined
         * failure output as if this process owned an opened adapter. */
        adapter->hAdapter = 0;
        return FALSE;
    }
    return adapter->hAdapter != 0;
}

static int list_displays(void)
{
    DWORD index;
    int result = 0;
    for (index = 0; index < MAX_DISPLAYS; ++index) {
        DISPLAY_DEVICEW display = {0};
        D3DKMT_OPENADAPTERFROMHDC adapter = {0};
        HDC dc = NULL;
        BOOL found;
        display.cb = sizeof(display);
        found = EnumDisplayDevicesW(NULL, index, &display, 0);
        log_event(L"api=EnumDisplayDevicesW index=%lu result=%d", index, found);
        if (!found)
            break;
        log_event(L"event=display name=\"%ls\" description=\"%ls\" "
                  L"state=0x%08lx attached_nonmirror=%d",
                  display.DeviceName, display.DeviceString, display.StateFlags,
                  attached_nonmirror(&display));
        if (!open_display(display.DeviceName, &dc, &adapter)) {
            log_event(L"event=display_identity_unavailable display=\"%ls\"", display.DeviceName);
            if (attached_nonmirror(&display))
                result = 1;
        }
        if (!close_adapter(adapter.hAdapter))
            result = 1;
        if (!delete_dc(dc, L"display"))
            result = 1;
    }
    if (!index || index == MAX_DISPLAYS) {
        log_event(L"event=enumeration_rejected count=%lu limit=%u", index, MAX_DISPLAYS);
        result = 1;
    }
    log_event(L"event=list_complete display_count=%lu result=%d draw_calls=0", index, result);
    return result;
}

static BOOL find_display(const wchar_t *name, DISPLAY_DEVICEW *target)
{
    DWORD index;
    for (index = 0; index < MAX_DISPLAYS; ++index) {
        DISPLAY_DEVICEW display = {0};
        BOOL found;
        display.cb = sizeof(display);
        found = EnumDisplayDevicesW(NULL, index, &display, 0);
        log_event(L"api=EnumDisplayDevicesW index=%lu result=%d", index, found);
        if (!found)
            break;
        if (_wcsicmp(display.DeviceName, name) == 0) {
            *target = display;
            log_event(L"event=target_found name=\"%ls\" state=0x%08lx attached_nonmirror=%d",
                      display.DeviceName, display.StateFlags, attached_nonmirror(&display));
            return attached_nonmirror(&display);
        }
    }
    return FALSE;
}

static BOOL delete_object(HGDIOBJ object, const wchar_t *kind, BOOL still_selected)
{
    BOOL result;
    if (!object)
        return TRUE;
    if (still_selected) {
        log_event(L"event=cleanup_object_retained kind=%ls object=%p reason=still_selected",
                  kind, (void *)object);
        return FALSE;
    }
    result = DeleteObject(object);
    log_event(L"api=DeleteObject kind=%ls object=%p result=%d", kind, (void *)object, result);
    return result;
}

static int draw_once(const wchar_t *name, DWORD high, DWORD low)
{
    DISPLAY_DEVICEW display = {0};
    D3DKMT_OPENADAPTERFROMHDC adapter = {0};
    HDC display_dc = NULL, memory_dc = NULL;
    HBITMAP bitmap = NULL;
    HBRUSH brush = NULL;
    HGDIOBJ old_bitmap = NULL, old_brush = NULL, previous;
    BITMAP format = {0};
    BOOL bitmap_selected = FALSE, brush_selected = FALSE;
    BOOL painted = FALSE, flushed = FALSE, cleanup_ok = TRUE;
    unsigned int draw_calls = 0;
    ULONGLONG hold_start;
    int bytes, result = 1;

    if (!find_display(name, &display)) {
        log_event(L"event=target_rejected name=\"%ls\" reason=not_attached_nonmirror", name);
        result = 3;
        goto cleanup;
    }
    if (!open_display(display.DeviceName, &display_dc, &adapter))
        goto cleanup;
    if ((DWORD)adapter.AdapterLuid.HighPart != high || adapter.AdapterLuid.LowPart != low) {
        log_event(L"event=target_rejected reason=luid_mismatch expected_high=0x%08lx expected_low=0x%08lx",
                  high, low);
        result = 3;
        goto cleanup;
    }
    log_event(L"event=target_verified display=\"%ls\" luid_high=0x%08lx luid_low=0x%08lx",
              display.DeviceName, high, low);

    memory_dc = CreateCompatibleDC(display_dc);
    log_event(L"api=CreateCompatibleDC display_dc=%p memory_dc=%p", (void *)display_dc, (void *)memory_dc);
    if (!memory_dc)
        goto cleanup;
    /* The new memory DC initially contains a monochrome bitmap. Use the
     * ORIGINAL display DC here to create a device-dependent color bitmap. */
    bitmap = CreateCompatibleBitmap(display_dc, DRAW_WIDTH, DRAW_HEIGHT);
    log_event(L"api=CreateCompatibleBitmap display_dc=%p bitmap=%p width=%d height=%d",
              (void *)display_dc, (void *)bitmap, DRAW_WIDTH, DRAW_HEIGHT);
    if (!bitmap)
        goto cleanup;
    bytes = GetObjectW(bitmap, (int)sizeof(format), &format);
    log_event(L"api=GetObjectW result_bytes=%d type=%ld width=%ld height=%ld pitch=%ld planes=%u bpp=%u",
              bytes, format.bmType, format.bmWidth, format.bmHeight, format.bmWidthBytes,
              (unsigned int)format.bmPlanes, (unsigned int)format.bmBitsPixel);
    if (bytes != (int)sizeof(format))
        goto cleanup;
    if (format.bmType != 0 || format.bmWidth != DRAW_WIDTH || format.bmHeight != DRAW_HEIGHT ||
        format.bmWidthBytes != DRAW_PITCH || format.bmPlanes != 1 || format.bmBitsPixel != 32) {
        log_event(L"event=format_rejected required=16x16_32bpp_pitch64");
        result = 3;
        goto cleanup;
    }
    old_bitmap = SelectObject(memory_dc, bitmap);
    log_event(L"api=SelectObject kind=bitmap previous=%p", (void *)old_bitmap);
    if (!old_bitmap || old_bitmap == HGDI_ERROR)
        goto cleanup;
    bitmap_selected = TRUE;
    brush = CreateSolidBrush(RGB(0x11, 0x22, 0x33));
    log_event(L"api=CreateSolidBrush brush=%p red=0x11 green=0x22 blue=0x33", (void *)brush);
    if (!brush)
        goto cleanup;
    old_brush = SelectObject(memory_dc, brush);
    log_event(L"api=SelectObject kind=brush previous=%p", (void *)old_brush);
    if (!old_brush || old_brush == HGDI_ERROR)
        goto cleanup;
    brush_selected = TRUE;

    log_event(L"event=draw_begin operation=PatBlt rop=PATCOPY x=0 y=0 width=16 height=16");
    ++draw_calls;
    painted = PatBlt(memory_dc, 0, 0, DRAW_WIDTH, DRAW_HEIGHT, PATCOPY);
    log_event(L"api=PatBlt result=%d", painted);
    flushed = GdiFlush();
    log_event(L"api=GdiFlush result=%d gpu_completion=unproven", flushed);
    hold_start = GetTickCount64();
    log_event(L"event=hold_begin requested_ms=%u", HOLD_MS);
    Sleep(HOLD_MS);
    log_event(L"event=hold_returned elapsed_ms=%llu", GetTickCount64() - hold_start);
    result = painted && flushed ? 0 : 1;

cleanup:
    if (brush_selected) {
        previous = SelectObject(memory_dc, old_brush);
        log_event(L"api=SelectObject kind=restore_brush previous=%p expected_owned=%p",
                  (void *)previous, (void *)brush);
        if (previous && previous != HGDI_ERROR)
            brush_selected = FALSE;
        if (previous != (HGDIOBJ)brush)
            cleanup_ok = FALSE;
    }
    if (bitmap_selected) {
        previous = SelectObject(memory_dc, old_bitmap);
        log_event(L"api=SelectObject kind=restore_bitmap previous=%p expected_owned=%p",
                  (void *)previous, (void *)bitmap);
        if (previous && previous != HGDI_ERROR)
            bitmap_selected = FALSE;
        if (previous != (HGDIOBJ)bitmap)
            cleanup_ok = FALSE;
    }
    /* Delete the owned memory DC before the owned drawing objects. If a
     * restoration failed, successful DC deletion still removes its selections. */
    if (delete_dc(memory_dc, L"memory")) {
        bitmap_selected = FALSE;
        brush_selected = FALSE;
    } else {
        cleanup_ok = FALSE;
    }
    if (!delete_object((HGDIOBJ)brush, L"brush", brush_selected))
        cleanup_ok = FALSE;
    if (!delete_object((HGDIOBJ)bitmap, L"bitmap", bitmap_selected))
        cleanup_ok = FALSE;
    if (!close_adapter(adapter.hAdapter))
        cleanup_ok = FALSE;
    if (!delete_dc(display_dc, L"display"))
        cleanup_ok = FALSE;
    if (!cleanup_ok)
        result = 1;
    log_event(L"event=draw_complete result=%d draw_calls=%u patblt=%d gdiflush=%d cleanup_ok=%d "
              L"gpu_completion=unproven", result, draw_calls, painted, flushed, cleanup_ok);
    return result;
}

int __cdecl wmain(int argc, wchar_t **argv)
{
    DWORD high = 0, low = 0;
    log_event(L"event=begin tool=AppleAgxGdiColorFill gpu_acceleration=unproven");
    if (argc == 1 || (argc == 2 && wcscmp(argv[1], L"--list") == 0))
        return list_displays();
    if (argc == 5 && wcscmp(argv[1], L"--draw") == 0 &&
        parse_word(argv[3], &high) && parse_word(argv[4], &low))
        return draw_once(argv[2], high, low);
    fwprintf(stderr, L"usage: AppleAgxGdiColorFill.exe [--list]\n"
                     L"       AppleAgxGdiColorFill.exe --draw <display> <luid-high> <luid-low>\n"
                     L"LUID words must be unsigned 32-bit decimal or 0x-prefixed hexadecimal.\n");
    log_event(L"event=arguments_rejected result=2 draw_calls=0");
    return 2;
}
