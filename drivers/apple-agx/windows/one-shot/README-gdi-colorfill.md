# AppleAgxGdiColorFill

This small ARM64 console program asks Windows GDI to produce one color fill into
a new, private 16-by-16 device-dependent bitmap. It is a routing discriminator.
It does not establish acceleration, pixel correctness, AGX completion, or a
Windows fence. It does not modify or present onto the existing desktop.

The sibling legacy `AppleAgxOneShot` helper and the kernel driver are unchanged.
This producer creates no private graphics allocation/context packet, TestContext,
or fabricated D3DKMTRender call. Its only D3DKMT calls open an adapter from the
named display HDC and close that owned adapter handle.

## Build gate

Use the existing ARM64 build environment with Windows SDK/WDK `10.0.26100.0`,
the installed MSVC 14.44 compiler, and the `v143` toolset. The project requests
level-4 warnings as errors and code analysis. Supply the exact installed
`VCToolsVersion` through the existing build invocation when selecting 14.44;
`v143` alone does not pin a particular compiler installation.

```powershell
msbuild .\AppleAgxGdiColorFill.vcxproj /t:Build /p:Configuration=Release /p:Platform=ARM64 /p:RunCodeAnalysis=true
```

Source review is not a build or runtime pass. The parent experiment owns the
build, hash recording, staging, and runtime qualification. Do not run a drawing
candidate before normal production admission, the target adapter's Code 0,
stable SSH/storage, and its exact display/LUID mapping have been established.

## Read-only discovery

```powershell
.\AppleAgxGdiColorFill.exe
.\AppleAgxGdiColorFill.exe --list
```

Both forms enumerate display names, state flags, and eligibility. For every
display, the program attempts to create an explicitly named display DC, reads
its adapter LUID and VidPn source ID, then closes its owned handles. There are
no bitmap, brush, or drawing operations in this mode. Unavailable HDC identities
are logged; an unavailable attached non-mirroring target makes the exit status
nonzero. Detached displays may legitimately have no usable HDC identity.

## Explicit single draw

```powershell
.\AppleAgxGdiColorFill.exe --draw '\\.\DISPLAY1' 0x00000000 0x00000000
```

The display and LUID above are placeholders, not a proposed hardware command.
Replace all three with the exact current values established by discovery and
the experiment's adapter evidence. LUID words are unsigned 32-bit values; decimal
or `0x` hexadecimal is accepted. Empty values, signs, leading whitespace,
trailing characters, and overflow are rejected. No device is selected implicitly.

Before creating drawing objects, the program requires the exact enumerated
display to be attached to the desktop, non-mirroring, and associated with the
supplied high/low LUID through `D3DKMTOpenAdapterFromHdc`. A changed LUID, missing
display, or detached/mirroring target ends the request without drawing.

The program creates a compatible memory DC and a 16-by-16 DDB using the original
display DC. It checks `GetObjectW` for one plane, 32 bits per pixel, and a 64-byte
pitch. A different format is rejected without drawing. It selects an owned solid
`RGB(0x11, 0x22, 0x33)` brush and issues exactly one
`PatBlt(memoryDC, 0, 0, 16, 16, PATCOPY)`, followed by `GdiFlush`. Whether those
calls succeed or fail, it requests a three-second hold with the objects alive
and records the actual elapsed time; scheduling can extend that interval.

Cleanup restores the original selections, deletes its memory DC, bitmap and
brush, closes the opened adapter, and deletes the display DC. It never deletes
the original stock selections. If restoration and DC deletion both fail, an
object still selected into that DC is retained and the cleanup failure is logged;
the program does not falsely report that it deleted it.

Every record includes a UTC Windows FILETIME, expressed as 100-nanosecond ticks
since 1601-01-01 UTC. Logs report actual API return values, NTSTATUS values,
bitmap geometry, adapter identity, hold duration, and cleanup results. A successful
API or flush record is not a GPU completion receipt. The OS may implement this
operation in software or choose a different underlying command, color/alpha
representation, or subrectangle shape. Actual KMD routing and AGX/Windows fence
evidence must be collected separately by the experiment.

Exit codes: `0` means listing completed or the single GDI draw/flush and cleanup
calls succeeded; `1` means an API/cleanup or enumeration failure; `2` means invalid
arguments; `3` means a target identity/state or bitmap format rejection. Cleanup
failure takes precedence and returns `1`.

## Primary API references

- [CreateCompatibleBitmap](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatiblebitmap): use the original display DC to avoid the new memory DC's monochrome default.
- [CreateDCW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdcw) and [EnumDisplayDevicesW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaydevicesw): use enumerated device names explicitly.
- [D3DKMT_OPENADAPTERFROMHDC](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_openadapterfromhdc): obtain the HDC's adapter LUID and VidPn source ID.
- [PatBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-patblt) and [GdiFlush](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush): issue the pattern copy and flush the current thread's batch.
- [GetObjectW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getobjectw), [SelectObject](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-selectobject), and [DeleteObject](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-deleteobject): inspect the owned bitmap and restore selections before deleting owned objects.
