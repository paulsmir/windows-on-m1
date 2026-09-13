"""Build the real projected native runtime once, for the existing UMD executable.

The state builder owns all native platform/lifecycle projections. This builder
consumes that exact tree, rebuilds the proven nonfixture Asahi compiler inputs,
and archives runtime objects. Only the existing UmdContractTest link/execution
can establish executable closure; a successful archive alone does not do so.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(r'C:\Users\pauls\AD04-fullcompiler-001')
MESA = Path(r'C:\Users\pauls\AD04-d3d10-frontend-build\mesa')
CLANG = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin')
GENERATED = Path(r'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated')
OWNER_GENERATED = Path(r'C:\Users\pauls\AD04-umd-owner-004-generated')
COMPILER_EVIDENCE = {
    'x64': ROOT / 'asahi-x64-4aac999030764fef813f32b1e0dcc7fd',
    'arm64': ROOT / 'asahi-arm64-37c472b7142e4e52a35fccf1b41da45a',
}
LIBRARIES = ('src/compiler/nir/libnir.a', 'src/compiler/libcompiler.a',
             'src/util/libmesa_util.a', 'src/c11/impl/libmesa_util_c11.a',
             'src/util/blake3/libblake3.a')
# These Linux implementation units are replaced by the existing Windows native
# BO/construction owner; never archive two definitions of the same BO API.
OS_UNITS = {'agx_bo.c', 'agx_device.c', 'agx_device_virtio.c', 'agx_va.c'}
# Windows context/screen projection removes unsupported fd/server-sync entry
# points; its batch completion uses the existing composer ordered fence. The
# executable link must prove no Linux agx_fence API references remain.
OS_GALLIUM_UNITS = {'agx_fence.c'}
BRIDGES = ('agx_win32_asahi_bo.c', 'agx_win32_asahi_capture.c',
           'agx_win32_asahi_pipeline.c', 'agx_win32_asahi_batch.c',
           'agx_win32_asahi_runtime_test.c')
# Actual Gallium helpers required by native context/resource/state construction.
# Further additions must follow real compile/link diagnostics, not dummy symbols.
AUX_UNITS = ('util/u_framebuffer.c', 'util/u_helpers.c', 'util/u_prim.c',
             'util/u_prim_restart.c', 'util/u_upload_mgr.c', 'util/u_surface.c',
             'util/u_transfer.c', 'util/u_transfer_helper.c', 'util/u_draw.c',
             'util/u_blitter.c', 'util/u_threaded_context.c')


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def windows_argv(command):
    argc = ctypes.c_int()
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    values = parse(command, ctypes.byref(argc))
    if not values:
        raise RuntimeError('Cannot parse pinned compiler command')
    try:
        return [values[i] for i in range(argc.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(values, ctypes.c_void_p))


def compiler_flags(entry, architecture):
    flags = [a for a in windows_argv(entry['command'])[1:]
             if not a.startswith(('-I', '/I', '/Fo', '/Fd'))
             and a not in ('/c', '/showIncludes', '/Zi', entry['file'])]
    flags += ['/DHAVE_FUNC_ATTRIBUTE_PACKED=1', '/Gy']
    if architecture == 'arm64' and not any(a.startswith('--target=') for a in flags):
        flags.append('--target=aarch64-pc-windows-msvc')
    return flags


def meson_sources(path, variable):
    text = path.read_text()
    match = re.search(r'\b' + re.escape(variable) + r'\s*=\s*files\((.*?)\n\)', text, re.S)
    if not match:
        raise RuntimeError('Pinned Meson source graph missing: ' + str(path))
    return re.findall(r"'([^']+\.(?:c|cc|cpp))'", match.group(1))


def write_props(path, architecture, native, library, dependencies):
    namespace = 'http://schemas.microsoft.com/developer/msbuild/2003'
    ET.register_namespace('', namespace)
    tag = lambda name: '{' + namespace + '}' + name
    project = ET.Element(tag('Project'))
    group = ET.SubElement(project, tag('PropertyGroup'))
    for key, value in {'NativeRuntimeArchitecture': architecture,
                       'NativeRuntimeNativeSource': str(native),
                       'NativeRuntimeLibrary': str(library)}.items():
        ET.SubElement(group, tag(key)).text = value
    link = ET.SubElement(ET.SubElement(project, tag('ItemDefinitionGroup')), tag('Link'))
    ET.SubElement(link, tag('AdditionalDependencies')).text = ';'.join(
        [str(library), *map(str, dependencies), 'synchronization.lib',
         'advapi32.lib', 'user32.lib', '%(AdditionalDependencies)'])
    target = ET.SubElement(project, tag('Target'), {
        'Name': 'ValidateNativeRuntimeArchitecture', 'BeforeTargets': 'PrepareForBuild'})
    ET.SubElement(target, tag('Error'), {
        'Condition': "('$(Platform)' == 'ARM64' and '$(NativeRuntimeArchitecture)' != 'arm64') or "
                     "('$(Platform)' == 'x64' and '$(NativeRuntimeArchitecture)' != 'x64')",
        'Text': 'Native runtime library architecture does not match the executable.'})
    ET.indent(project)
    ET.ElementTree(project).write(path, encoding='utf-8', xml_declaration=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--project', required=True, type=Path)
    parser.add_argument('--architecture', choices=('x64', 'arm64'), default='x64')
    parser.add_argument('--native-source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    out = args.output
    out.mkdir(exist_ok=False)
    native = args.native_source
    build = ROOT / ('nir-' + args.architecture)
    evidence = COMPILER_EVIDENCE[args.architecture]
    manifest = {'architecture': args.architecture, 'project': str(args.project),
                'native_source': str(native), 'compiler_evidence': str(evidence),
                'scope': 'Real native runtime/compiler archive; executable link and execution required',
                'native_draw_executed': False, 'hardware': 'NOT_RUN',
                'inputs': [], 'units': [], 'libraries': [], 'exit': None}

    def save():
        (out / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n')

    def record(path, expected=None, kind='source'):
        actual = sha256(path)
        entry = {'path': str(path), 'sha256': actual, 'kind': kind}
        if expected is not None:
            entry['expected_sha256'] = expected
        manifest['inputs'].append(entry)
        if expected is not None and actual != expected:
            raise RuntimeError('Pinned input hash mismatch: ' + str(path))
        return actual

    def run(name, command):
        entry = {'name': name, 'argv': list(map(str, command))}
        manifest['units'].append(entry)
        save()
        result = subprocess.run(entry['argv'], cwd=build, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (name + '.log')).write_bytes(result.stdout)
        entry['exit'] = result.returncode
        save()
        if result.returncode:
            manifest['first_failure'] = name
            print('FIRST_RUNTIME_FAILURE=' + name + ' EVIDENCE=' + str(out), flush=True)
            print(result.stdout.decode(errors='replace')[-5000:])
            raise RuntimeError('Native compilation/archive failed: ' + name)

    try:
        # Require the one prepared source tree rather than quietly reconstructing
        # a different set of platform headers or ignoring a failed projection.
        native_result = json.loads((native / 'result.json').read_text())
        if native_result.get('architecture') != args.architecture or native_result.get('exit') != 0:
            raise RuntimeError('Prepared native source architecture/result mismatch')
        record(native / 'result.json', kind='native-projection-result')
        record(native / 'inputs.json', kind='native-projection-manifest')
        native_inputs = json.loads((native / 'inputs.json').read_text())
        overlays = native_inputs.get('overlays', {})
        if not overlays:
            raise RuntimeError('Prepared native source has no projection identity')
        for relative, entry in overlays.items():
            path = native / relative
            expected = (entry.get('final_sha256') or entry.get('after_state_capture') or entry.get('after') or
                        entry.get('sha256'))
            if expected and path.is_file():
                record(path, expected, 'native-projection')
        # Record the prepared tree's complete source/header inputs,
        # including generated graph includes not present in original Mesa.
        for subdir in ('src/asahi', 'src/gallium/drivers/asahi', 'include/drm-uapi'):
            for path in sorted((native / subdir).rglob('*')):
                if path.is_file():
                    record(path, kind='native-tree')
        saved_inputs = json.loads((evidence / 'inputs.json').read_text())
        saved_artifacts = json.loads((evidence / 'artifacts.json').read_text())
        saved_results = json.loads((evidence / 'result.json').read_text())
        if not saved_results or any(r.get('exit') != 0 for r in saved_results):
            raise RuntimeError('Compiler evidence is not a passing pinned result')
        for name in ('inputs.json', 'artifacts.json', 'result.json'):
            record(evidence / name, kind='compiler-evidence')
        compiler_names = meson_sources(MESA / 'src/asahi/compiler/meson.build', 'libasahi_agx_files')
        if len(compiler_names) != 40:
            raise RuntimeError('Pinned compiler graph is not the proven 40 units')
        expected_names = set(compiler_names) | {'agx_opcodes.c', 'agx_nir_algebraic.c',
                                               'agx2_disasm.c', 'libagx.cpp'}
        compiler_units = []
        for name, expected in saved_inputs.items():
            source = Path(name)
            if source.name == 'agx_shader_fixture.c':
                continue
            if source.name not in expected_names:
                raise RuntimeError('Unexpected compiler evidence unit: ' + name)
            record(source, expected, 'proven-compiler-source')
            compiler_units.append(source)
        if len(compiler_units) != 44 or {p.name for p in compiler_units} != expected_names:
            raise RuntimeError('Nonfixture compiler closure is incomplete')
        libraries = []
        for relative in LIBRARIES:
            library = build / relative
            expected = saved_artifacts.get(str(library))
            if not expected:
                raise RuntimeError('No pinned library hash: ' + str(library))
            actual = record(library, expected, 'compiler-library')
            manifest['libraries'].append({'path': str(library), 'sha256': actual})
            libraries.append(library)

        command_file = build / 'compile_commands.json'
        record(command_file, kind='pinned-build-commands')
        commands = json.loads(command_file.read_text())
        c_entry = next(c for c in commands if c['file'].replace('\\', '/').endswith('/nir.c'))
        cpp_entry = next(c for c in commands if c['file'].replace('\\', '/').endswith('/gtest-all.cc'))
        c_flags = compiler_flags(c_entry, args.architecture)
        cpp_flags = compiler_flags(cpp_entry, args.architecture)
        toolchain = Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207')
        sdk = Path(r'C:\Program Files (x86)\Windows Kits\10')
        env = os.environ.copy()
        env['INCLUDE'] = ';'.join(map(str, [toolchain / 'include',
            sdk / 'Include/10.0.26100.0/ucrt', sdk / 'Include/10.0.26100.0/um',
            sdk / 'Include/10.0.26100.0/shared']))
        env['LIB'] = ';'.join(map(str, [toolchain / ('lib/' + args.architecture),
            sdk / ('Lib/10.0.26100.0/ucrt/' + args.architecture),
            sdk / ('Lib/10.0.26100.0/um/' + args.architecture)]))
        env['PATH'] = str(toolchain / ('bin/Hostx64/' + args.architecture)) + os.pathsep + env['PATH']
        geometry = MESA / 'src/poly/geometry.h'
        record(geometry, kind='native-primary-type-source')
        geometry_output = native / 'src/poly/geometry.h'
        geometry_output.parent.mkdir(parents=True, exist_ok=True)
        geometry_text = geometry.read_text()
        # Native CPU helper code uses the Linux uint alias (unsigned 32-bit).
        # Keep every shared data field/static assertion and project that alias
        # explicitly for Windows; generated GPU helper binaries are unchanged.
        geometry_output.write_text(geometry_text.replace('#pragma once',
            '#pragma once\n#ifdef _WIN32\ntypedef uint32_t uint;\n#endif', 1))
        record(geometry_output, kind='native-windows-type-projection')
        includes = [native / 'include', native / 'src', native / 'src/asahi', native / 'src/asahi/lib',
                    native / 'src/asahi/layout', native / 'src/asahi/compiler',
                    native / 'src/asahi/libagx', native / 'src/gallium/drivers/asahi',
                    args.project / 'drivers/apple-agx/mesa/winsys',
                    args.project / 'drivers/apple-agx/shared/include',
                    Path(r'C:\Users\pauls\AD04-uatomic\include'),
                    evidence / 'src', evidence / 'src/asahi/compiler',
                    build / 'src', build / 'include', build / 'src/compiler',
                    build / 'src/compiler/nir', MESA / 'include', MESA / 'src',
                    MESA / 'src/gallium/include', MESA / 'src/gallium/auxiliary',
                    MESA / 'src/compiler', MESA / 'src/compiler/nir',
                    GENERATED / 'src/asahi/compiler', GENERATED / 'src/asahi/libagx',
                    OWNER_GENERATED / 'src', OWNER_GENERATED / 'src/asahi/genxml',
                    OWNER_GENERATED / 'src/asahi/lib', MESA / 'src/asahi/isa',
                    MESA / 'src/poly', MESA / 'src/poly/nir']
        manifest['compiler'] = {'path': str(CLANG / 'clang-cl.exe'),
                                'sha256': record(CLANG / 'clang-cl.exe', kind='toolchain'),
                                'c_flags': c_flags, 'cpp_flags': cpp_flags,
                                'includes': list(map(str, includes))}
        runtime = []
        for relative, variable in [('src/gallium/drivers/asahi', 'files_asahi'),
                                   ('src/asahi/lib', 'libasahi_lib_files'),
                                   ('src/asahi/layout', 'libasahi_layout_files')]:
            meson = MESA / relative / 'meson.build'
            record(meson, kind='native-meson-graph')
            for name in meson_sources(meson, variable):
                if ((relative == 'src/asahi/lib' and name in OS_UNITS) or
                    (relative == 'src/gallium/drivers/asahi' and name in OS_GALLIUM_UNITS)):
                    continue
                runtime.append(native / relative / name)
        runtime.append(native / 'src/asahi/lib/agx_win32_device_key.c')
        runtime += [args.project / 'drivers/apple-agx/mesa/winsys' / name for name in BRIDGES]
        generated_c = OWNER_GENERATED / 'src/asahi/lib/libagx_shaders.c'
        record(generated_c, kind='native-generated-source')
        # Quoted includes must resolve beside the accepted projected header,
        # not beside the unprojected generated Windows input.
        projected_c = native / 'src/asahi/lib/libagx_shaders.c'
        shutil.copy2(generated_c, projected_c)
        runtime.append(projected_c)
        runtime += [MESA / 'src/gallium/auxiliary' / name for name in AUX_UNITS]
        # Actual executable link, run native-lifecycle-20260913g, names these
        # normal Gallium/TGSI utility definitions. Keep their primary sources.
        for relative in ('util/u_screen.c','util/u_sample_positions.c','util/u_sampler.c',
                         'util/u_dump_state.c','util/u_dump_defines.c','util/u_texture.c',
                         'util/u_simple_shaders.c','nir/tgsi_to_nir.c'):
            runtime.append(MESA / 'src/gallium/auxiliary' / relative)
        for relative in ('tgsi/tgsi_build.c','tgsi/tgsi_parse.c','tgsi/tgsi_scan.c',
                         'tgsi/tgsi_info.c','tgsi/tgsi_util.c','tgsi/tgsi_ureg.c',
                         'tgsi/tgsi_dump.c','tgsi/tgsi_text.c','tgsi/tgsi_strings.c',
                         'tgsi/tgsi_iterate.c','tgsi/tgsi_sanity.c','util/u_bitmask.c','cso_cache/cso_hash.c','cso_cache/cso_cache.c'):
            runtime.append(MESA / 'src/gallium/auxiliary' / relative)
        runtime.append(OWNER_GENERATED / 'src/poly/cl/libpoly.cpp')
        # The real prior agx_state link names poly_nir_lower_{gs,tcs,tes,vs}
        # and poly_tcs_* from these actual primary implementation units.
        poly_meson = MESA / 'src/poly/nir/meson.build'
        record(poly_meson, kind='native-meson-graph')
        runtime += [MESA / 'src/poly/nir' / name
                    for name in meson_sources(poly_meson, 'libpoly_nir_files')]
        manifest['excluded_os_units'] = sorted(OS_UNITS | OS_GALLIUM_UNITS)
        manifest['excluded_fixture_units'] = ['agx_shader_fixture.c', 'native_pipeline_contract.c',
            'native_batch_encoder_contract.c', 'agx_win32_asahi_pool_test.c',
            'agx_win32_asahi_pipeline_test.c', 'agx_win32_reloc_capture_test.c']
        units = [('native', source) for source in runtime] + [('compiler', source) for source in compiler_units]
        objects = []
        object_dir = out / 'objects'
        object_dir.mkdir()
        for number, (group, source) in enumerate(units):
            source_hash = record(source)
            name = '%03d-%s-%s' % (number, group, source.stem)
            obj = object_dir / (name + '.obj')
            flags = cpp_flags if source.suffix in ('.cpp', '.cc') else c_flags
            # Actual-body test exports live in agx_state; no duplicate fixture
            # definition is included in this archive or its dependency list.
            exports = ['/DAGX_WIN32_NATIVE_PIPELINE_TEST=1'] if source.name == 'agx_state.c' else []
            run(name, [CLANG / 'clang-cl.exe', *flags, *exports,
                       *('/I' + str(p) for p in includes), '/c', source, '/Fo' + str(obj)])
            manifest['units'][-1].update(source_sha256=source_hash, object=str(obj), object_sha256=sha256(obj))
            objects.append(obj)
        library = out / 'native_runtime.lib'
        response = out / 'archive.rsp'
        response.write_text('\n'.join(subprocess.list2cmdline([str(p)]) for p in objects) + '\n')
        run('archive', [CLANG / 'llvm-lib.exe', '/nologo', '/out:' + str(library), '@' + str(response)])
        manifest['library'] = {'path': str(library), 'sha256': sha256(library)}
        write_props(out / 'NativeRuntime.props', args.architecture, native, library, libraries)
        manifest['props'] = {'path': str(out / 'NativeRuntime.props'), 'sha256': sha256(out / 'NativeRuntime.props')}
        manifest['executable_link'] = 'NOT_RUN'
        manifest['exit'] = 0
        save()
        print('NATIVE_RUNTIME_ARCHIVE_PASS EXECUTABLE_LINK=NOT_RUN EVIDENCE=' + str(out), flush=True)
    except Exception as error:
        manifest['exit'] = 1
        manifest['error'] = str(error)
        save()
        print('RUNTIME_FAILURE=' + str(error) + ' EVIDENCE=' + str(out), flush=True)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
