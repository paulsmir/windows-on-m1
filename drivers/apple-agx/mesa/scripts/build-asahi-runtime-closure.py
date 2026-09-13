"""Extract the first real Windows Gallium/Asahi runtime closure blocker.

This is a source-preserving compiler probe: it neither installs a driver nor
submits work. Compiler-side Asahi objects are supplied by the already proven
AD04 fullcompiler artifact; runtime source remains current pinned Mesa.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--project', required=True, type=Path)
parser.add_argument('--architecture', choices=('x64', 'arm64'), default='x64')
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
root = Path(r'C:\Users\pauls\AD04-fullcompiler-001')
mesa = Path(r'C:\Users\pauls\AD04-d3d10-frontend-build\mesa')
build = root / ('nir-' + args.architecture)
clang = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin')
generated = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated')
out = args.output
out.mkdir(exist_ok=False)

GENERATED_HELPERS_SHA256 = 'b10e6ec36950ecd55878f4665f644969a93ba7bdbeb75f7c6584e8a1da11bc37'
AGX_LINKER_SHA256 = 'bd469422d642fdbd136c978446426446d85b80070378e16fece842287136c6ff'

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

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
flags.append('/DHAVE_FUNC_ATTRIBUTE_PACKED=1')
if args.architecture == 'arm64':
    flags.append('--target=aarch64-pc-windows-msvc')
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
batch_text = batch.read_text()
if batch_text.count('#include "vdrm.h"') != 1:
    raise SystemExit('Pinned agx_batch vdrm include anchor mismatch')
batch.write_text(batch_text.replace('#include "vdrm.h"',
    '#ifndef _WIN32\n#include "vdrm.h"\n#endif'))
device = out / 'src/asahi/lib/agx_device.h'
device_text = device.read_text()
if device_text.count('#include <xf86drm.h>') != 1 or device_text.count('#include "vdrm.h"\n\n#include "asahi_proto.h"') != 1:
    raise SystemExit('Pinned agx_device Windows overlay anchors mismatch')
device_text = device_text.replace('#include <xf86drm.h>',
    '#ifndef _WIN32\n#include <xf86drm.h>\n#else\n#include <stddef.h>\n#include "c11/threads.h"\ntypedef ptrdiff_t ssize_t;\n#endif')
device_text = device_text.replace('#include "vdrm.h"\n\n#include "asahi_proto.h"',
    '#ifndef _WIN32\n#include "vdrm.h"\n#include "asahi_proto.h"\n#else\nstruct vdrm_device;\nstruct asahi_ccmd_submit_res;\n#endif')
if device_text.count('   pthread_mutex_t bo_map_lock;') != 1:
    raise SystemExit('Pinned agx_device mutex anchor mismatch')
device_text = device_text.replace('   pthread_mutex_t bo_map_lock;',
    '#ifdef _WIN32\n   mtx_t bo_map_lock;\n#else\n   pthread_mutex_t bo_map_lock;\n#endif')
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
state_header = out / 'src/gallium/drivers/asahi/agx_state.h'
state_header_text = state_header.read_text()
if state_header_text.count('#include <xf86drm.h>') != 1:
    raise SystemExit('Pinned agx_state DRM include anchor mismatch')
state_header.write_text(state_header_text.replace('#include <xf86drm.h>',
    '#ifndef _WIN32\n#include <xf86drm.h>\n#endif'))
libagx = out / 'src/asahi/libagx/libagx_dgc.h'
libagx_text = libagx.read_text()
if libagx_text.count('   uint range_B = d.index_buffer_range_B;') != 1:
    raise SystemExit('Pinned libagx uint anchor mismatch')
libagx.write_text(libagx_text.replace('   uint range_B = d.index_buffer_range_B;',
    '   uint32_t range_B = d.index_buffer_range_B;'))
# Generated helper argument layouts are an ABI contract. Reuse the previously
# proven Windows projection rather than weakening its static assertions.
generated_helpers = Path(r'C:\Users\pauls\AD04-umd-owner-004-generated\src\asahi\lib\libagx_shaders.h')
if not generated_helpers.exists():
    raise SystemExit('Proven Windows generated helper header unavailable')
generated_helpers_sha256 = sha256(generated_helpers)
if generated_helpers_sha256 != GENERATED_HELPERS_SHA256:
    (out/'result.json').write_text(json.dumps({
        'architecture': args.architecture,
        'generated_helpers': {
            'path': str(generated_helpers),
            'expected_sha256': GENERATED_HELPERS_SHA256,
            'sha256': generated_helpers_sha256,
        },
        'exit': 'generated-helper-hash-mismatch',
    }, indent=2))
    raise SystemExit('Proven Windows generated helper header hash mismatch')
generated_helpers_output = out/'src/asahi/lib/libagx_shaders.h'
shutil.copy2(generated_helpers, generated_helpers_output)
helpers_text = generated_helpers_output.read_text()
helper_args = 'struct libagx_helper_args {\n} PACKED;'
helper_args_projection = ('enum { LIBAGX_HELPER_ARGS_BYTES = 0 };\n'
    'struct libagx_helper_args {\n   uint8_t host_placeholder;\n} PACKED;')
if helpers_text.count(helper_args) != 1 or \
        helpers_text.count('static_assert(sizeof(struct libagx_helper_args) == 0, "");') != 1:
    raise SystemExit('Pinned generated helper projection anchors mismatch')
helpers_text = helpers_text.replace(helper_args, helper_args_projection)
helpers_text = helpers_text.replace(
    'static_assert(sizeof(struct libagx_helper_args) == 0, "");',
    'static_assert(LIBAGX_HELPER_ARGS_BYTES == 0, "zero argument wire payload");')
helper_dispatch = 'MESA_DISPATCH_PRECOMP(_context, _grid, _barrier, LIBAGX_HELPER, &_args, sizeof(_args));'
if helpers_text.count(helper_dispatch) != 2:
    raise SystemExit('Pinned generated helper dispatch anchors mismatch')
helpers_text = helpers_text.replace(helper_dispatch,
    'MESA_DISPATCH_PRECOMP(_context, _grid, _barrier, LIBAGX_HELPER, &_args, LIBAGX_HELPER_ARGS_BYTES);')
generated_helpers_output.write_text(helpers_text)

linker = out/'src/asahi/lib/agx_linker.h'
if sha256(linker) != AGX_LINKER_SHA256:
    raise SystemExit('Pinned agx_linker header hash mismatch')
linker_text = linker.read_text()
linker_start = linker_text.index('struct agx_fs_epilog_link_info {')
linker_end = linker_text.index('\n};', linker_start) + 3
linker_original = linker_text[linker_start:linker_end]
linker_projection = linker_original.replace('struct agx_fs_epilog_link_info {',
    'struct __attribute__((aligned(4))) agx_fs_epilog_link_info {').replace(
        '   unsigned ', '   uint8_t ')
linker.write_text(linker_text[:linker_start] + linker_projection + linker_text[linker_end:])

units = ['agx_batch.c','agx_state.c','agx_pipe.c',
         'agx_nir_lower_sysvals.c','agx_nir_lower_bindings.c',
         'agx_nir_lower_point_size.c','agx_query.c','agx_uniforms.c']
includes = [out/'include', out/'src', out/'src/asahi/lib', out/'src/asahi/compiler', out/'src/asahi/layout',
            out/'src/asahi/libagx', out/'src/gallium/drivers/asahi',
            args.project/'drivers/apple-agx/mesa/winsys',
            args.project/'drivers/apple-agx/shared/include', build/'src', build/'include',
            build/'src/compiler', build/'src/compiler/nir', mesa/'include', mesa/'src',
            mesa/'src/gallium/include', mesa/'src/gallium/auxiliary', mesa/'src/compiler', mesa/'src/compiler/nir',
            generated/'src', generated/'src/asahi/compiler', generated/'src/asahi/lib', generated/'src/asahi/libagx']
includes += [generated/'src/asahi/genxml']
inc = ['/I' + str(p) for p in includes]
manifest = {
    'architecture': args.architecture,
    'project': str(args.project),
    'compiler': str(clang/'clang-cl.exe'),
    'base_compile_command': base['command'],
    'flags': flags,
    'includes': [str(p) for p in includes],
    'generated_helpers': {
        'path': str(generated_helpers),
        'expected_sha256': GENERATED_HELPERS_SHA256,
        'sha256': generated_helpers_sha256,
        'projection_sha256': sha256(generated_helpers_output),
    },
    'agx_linker': {
        'expected_sha256': AGX_LINKER_SHA256,
        'sha256': AGX_LINKER_SHA256,
        'projection_sha256': sha256(linker),
    },
    'compile_inputs': [],
}
records = []
def write_result(overall_exit=None):
    result = dict(manifest)
    result['units'] = records
    if overall_exit is not None:
        result['exit'] = overall_exit
    (out/'result.json').write_text(json.dumps(result, indent=2))

for name in units:
    source = out/'src/gallium/drivers/asahi'/name
    obj = out/(source.stem+'.obj')
    command = [str(clang/'clang-cl.exe'), *flags, *inc, '/c', str(source),
               '/Fo'+str(obj)]
    manifest['compile_inputs'].append({
        'unit': name,
        'source_sha256': sha256(source),
        'argv': command,
    })
    result = subprocess.run(command, cwd=build, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (out/(source.stem+'.log')).write_bytes(result.stdout)
    records.append({'unit':name,'exit':result.returncode})
    write_result(result.returncode if result.returncode else None)
    if result.returncode:
        print('FIRST_RUNTIME_FAILURE=' + name + ' EVIDENCE=' + str(out))
        raise SystemExit(result.returncode)
write_result(0)
print('RUNTIME_OBJECT_CLOSURE_PASS EVIDENCE=' + str(out))
