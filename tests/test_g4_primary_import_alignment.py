"""Exercise the G4 primary-import call with the measured 16 KiB device page."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp"


def function_body():
    source = SOURCE.read_text()
    start = source.index("static HRESULT attach_presentation_render_resource(")
    opening = source.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    return source[start:position]


SHIM = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
using HRESULT = long;
using UINT = unsigned;
using APPLE_AGX_U32 = uint32_t;
using APPLE_AGX_U64 = uint64_t;
constexpr HRESULT S_OK = 0;
constexpr HRESULT E_FAIL = -1;
constexpr HRESULT E_INVALIDARG = -2;
constexpr int TRUE = 1;
inline bool FAILED(HRESULT result) { return result < 0; }
enum D3DDDIFORMAT { D3DDDIFMT_A8R8G8B8 = 21 };
enum AGX_WIN32_ASAHI_LINEAR_FORMAT { AgxWin32AsahiLinearFormatBgra8Unorm };
struct ADMISSION_ALLOCATION_DESCRIPTION {
  uint32_t Width, Height, Pitch, Format;
  uint64_t Size;
  uint32_t CpuVisible;
};
struct AGX_WIN32_SCREEN_BUFFER { uint64_t Token; };
struct pipe_resource { int marker; };
struct AGX_D3D10_WINDOWS_DEVICE {
  struct {
    struct { struct { uint64_t PageBytes; } Info; } Screen;
    HRESULT LastScreenError;
  } Runtime;
  pipe_resource *Screen;
};
struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE {
  struct {
    struct { ADMISSION_ALLOCATION_DESCRIPTION Allocation; } DirectFlip;
    uint64_t KernelAllocation;
  } Resource;
  AGX_WIN32_SCREEN_BUFFER RenderBuffer;
  pipe_resource *RenderResource;
};
static uint64_t capturedAlignment;
static int capturedDirect = -1;
inline bool presentation_linear_format(D3DDDIFORMAT format,
                                       AGX_WIN32_ASAHI_LINEAR_FORMAT *out) {
  if (format != D3DDDIFMT_A8R8G8B8) return false;
  *out = AgxWin32AsahiLinearFormatBgra8Unorm;
  return true;
}
constexpr UINT AgxWin32BufferClassGeneral = 1;
constexpr UINT AppleAgxWin32BufferCpuRead = 1;
constexpr UINT AppleAgxWin32BufferCpuWrite = 2;
constexpr UINT AppleAgxWin32BufferGpuRead = 4;
constexpr UINT AppleAgxWin32BufferGpuWrite = 8;
inline HRESULT AdmissionUmdScreenAdoptAllocation(
    void *, uint64_t, uint64_t, uint64_t alignment, UINT, UINT,
    int, int direct, AGX_WIN32_SCREEN_BUFFER *buffer) {
  capturedAlignment = alignment;
  capturedDirect = direct;
  buffer->Token = 1;
  return S_OK;
}
static pipe_resource target{1};
inline pipe_resource *AgxWin32AsahiImportLinearColor32(
    pipe_resource *, AGX_WIN32_SCREEN_BUFFER *, UINT, UINT, UINT, uint64_t,
    AGX_WIN32_ASAHI_LINEAR_FORMAT) { return &target; }
inline int AgxWin32ScreenDestroyBuffer(void *, AGX_WIN32_SCREEN_BUFFER *) {
  return 0;
}
#define ZeroMemory(address, bytes) memset((address), 0, (bytes))
"""


MAIN = r"""
int main() {
  AGX_D3D10_WINDOWS_DEVICE device{};
  device.Runtime.Screen.Info.PageBytes = 0x4000;
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE record{};
  auto &desc = record.Resource.DirectFlip.Allocation;
  desc.Width = 2560;
  desc.Height = 1600;
  desc.Pitch = 10240;
  desc.Size = 16384000;
  desc.Format = D3DDDIFMT_A8R8G8B8;
  record.Resource.KernelAllocation = 7;
  assert(attach_presentation_render_resource(&device, &record) == S_OK);
  assert(record.RenderResource == &target);
  assert(capturedAlignment == 0x10000);
  assert(capturedDirect == 1); /* R158: CpuVisible=0 presentation renders directly */
  return 0;
}
"""


class G4PrimaryImportAlignment(unittest.TestCase):
    def test_primary_import_requests_64k_alignment(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "primary.cpp"
            binary = Path(directory) / "primary"
            source.write_text(SHIM + function_body() + MAIN)
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17",
                            "-DAPPLE_AGX_GPUVA_WINSYS", "-Wall", "-Wextra",
                            "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
