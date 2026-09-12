"""Fixed Windows compiler-only integration; consumes upstream Meson commands.

No driver installation, hardware access, model-provided commands or renderer.
Keep each attempt and stop at the first failed translation unit/link/run.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid

ROOT = Path(r'C:\Users\pauls\AD04-fullcompiler-001')
MESA = Path(r'C:\Users\pauls\AD04-d3d10-frontend-build\mesa')
BUILD = ROOT / 'nir-x64'
GENERATED = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated')
CLANG = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin')
INPUT = ROOT / 'asahi-input'
OUT = ROOT / ('asahi-' + uuid.uuid4().hex)
OUT.mkdir()
records = []

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(name, args):
    result = subprocess.run([str(a) for a in args], cwd=BUILD,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (name + '.log')).write_bytes(result.stdout)
    records.append(dict(name=name, arguments=[str(a) for a in args], exit=result.returncode))
    (OUT / 'result.json').write_text(json.dumps(records, indent=2))
    if result.returncode:
        print('FIRST_FAILURE=' + name + ' EVIDENCE=' + str(OUT), flush=True)
        print(result.stdout.decode(errors='replace')[-7000:])
        raise SystemExit(result.returncode)

def split_windows(command):
    argc = ctypes.c_int()
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    argv = parse(command, ctypes.byref(argc))
    if not argv:
        raise RuntimeError('Cannot parse upstream compiler command')
    try:
        return [argv[i] for i in range(argc.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(argv, ctypes.c_void_p))

commands = json.loads((BUILD / 'compile_commands.json').read_text())
nir = [c for c in commands if c['file'].replace('\\', '/').endswith('/nir.c')]
if len(nir) != 1:
    raise SystemExit('Full NIR required; stub/header-only build forbidden')
cpp = next(c for c in commands if c['file'].replace('\\', '/').endswith('/gtest-all.cc'))

def flags(entry):
    args = split_windows(entry['command'])[1:]
    return [a for a in args if not a.startswith(('-I', '/I', '/Fo', '/Fd'))
            and a not in ('/c', '/showIncludes', '/Zi', entry['file'])]

sources = re.search(r'libasahi_agx_files = files\((.*?)\n\)',
                    (MESA / 'src/asahi/compiler/meson.build').read_text(), re.S).group(1)
names = re.findall(r"'([^']+\.c)'", sources)
assert len(names) == 40, len(names)
compiler = OUT / 'src/asahi/compiler'
shutil.copytree(MESA / 'src/asahi/compiler', compiler)
null_device = json.loads((INPUT / 'agx-null-device.json').read_text())
compile_source = compiler / 'agx_compile.c'
if digest(compile_source) != null_device['source_sha256']:
    raise SystemExit('Pinned agx_compile.c hash mismatch')
text = compile_source.read_text()
if text.count(null_device['old']) != 1:
    raise SystemExit('Null-device replacement anchor mismatch')
compile_source.write_text(text.replace(null_device['old'], null_device['new']))
for name, sha in [('agx_compiler.h', 'c41df12679c1692cc50617e81d57eb03cf906d8c5fbada482b89fc6dde0c7741'),
                  ('agx_pack.c', 'f594448db4ccfe5d4f98f849c3f1e08b662264702f5de40a4c97d34be180cf00')]:
    if digest(compiler / name) != sha:
        raise SystemExit('Pinned source mismatch: ' + name)
    shutil.copy2(INPUT / name, compiler / name)
(OUT / 'src/util').mkdir()
shutil.copy2(INPUT / 'lut.h', OUT / 'src/util/lut.h')
includes = [Path(r'C:\Users\pauls\AD04-uatomic\include'), OUT / 'src', compiler, BUILD / 'src', BUILD / 'include',
            BUILD / 'src/compiler', BUILD / 'src/compiler/nir', MESA / 'include',
            MESA / 'src', MESA / 'src/compiler', MESA / 'src/compiler/nir',
            GENERATED / 'src/asahi/compiler', GENERATED / 'src/asahi/libagx',
            MESA / 'src/asahi/libagx', MESA / 'src/asahi/isa']
inc = ['/I' + str(p) for p in includes]
units = [(compiler / name, False) for name in names]
units += [(GENERATED / 'src/asahi/compiler' / name, False)
          for name in ('agx_opcodes.c', 'agx_nir_algebraic.c')]
units += [(INPUT / 'agx2_disasm.c', False), (INPUT / 'libagx.cpp', True),
          (INPUT / 'agx_shader_fixture.c', False)]
(OUT / 'inputs.json').write_text(json.dumps({str(p): digest(p) for p, _ in units}, indent=2))
objects = []
for src, is_cpp in units:
    obj = OUT / (src.stem + '.obj')
    run(src.stem, [CLANG / 'clang-cl.exe', *flags(cpp if is_cpp else nir[0]),
                   *inc, '/c', src, '/Fo' + str(obj)])
    objects.append(obj)
libraries = [BUILD / p for p in ('src/compiler/nir/libnir.a', 'src/compiler/libcompiler.a',
                                'src/util/libmesa_util.a', 'src/c11/impl/libmesa_util_c11.a',
                                'src/util/blake3/libblake3.a')]
exe = OUT / 'agx_shader_fixture.exe'
run('link', [CLANG / 'clang-cl.exe', '/nologo', '/MD', *objects, *libraries,
             '/Fe' + str(exe), '/link', 'synchronization.lib', 'advapi32.lib', 'user32.lib'])
for variant in (0, 1):
    target = OUT / ('variant' + str(variant))
    target.mkdir()
    run('execute-' + str(variant), [exe, target, str(variant)])
    if not (target / 'compute.bin').stat().st_size:
        raise SystemExit('Empty compiled shader')
if (OUT / 'variant0/compute.bin').read_bytes() == (OUT / 'variant1/compute.bin').read_bytes():
    raise SystemExit('Distinct NIR inputs unexpectedly compile to identical bytes')
(OUT / 'artifacts.json').write_text(json.dumps({str(p): digest(p) for p in
    [exe, *libraries, OUT/'variant0/compute.bin', OUT/'variant1/compute.bin']}, indent=2))
print('BUILD_LINK_EXECUTE_PASS EVIDENCE=' + str(OUT), flush=True)
