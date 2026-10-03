"""Replay the actual G4 presentation rotation body with two VidMm primaries."""

from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp"
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def rotation_body():
    source = SOURCE.read_text()
    start = source.index("HRESULT AgxD3d10WindowsPresentationRotate(")
    opening = source.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    return source[start:position]


def frontend_rotation_body():
    source = GENERATOR.read_text()
    match = re.search(
        r"\('''_RotateResourceIdentities\(.*?''','''(.*?)'''\)",
        source, re.S)
    if match is None:
        raise AssertionError("native frontend rotation projection missing")
    body = match.group(1).replace(
        "{", "{\n   UINT count = RotateResourceIdentities ? "
        "RotateResourceIdentities->Resources : ~0u;", 1)
    return "HRESULT " + body + "\n}"


SHIM = r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
using UINT = unsigned;
using SIZE_T = size_t;
using HRESULT = long;
using D3DKMT_HANDLE = uint64_t;
using BOOL = int;
enum pipe_format { PIPE_FORMAT_NONE, PIPE_FORMAT_BGRA };
struct pipe_resource { pipe_format format; };
struct pipe_surface { pipe_resource *texture; };
struct pipe_sampler_view { pipe_resource *texture; int descriptor; };
enum mesa_shader_stage { MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT };
struct pipe_context {
  pipe_sampler_view *(*create_sampler_view)(pipe_context *, pipe_resource *,
                                            const pipe_sampler_view *);
  void (*sampler_view_release)(pipe_context *, pipe_sampler_view *);
  void (*set_sampler_views)(pipe_context *, mesa_shader_stage, unsigned, unsigned,
                            unsigned, pipe_sampler_view **);
  void (*set_framebuffer_state)(pipe_context *, const void *);
};
struct Retirement { D3DKMT_HANDLE KernelAllocation; };
struct Transport { uint64_t Token; };
struct RenderBuffer { Transport Transport; };
using AGX_WIN32_SCREEN_BUFFER = RenderBuffer;
struct ADMISSION_UMD_SCREEN_BUFFER {
  BOOL Active, Borrowed, Direct, Transition, SubmissionHolds, SourceHolds;
  uint64_t Token;
  D3DKMT_HANDLE KernelAllocation, StagingAllocation;
};
constexpr UINT ADMISSION_UMD_SCREEN_BUFFER_LIMIT = 4;
constexpr UINT ADMISSION_UMD_RESOURCE_MAGIC = 0x1234;
constexpr int AgxD3d10DeviceReady = 4;
constexpr HRESULT S_OK = 0;
constexpr HRESULT E_INVALIDARG = -1;
constexpr int ERROR_BUSY = 170;
inline HRESULT HRESULT_FROM_WIN32(int) { return -2; }
inline BOOL FAILED(HRESULT value) { return value < 0; }
struct Runtime {
  int ScreenBufferLock;
  ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[ADMISSION_UMD_SCREEN_BUFFER_LIMIT];
};
struct AGX_D3D10_WINDOWS_DEVICE { int Stage; Runtime Runtime; };
struct NativeResource { UINT Magic; Retirement *Retirement; D3DKMT_HANDLE KernelAllocation; };
struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE {
  AGX_D3D10_WINDOWS_DEVICE *Device;
  NativeResource Resource;
  RenderBuffer RenderBuffer;
  pipe_resource *RenderResource;
};
inline HRESULT AgxD3d10WindowsFlushRetire(AGX_D3D10_WINDOWS_DEVICE *) {
  return S_OK;
}
inline void AcquireSRWLockExclusive(int *) {}
inline void ReleaseSRWLockExclusive(int *) {}
inline BOOL presentation_pipe_format(const pipe_resource *resource,
                                     enum pipe_format *format) {
  if (!resource || !format) return 0;
  *format = resource->format;
  return 1;
}
inline pipe_resource *AgxD3d10WindowsPresentationPipeResource(
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *record) {
  return record ? record->RenderResource : nullptr;
}
using HANDLE = void *;
using DXGI_DDI_HRESOURCE = void *;
constexpr unsigned MESA_SHADER_STAGES = 2;
constexpr unsigned PIPE_MAX_SHADER_SAMPLER_VIEWS = 4;
constexpr unsigned PIPE_MAX_COLOR_BUFS = 2;
struct Device {
  AGX_D3D10_WINDOWS_DEVICE *windows;
  pipe_context *pipe;
  pipe_sampler_view *sampler_views[MESA_SHADER_STAGES][PIPE_MAX_SHADER_SAMPLER_VIEWS];
  struct { unsigned nr_cbufs; pipe_surface cbufs[PIPE_MAX_COLOR_BUFS]; } fb;
};
struct ShaderResourceView;
struct RenderTargetView;
struct Resource {
  Device *owner_device;
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation;
  pipe_resource *resource;
  ShaderResourceView *first_presentation_srv;
  RenderTargetView *first_presentation_rtv;
};
struct ShaderResourceView {
  pipe_sampler_view *handle;
  Device *owner_device;
  Resource *owner_resource;
  ShaderResourceView *next_presentation;
};
struct RenderTargetView {
  pipe_surface surface;
  Resource *owner_resource;
  RenderTargetView *next_presentation;
};
struct DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES {
  HANDLE hDevice;
  UINT Resources;
  DXGI_DDI_HRESOURCE *pResources;
};
inline Device *CastDevice(HANDLE handle) { return static_cast<Device *>(handle); }
inline Resource *CastResource(HANDLE handle) { return static_cast<Resource *>(handle); }
inline HANDLE GetProcessHeap() { return nullptr; }
constexpr unsigned HEAP_ZERO_MEMORY = 8;
constexpr HRESULT E_OUTOFMEMORY = -3;
inline BOOL SUCCEEDED(HRESULT value) { return value >= 0; }
inline void *HeapAlloc(HANDLE, unsigned, size_t bytes) { return calloc(1, bytes); }
inline BOOL HeapFree(HANDLE, unsigned, void *address) { free(address); return 1; }
inline void pipe_resource_reference(pipe_resource **destination,
                                    pipe_resource *source) {
  *destination = source;
}
static unsigned created_views, released_views, sampler_rebinds, framebuffer_rebinds;
inline pipe_sampler_view *create_view(pipe_context *, pipe_resource *resource,
                                     const pipe_sampler_view *original) {
  auto *created = static_cast<pipe_sampler_view *>(calloc(1, sizeof(pipe_sampler_view)));
  if (created) {
    created->texture = resource;
    created->descriptor = original->descriptor;
    ++created_views;
  }
  return created;
}
inline void release_view(pipe_context *, pipe_sampler_view *view) {
  ++released_views;
  free(view);
}
inline void bind_views(pipe_context *, mesa_shader_stage, unsigned, unsigned,
                       unsigned, pipe_sampler_view **) { ++sampler_rebinds; }
