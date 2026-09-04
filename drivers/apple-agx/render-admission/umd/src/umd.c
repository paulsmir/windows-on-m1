#include <windows.h>
#include <wingdi.h>

/* d3d10umddi.h includes d3dkmddi.h, which expects this kernel-style type. */
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)

BOOL WINAPI
DllMain(
    HINSTANCE Instance,
    DWORD Reason,
    LPVOID Reserved
    )
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
}

HRESULT APIENTRY
OpenAdapter10_2(
    D3D10DDIARG_OPENADAPTER *OpenAdapter
    )
{
    UNREFERENCED_PARAMETER(OpenAdapter);

    /* Admission-only UMD: loadable, correctly exported, and fail-closed. */
    return E_NOTIMPL;
}
