"""Extract the first real Windows Gallium/Asahi runtime closure blocker.

This is a source-preserving compiler probe: it neither installs a driver nor
submits work. Compiler-side Asahi objects are supplied by the already proven
AD04 fullcompiler artifact; runtime source remains current pinned Mesa.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--project', required=True, type=Path)
parser.add_argument('--architecture', choices=('x64', 'arm64'), default='x64')
args = parser.parse_args()
root = Path(r'C:\Users\pauls\AD04-fullcompiler-001')
mesa = Path(r'C:\Users\pauls\AD04-d3d10-frontend-build\mesa')
build = root / ('nir-' + args.architecture)
clang = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin')
generated = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated')
out = root / ('asahi-runtime-' + args.architecture + '-' + uuid.uuid4().hex)
out.mkdir()

def argv(command):
    argc = ctypes.c_int()
    fn = ctypes.windll.shell32.CommandLineToArgvW
    fn.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    fn.restype = ctypes.POINTER(ctypes.c_wchar_p)
    result = fn(command, ctypes.byref(argc))
    try:
        return [result[i] for i in range(argc.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(result, ctypes.c_void_p))

commands = json.loads((build / 'compile_commands.json').read_text())
base = next(c for c in commands if c['file'].replace('\\', '/').endswith('/nir.c'))
flags = [a for a in argv(base['command'])[1:] if not a.startswith(('-I','/I','/Fo','/Fd'))
         and a not in ('/c','/showIncludes','/Zi',base['file'])]
for directory in ('src/asahi', 'src/gallium/drivers/asahi', 'include/drm-uapi'):
    shutil.copytree(mesa / directory, out / directory)

# Reuse the proven compile-only Windows include overlay. DRM transport APIs are
# still not supplied: subsequent diagnostics must name each real dependency.
batch = out / 'src/gallium/drivers/asahi/agx_batch.c'
batch_text = batch.read_text()
if batch_text.count('#include <xf86drm.h>') != 1:
    raise SystemExit('Pinned agx_batch DRM include anchor mismatch')
batch.write_text(batch_text.replace('#include <xf86drm.h>',
    '#ifndef _WIN32\n#include <xf86drm.h>\n#endif'))
device = out / 'src/asahi/lib/agx_device.h'
device_text = device.read_text()
if device_text.count('#include <xf86drm.h>') != 1 or device_text.count('#include "vdrm.h"\n\n#include "asahi_proto.h"') != 1:
    raise SystemExit('Pinned agx_device Windows overlay anchors mismatch')
device_text = device_text.replace('#include <xf86drm.h>',
    '#ifndef _WIN32\n#include <xf86drm.h>\n#else\n#include <stddef.h>\n#include "c11/threads.h"\ntypedef ptrdiff_t ssize_t;\n#endif')
device_text = device_text.replace('#include "vdrm.h"\n\n#include "asahi_proto.h"',
    '#ifndef _WIN32\n#include "vdrm.h"\n#include "asahi_proto.h"\n#else\nstruct vdrm_device;\nstruct asahi_ccmd_submit_res;\n#endif')
device.write_text(device_text)
drm = out / 'include/drm-uapi/drm.h'
drm_text = drm.read_text()
anchor = '#if defined(__GNU__)\n#include <sys/ioctl.h>'
if drm_text.count(anchor) != 1:
    raise SystemExit('Pinned drm Windows overlay anchor mismatch')
drm.write_text(drm_text.replace(anchor,
    '#if defined(_WIN32)\n/* Declarations only; Windows has no DRM ioctl transport. */\n#elif defined(__GNU__)\n#include <sys/ioctl.h>'))
asahi_drm = out / 'include/drm-uapi/asahi_drm.h'
asahi_text = asahi_drm.read_text()
begin = '#define DRM_IOCTL_ASAHI(__access, __id, __type)'
end = '#if defined(__cplusplus)\n}\n#endif'
if asahi_text.count(begin) != 1 or asahi_text.count(end) != 1:
    raise SystemExit('Pinned asahi_drm Windows overlay anchors mismatch')
asahi_text = asahi_text.replace(begin, '#ifndef _WIN32\n' + begin)
asahi_text = asahi_text.replace(end, '#endif /* !_WIN32: no Linux ioctl enum */\n' + end)
asahi_drm.write_text(asahi_text)

units = ['agx_batch.c','agx_blit.c','agx_disk_cache.c','agx_fence.c','agx_pipe.c',
         'agx_nir_lower_sysvals.c','agx_nir_lower_bindings.c',
         'agx_nir_lower_point_size.c','agx_query.c','agx_state.c','agx_streamout.c',
         'agx_uniforms.c']
includes = [out/'include', out/'src', out/'src/asahi/lib', out/'src/asahi/compiler',
            out/'src/asahi/libagx', out/'src/gallium/drivers/asahi',
            args.project/'drivers/apple-agx/mesa/winsys',
            args.project/'drivers/apple-agx/shared/include', build/'src', build/'include',
            build/'src/compiler', build/'src/compiler/nir', mesa/'include', mesa/'src',
            mesa/'src/gallium/include', mesa/'src/gallium/auxiliary',
            mesa/'src/asahi/layout', generated/'src/asahi/compiler', generated/'src/asahi/libagx']
includes += [generated/'src/asahi/genxml']
inc = ['/I' + str(p) for p in includes]
records = []
for name in units:
    source = out/'src/gallium/drivers/asahi'/name
    obj = out/(source.stem+'.obj')
    result = subprocess.run([str(clang/'clang-cl.exe'), *flags, *inc, '/c', str(source),
                             '/Fo'+str(obj)], cwd=build, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (out/(source.stem+'.log')).write_bytes(result.stdout)
    records.append({'unit':name,'exit':result.returncode})
    (out/'result.json').write_text(json.dumps(records, indent=2))
    if result.returncode:
        print('FIRST_RUNTIME_FAILURE=' + name + ' EVIDENCE=' + str(out))
        raise SystemExit(result.returncode)
print('RUNTIME_OBJECT_CLOSURE_PASS EVIDENCE=' + str(out))