inline void bind_framebuffer(pipe_context *, const void *) {
  ++framebuffer_rebinds;
}
"""


MAIN = r"""
int main() {
  AGX_D3D10_WINDOWS_DEVICE device{};
  device.Stage = AgxD3d10DeviceReady;
  for (unsigned i = 0; i < 2; ++i) {
    auto &slot = device.Runtime.ScreenBuffers[i];
    slot.Active = slot.Borrowed = 1;
    slot.Token = i + 1;
#ifdef APPLE_AGX_GPUVA_WINSYS
#ifdef TEST_DIRECT_PRIMARY
    slot.Direct = 1;
    slot.KernelAllocation = 101 + i;
    slot.StagingAllocation = 0;
#else
    slot.KernelAllocation = 201 + i;
    slot.StagingAllocation = 101 + i;
#endif
#else
    slot.KernelAllocation = 101 + i;
#endif
  }
  pipe_resource targets[2]{{PIPE_FORMAT_BGRA}, {PIPE_FORMAT_BGRA}};
  Retirement retirements[2]{{101}, {102}};
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE records[2]{};
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *order[2]{&records[0], &records[1]};
  for (unsigned i = 0; i < 2; ++i) {
    records[i].Device = &device;
    records[i].Resource.Magic = ADMISSION_UMD_RESOURCE_MAGIC;
    records[i].Resource.Retirement = &retirements[i];
    records[i].Resource.KernelAllocation = 101 + i;
    records[i].RenderBuffer.Transport.Token = i + 1;
    records[i].RenderResource = &targets[i];
  }
  // Real busy ownership still blocks rotation without partially mutating it.
  for (auto flag : {&device.Runtime.ScreenBuffers[0].Transition,
                    &device.Runtime.ScreenBuffers[0].SubmissionHolds,
                    &device.Runtime.ScreenBuffers[0].SourceHolds}) {
    *flag = 1;
    assert(AgxD3d10WindowsPresentationRotate(&device, order, 2) ==
           HRESULT_FROM_WIN32(ERROR_BUSY));
    assert(records[0].Resource.KernelAllocation == 101);
    assert(records[1].Resource.KernelAllocation == 102);
    *flag = 0;
  }
#if defined(APPLE_AGX_GPUVA_WINSYS) && !defined(TEST_DIRECT_PRIMARY)
  auto &identity = device.Runtime.ScreenBuffers[0].StagingAllocation;
#else
  auto &identity = device.Runtime.ScreenBuffers[0].KernelAllocation;
#endif
  identity = 999;
  assert(AgxD3d10WindowsPresentationRotate(&device, order, 2) ==
         HRESULT_FROM_WIN32(ERROR_BUSY));
  assert(records[0].Resource.KernelAllocation == 101);
  identity = 101;
  assert(AgxD3d10WindowsPresentationRotate(&device, order, 2) == S_OK);
  assert(records[0].Resource.KernelAllocation == 102);
  assert(records[0].Resource.Retirement->KernelAllocation == 102);
  assert(records[0].RenderBuffer.Transport.Token == 2);
  assert(records[0].RenderResource == &targets[1]);
  assert(records[1].Resource.KernelAllocation == 101);
  assert(records[1].RenderBuffer.Transport.Token == 1);
  assert(records[1].RenderResource == &targets[0]);
#ifdef APPLE_AGX_GPUVA_WINSYS
#ifdef TEST_DIRECT_PRIMARY
  assert(device.Runtime.ScreenBuffers[0].KernelAllocation == 101);
  assert(device.Runtime.ScreenBuffers[1].KernelAllocation == 102);
  assert(device.Runtime.ScreenBuffers[0].StagingAllocation == 0);
  assert(device.Runtime.ScreenBuffers[1].StagingAllocation == 0);
#else
  assert(device.Runtime.ScreenBuffers[0].KernelAllocation == 201);
  assert(device.Runtime.ScreenBuffers[1].KernelAllocation == 202);
  assert(device.Runtime.ScreenBuffers[0].StagingAllocation == 101);
  assert(device.Runtime.ScreenBuffers[1].StagingAllocation == 102);
#endif
#else
  assert(device.Runtime.ScreenBuffers[0].KernelAllocation == 101);
  assert(device.Runtime.ScreenBuffers[1].KernelAllocation == 102);
#endif
  pipe_context pipe{create_view, release_view, bind_views, bind_framebuffer};
  Device frontendDevice{};
  frontendDevice.windows = &device;
  frontendDevice.pipe = &pipe;
  Resource frontend[2]{};
  for (unsigned i = 0; i < 2; ++i) {
    frontend[i].owner_device = &frontendDevice;
    frontend[i].presentation = &records[i];
    frontend[i].resource = records[i].RenderResource;
  }
  auto *oldView = static_cast<pipe_sampler_view *>(calloc(1, sizeof(pipe_sampler_view)));
  assert(oldView);
  oldView->texture = frontend[0].resource;
  oldView->descriptor = 77;
  ShaderResourceView srv{oldView, &frontendDevice, &frontend[0], nullptr};
  RenderTargetView rtv{{frontend[0].resource}, &frontend[0], nullptr};
  frontend[0].first_presentation_srv = &srv;
  frontend[0].first_presentation_rtv = &rtv;
  frontendDevice.sampler_views[0][0] = oldView;
  frontendDevice.fb.nr_cbufs = 1;
  frontendDevice.fb.cbufs[0].texture = frontend[0].resource;
  DXGI_DDI_HRESOURCE handles[2]{&frontend[0], &frontend[1]};
  DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES args{&frontendDevice, 2, handles};
  assert(_RotateResourceIdentities(&args) == S_OK);
  assert(frontend[0].resource == records[0].RenderResource);
  assert(frontend[1].resource == records[1].RenderResource);
  assert(rtv.surface.texture == frontend[0].resource);
  assert(srv.handle->texture == frontend[0].resource);
  assert(srv.handle->descriptor == 77);
  assert(frontendDevice.sampler_views[0][0] == srv.handle);
  assert(frontendDevice.fb.cbufs[0].texture == frontend[0].resource);
  assert(created_views == 1 && released_views == 1);
  assert(sampler_rebinds == 1 && framebuffer_rebinds == 1);
  assert(records[0].RenderResource == &targets[0]);
  assert(records[1].RenderResource == &targets[1]);
  return 0;
}
"""


class G4PrimaryRotationReplay(unittest.TestCase):
    def test_rotation_keeps_render_target_with_presented_allocation(self):
        self.replay(False)

    def test_gpuva_rotation_preserves_split_canonical_and_original_identity(self):
        self.replay(True)

    def test_gpuva_rotation_accepts_direct_primary_without_staging(self):
        self.replay(True, direct=True)

    def replay(self, gpuva, direct=False):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "rotation.cpp"
            binary = Path(directory) / "rotation"
            source.write_text(("#define APPLE_AGX_GPUVA_WINSYS 1\n" if gpuva else "") +
                              ("#define TEST_DIRECT_PRIMARY 1\n" if direct else "") +
                              "#include <initializer_list>\n" +
                              SHIM + rotation_body() +
                              frontend_rotation_body() + MAIN)
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17",
                            "-Wall", "-Wextra", "-Werror", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
