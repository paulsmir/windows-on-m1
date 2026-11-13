#!/usr/bin/env python3
"""RC74: rank D3D12 compute shaders by AGX cost.

Usage:
  agx_shader_cost.py log UMD.log [--top 30]
      Runtime ranking from receipts (APPLE_AGX diagnostics on): the compiled
      statistics of each shader (measure-d3d12-shader-stats, one per compiled
      part) joined with its dispatches (measure-d3d12-dispatch, 6th value =
      the CS hash's low 32 bits). Direct dispatches contribute groups and
      threads (groups x workgroup size); indirect ones (grid 0) only a count,
      their grids live in GPU memory. Score = threads x instructions (the
      instruction-issue work of the shader's direct dispatches).
  agx_shader_cost.py compile PATH... [--no-d3d-robust] [--assume-in-bounds]
                    [--root-views UAV,CBV]
      Offline cost of captured shaders (APPLE_AGX_D3D12_SHADER_DUMP_DIR with
      APPLE_AGX_D3D12_SHADER_DUMP_ALL=1: <hash>-s5-r0.bin files, or any DXIL
      program/container): tests/dxil_agx_cost_fixture.c built against the
      host Mesa (the pinned 9aa1215f compiler) and run on each compute shader.
      --compare prints the D3D-robust, the robust in-bounds path and the
      unchecked cost side by side. --root-views takes the hex masks of the
      UAV and constant-buffer slots that are D3D12 root descriptors (RC75:
      unchecked; measure-d3d12-root-views receipts name them per shader).
      RC75 op classes of the final NIR: nir-alu, nir-mem (device memory),
      nir-tex (texture and image), nir-shared, barriers, atomics.
      RC78: preamble (instructions run once per dispatch), ic (integer and
      complex unit work of the shader) and crawl-speculated (texture
      descriptor loads allowed into the preamble; set
      APPLE_AGX_D3D_CRAWL_SPECULATE=0 in the environment for Asahi's
      placement).
      RC82: --soft-fault asahi compiles with Asahi's soft-fault compiler key
      (every access-qualified intrinsic and texture speculatable);
      --soft-fault loads with the Windows projection (memory loads only).
      --production (with --root-views, optionally --soft-fault loads): the
      fixture's production shape, the real Gallium binding / sysval / uniform
      layout passes instead of the stand-ins. Every row carries "shape":
      only "production" rows are comparable with guest receipts.
  agx_shader_cost.py verify UMD.log --captures DIR [HASH...]
      Is the offline compile the guest's compile? For every compute shader
      of the log (or the named 16-digit hashes or low-32-bit tags) with a
      capture DIR/<hash>-s5-r0.bin: compile it in the production shape with
      the masks of its measure-d3d12-root-views receipt and the device's
      measure-soft-faults feature, and compare with its
      measure-d3d12-shader-stats row. Prints EQUAL or the differing fields
      (offline-guest) per shader and the totals; exit 0 only when at least
      one shader was compared and none differs.
JSON or a table on stdout; nothing is written elsewhere except the fixture
build directory (--build-dir, default a temporary one).
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / 'tests'

# measure-d3d12-shader-stats value slots (agx_win32_shader_stats.h)
STATS = ['hash_hi', 'hash_lo', 'stage', 'bytes', 'instrs', 'alu', 'gprs', 'threads', 'scratch', 'spills', 'fills',
         'loops', 'code', 'preamble', 'block', 'shared']
COMPUTE = 5
INTERNAL = 0x100


def receipts(lines, names):
    """(name, pid, values) of the receipts named in names."""
    for line in lines:
        name = line.split(' ', 1)[0]
        if name not in names:
            continue
        fields = line.split()
        meta = dict(f.split('=', 1) for f in fields[1:] if '=' in f)
        try:
            values = [int(x, 16) for x in fields[1:] if '=' not in x]
            yield name, int(meta.get('pid', 0)), values
        except ValueError:
            continue


def block_threads(block):
    x, y, z = block & 0x7ff, (block >> 11) & 0x7ff, (block >> 22) & 0x3ff
    return x * y * z, (x, y, z)


def rank_log(lines):
    """Per compute shader (hash_lo): statistics, dispatches, groups, score."""
    stats, dispatch = {}, {}
    pids = {}
    rows = list(receipts(lines, ('measure-d3d12-shader-stats', 'measure-d3d12-dispatch')))
    for name, pid, _ in rows:
        if name == 'measure-d3d12-dispatch':
            pids[pid] = pids.get(pid, 0) + 1
    pid = max(pids, key=pids.get) if pids else None
    for name, p, v in rows:
        if p != pid:
            continue
        if name == 'measure-d3d12-shader-stats' and len(v) >= len(STATS):
            s = dict(zip(STATS, v))
            if s['stage'] != COMPUTE:
                continue
            old = stats.get(s['hash_lo'])
            if not old or s['instrs'] > old['instrs']:
                stats[s['hash_lo']] = s
        elif name == 'measure-d3d12-dispatch' and len(v) >= 6 and v[0] == 0:
            d = dispatch.setdefault(v[5], dict(dispatches=0, direct=0, indirect=0, groups=0))
            d['dispatches'] += 1
            groups = v[1] * v[2] * v[3]
            if groups:
                d['direct'] += 1
                d['groups'] += groups
            else:
                d['indirect'] += 1
    table = []
    for tag, d in dispatch.items():
        s = stats.get(tag)
        threads_per_group, block = block_threads(s['block']) if s else (0, (0, 0, 0))
        row = dict(tag=f'{tag:08x}', hash=f"{s['hash_hi']:08x}{s['hash_lo']:08x}" if s else None, **d,
                   threads=d['groups'] * threads_per_group, block=block)
        for key in ('instrs', 'alu', 'gprs', 'threads', 'scratch', 'spills', 'fills', 'shared', 'bytes'):
            row['occupancy' if key == 'threads' else key] = s[key] if s else None
        row['score'] = row['threads'] * (s['instrs'] if s else 0)
        table.append(row)
    table.sort(key=lambda r: (r['score'], r['dispatches']), reverse=True)
    return dict(pid=pid, shaders=len(stats), tagged_dispatch_shaders=len(dispatch), ranking=table)


def projected_compiler(build_tree, mesa, out, flags):
    """RC96: the Windows build projects agx_compiler.h, agx_pressure_schedule.c
    and agx_compile.c through mesa/scripts/native-asahi-latency-schedule.py.
    Compile the projected sources (next to the projected header, which their
    quoted includes find first) with the fixture's flags; linked ahead of
    libasahi_compiler.a, their archive members are not loaded, so offline
    costs are the Windows compiler's."""
    import importlib.util
    script = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-latency-schedule.py'
    spec = importlib.util.spec_from_file_location('native_latency_schedule', script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    objects = []
    projected = out / 'rc96-compiler'
    projected.mkdir(exist_ok=True)
    for path, project in module.FILES.items():
        source = projected / Path(path).name
        source.write_text(project((mesa / path).read_text()))
        if source.suffix != '.c':
            continue
        obj = source.with_suffix('.o')
        # the projected agx_compiler.h must win every quoted include, also
        # those of the generated agx_builder.h: its directory goes first
        r = subprocess.run(['clang', '-I', str(projected), *flags, '-I', str(build_tree / 'src/asahi/compiler'),
                            '-I', str(mesa / 'src/asahi/compiler'), '-c', str(source), '-o', str(obj)],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f'projected {path}: ' + r.stderr[-3000:])
        objects.append(str(obj))
    return objects


def production_objects(build_tree, mesa, out):
    """dxil_agx_cost_fixture --production: the Gallium Asahi sources the
    Windows build compiles for these steps. agx_nir_lower_bindings.c is the
    pinned file; agx_nir_lower_sysvals.c (agx_nir_layout_uniforms lives
    there too) is projected exactly as build-native-asahi-state.py projects
    it (D3D12 C47: the pin and the replacements are read from that script,
    so a changed projection changes this compile or stops it)."""
    import hashlib
    from test_dxbc_to_nir import host_flags
    from test_agx_heap_handle_lowering import sysvals_change
    path, sha, replacements = sysvals_change()
    raw = (mesa / path).read_bytes()
    if hashlib.sha256(raw).hexdigest() != sha:
        raise RuntimeError(f'{path}: not the source build-native-asahi-state.py pins')
    text = raw.decode()
    for old, new in replacements:
        if text.count(old) != 1:
            raise RuntimeError(f'{path}: projection anchor matches {text.count(old)} times')
        text = text.replace(old, new)
    gallium = out / 'production-gallium'
    (gallium / 'stub').mkdir(parents=True, exist_ok=True)
    (gallium / 'stub/xf86drm.h').write_text('')  # libdrm: not on the host, nothing of it is used
    sysvals = gallium / 'agx_nir_lower_sysvals.c'
    sysvals.write_text(text)
    dirs = [gallium / 'stub'] + [mesa / d for d in (
        'src/gallium/drivers/asahi', 'src/gallium/include', 'src/gallium/auxiliary', 'src/asahi', 'src/asahi/lib',
        'src/asahi/libagx', 'src/asahi/layout', 'src/asahi/compiler', 'src', 'include', 'include/drm-uapi',
        'src/virtio/vdrm')] + [build_tree / d for d in (
            'src/asahi', 'src/asahi/lib', 'src/asahi/genxml', 'src/asahi/libagx', 'src/asahi/compiler',
            'src/gallium/drivers/asahi')]
    objects = []
    for source in (TESTS / 'dxil_agx_cost_gallium.c', mesa / 'src/gallium/drivers/asahi/agx_nir_lower_bindings.c',
                   sysvals):
        obj = gallium / (source.stem + '.o')
        r = subprocess.run(['clang', '-std=c11', '-Wno-c23-extensions', *host_flags(build_tree),
                            *['-I' + str(d) for d in dirs], '-c', str(source), '-o', str(obj)],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f'{source.name}: ' + r.stderr[-3000:])
        objects.append(str(obj))
    return objects


def build(out_dir, pinned=False):
    """Build tests/dxil_agx_cost_fixture.c against the host Mesa and
    dxil-spirv trees (the test_dxil_to_nir layout); None without them.
    RC96: with the Windows compiler projection (projected_compiler) unless
    pinned (the host build's unmodified libasahi_compiler.a)."""
    sys.path.insert(0, str(TESTS))
    from test_dxbc_to_nir import local_tree, host_flags, DXBC, HOST_LIBS, CXX_RUNTIME, BREW_LINK
    from test_dxil_to_nir import MESA_LIBS, DXIL
    build_tree = local_tree('accelerated-desktop-ad03', 'mesa-build')
    mesa = local_tree('reference', 'mesa')
    spirv = local_tree('reference', 'dxil-spirv')
    archives = local_tree('dxil-work', 'build-dxil-spirv')
    if not build_tree or not mesa or not spirv or not archives or not (build_tree / MESA_LIBS[3]).exists():
        return None
    out = Path(out_dir)
    winsys = ROOT / 'drivers/apple-agx/mesa/winsys'
    subprocess.run(['clang++', '-std=c++17', '-c', '-I', str(spirv), str(DXIL / 'agx_dxil_checked.cpp'),
                    '-o', str(out / 'checked.o')], check=True, capture_output=True)
    common = ['-std=c11', '-Werror=implicit-function-declaration', *host_flags(build_tree),
              '-I', str(build_tree / 'src/asahi'), '-I', str(mesa / 'src/asahi'), '-I', str(mesa / 'src'),
              '-I', str(build_tree / 'src'), '-I', str(DXBC), '-I', str(DXIL), '-I', str(spirv), '-I', str(winsys)]
    libs = [str(build_tree / p) for p in MESA_LIBS]
    dlibs = [str(p) for p in sorted(archives.glob('*.a'))]
    link = ['-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread', *HOST_LIBS]
    binary = out / 'dxil_agx_cost_fixture'
    compiler = [] if pinned else projected_compiler(build_tree, mesa, out, common)
    compiler += production_objects(build_tree, mesa, out)
    subprocess.run(['clang', *common, str(TESTS / 'dxil_agx_cost_fixture.c'), *compiler, str(DXIL / 'agx_dxil_to_nir.c'),
                    str(DXIL / 'agx_dxil_program.c'), str(DXBC / 'dxbc_to_nir.c'), str(DXBC / 'dxbc_parse.c'),
                    str(DXBC / 'dxbc_minmax.c'), str(out / 'checked.o'), '-o', str(binary), *libs, *dlibs, *dlibs,
                    *link, CXX_RUNTIME, *BREW_LINK, '-lSPIRV-Tools-shared'], check=True, capture_output=True)
    return binary


def cost(binary, source, *flags, env=None):
    """The fixture's COST fields for one input (a path,
    --synthetic-uav-reads=K or --synthetic-local-array=N), {} when refused;
    env: extra environment (APPLE_AGX_D3D_BUFFER_BRANCHES=1 or
    APPLE_AGX_D3D_SCRATCH_ARRAYS=0 compile the earlier forms)."""
    r = subprocess.run([str(binary), str(source), *flags], capture_output=True, text=True, timeout=300,
                       env={**os.environ, **(env or {})})
    if r.returncode:
        raise RuntimeError(f'{source}: fixture exited {r.returncode}: {r.stderr[-2000:]}')
    line = next((l for l in r.stdout.splitlines() if l.startswith('COST ')), None)
    if not line:
        return {}
    fields = dict(re.findall(r'([a-z-]+) (\S+)', line[5:]))
    return {k: (int(v) if v.isdigit() else v) for k, v in fields.items()}


# the measure-d3d12-shader-stats values the backend reports (STATS slots 4..13)
GUEST_FIELDS = STATS[4:14]


def guest_compiles(lines):
    """What a guest UMD log says about its compute shader compiles:
    ({hash: {'stats': rows, 'views': masks}}, soft-fault features).
    measure-d3d12-shader-stats: STATS. measure-d3d12-root-views
    (agx_d3d12_windows.cpp): stage, the CS hash's low 32 bits, root CBV
    slots, root UAV slots, CBV slots, UAV slots, applied (1: marked, 2: kept
    checked by APPLE_AGX_D3D12_ROOT_BOUNDS=1, 0: nothing to mark).
    measure-soft-faults (agx_d3d12_ddi.cpp): info valid, version, features
    (bit 0: DRM_ASAHI_FEATURE_SOFT_FAULTS, the Windows projection's load
    speculation = the fixture's --soft-fault-loads)."""
    shaders, views, features = {}, {}, set()
    for name, _, v in receipts(lines, ('measure-d3d12-shader-stats', 'measure-d3d12-root-views',
                                       'measure-soft-faults')):
        if name == 'measure-d3d12-shader-stats' and len(v) >= len(STATS) and v[2] == COMPUTE:
            row = tuple(v[4:14])
            rows = shaders.setdefault(f'{v[0]:08x}{v[1]:08x}', [])
            if row not in rows:
                rows.append(row)
        elif name == 'measure-d3d12-root-views' and len(v) >= 7 and v[0] == COMPUTE:
            # kept checked: the guest compiled without marks
            masks = (0, 0) if v[6] == 2 else (v[3], v[2])
            if masks not in views.setdefault(v[1], []):
                views[v[1]].append(masks)
        elif name == 'measure-soft-faults' and len(v) >= 3:
            features.add(v[2] if v[0] else 0)
    return ({h: dict(stats=rows, views=views.get(int(h[8:], 16), [])) for h, rows in shaders.items()}, features)


def verify(binary, lines, captures, wanted=(), out=sys.stdout):
    """Compare the production-shape offline compile with the guest's rows;
    (equal, different, not compared)."""
    shaders, features = guest_compiles(lines)
    if len(features) != 1:
        raise RuntimeError('the log needs exactly one measure-soft-faults feature set, found '
                           f'{sorted(features)}: the device feature decides the compile')
    soft = ('--soft-fault-loads',) if features.pop() & 1 else ()
    wanted = [w.lower() for w in wanted]
    missing = [w for w in wanted if not any(h == w or h[8:] == w for h in shaders)]
    if missing:
        raise RuntimeError(f'no compute measure-d3d12-shader-stats row for {missing}')
    equal = different = skipped = 0
    for h, guest in sorted(shaders.items()):
        if wanted and h not in wanted and h[8:] not in wanted:
            continue
        blob = Path(captures) / f'{h}-s5-r0.bin'
        why = (None if blob.exists() else 'no capture') if guest['views'] else 'no measure-d3d12-root-views receipt'
        if why:
            skipped += 1
            print(h, f'NOT COMPARED ({why})', file=out)
            continue
        for uav, cbv in guest['views']:
            c = cost(binary, blob, '--production', f'--root-views={uav:x},{cbv:x}', *soft)
            if c.get('shape') != 'production':
                raise RuntimeError(f'{h}: the fixture did not compile the production shape: {c or "refused"}')
            offline = tuple(c[k] for k in GUEST_FIELDS)
            # one receipt per compile: several rows only when the shader was
            # compiled in several shapes; report against the nearest one
            row = min(guest['stats'], key=lambda g: sum(a != b for a, b in zip(offline, g)))
            diff = ' '.join(f'{k}{a - b:+d}' for k, a, b in zip(GUEST_FIELDS, offline, row) if a != b)
            equal += not diff
            different += bool(diff)
            print(h, f'root-views={uav:x},{cbv:x}', ' '.join(soft), 'EQUAL' if not diff else 'DIFF offline-guest: ' + diff,
                  '| guest', '/'.join(map(str, row)), file=out)
    print(f'{"/".join(GUEST_FIELDS)}: equal {equal} different {different} not-compared {skipped}', file=out)
    return equal, different, skipped


def inputs(paths):
    for p in map(Path, paths):
        if p.is_dir():
            yield from sorted(q for q in p.iterdir() if re.search(r'-s5-r\d+\.bin$', q.name) or q.suffix == '.dxil')
        else:
            yield p


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='mode', required=True)
    log = sub.add_parser('log')
    log.add_argument('umd_log')
    log.add_argument('--top', type=int, default=30)
    comp = sub.add_parser('compile')
    comp.add_argument('paths', nargs='+')
    comp.add_argument('--compare', action='store_true')
    comp.add_argument('--no-d3d-robust', action='store_true')
    comp.add_argument('--assume-in-bounds', action='store_true')
    comp.add_argument('--root-views', help='UAV,CBV hex slot masks of root descriptors')
    comp.add_argument('--soft-fault', choices=('asahi', 'loads'),
                      help='RC82: compile as with GPU soft faults (asahi: Mesa set_speculate; '
                           'loads: the Windows projection, memory loads only)')
    comp.add_argument('--production', action='store_true',
                      help='the production shape (real Gallium passes); needs --root-views')
    comp.add_argument('--srv-ssbo', action='store_true',
                      help='SRV-1: DXBC raw/structured SRVs as shader buffers (APPLE_AGX_D3D12_SRV_SSBO)')
    comp.add_argument('--build-dir')
    ver = sub.add_parser('verify')
    ver.add_argument('umd_log')
    ver.add_argument('hashes', nargs='*', help='16-digit shader hashes or their low 32 bits (default: all)')
    ver.add_argument('--captures', required=True, help='directory of <hash>-s5-r0.bin captures')
    ver.add_argument('--build-dir')
    args = parser.parse_args(argv)
    if args.mode == 'compile' and args.production and (
            args.compare or args.no_d3d_robust or args.assume_in_bounds or args.soft_fault == 'asahi'
            or not args.root_views):
        parser.error('--production needs --root-views and excludes --compare, --no-d3d-robust, '
                     '--assume-in-bounds and --soft-fault asahi')
    if args.mode == 'log':
        with open(args.umd_log, errors='replace') as f:
            result = rank_log(f)
        result['ranking'] = result['ranking'][:args.top]
        json.dump(result, sys.stdout, indent=1)
        print()
        return 0
    with tempfile.TemporaryDirectory(prefix='agx-shader-cost-') as td:
        binary = build(args.build_dir or td)
        if not binary:
            print('host Mesa / dxil-spirv trees not available', file=sys.stderr)
            return 2
        if args.mode == 'verify':
            try:
                with open(args.umd_log, errors='replace') as f:
                    equal, different, _ = verify(binary, f, args.captures, args.hashes)
            except RuntimeError as e:
                print(f'verify: {e}', file=sys.stderr)
                return 2
            return 0 if equal and not different else 1
        variants = ([('robust', ()), ('in-bounds', ('--assume-in-bounds',)), ('unchecked', ('--no-d3d-robust',))]
                    if args.compare else
                    [('cost', tuple(f for f, on in (('--no-d3d-robust', args.no_d3d_robust),
                                                    ('--assume-in-bounds', args.assume_in_bounds)) if on))])
        if args.root_views:
            variants = [(name, flags + (f'--root-views={args.root_views}',)) for name, flags in variants]
        if args.soft_fault:
            sf = '--soft-fault' if args.soft_fault == 'asahi' else '--soft-fault-loads'
            variants = [(name, flags + (sf,)) for name, flags in variants]
        if args.production:
            variants = [(name, flags + ('--production',)) for name, flags in variants]
        if args.srv_ssbo:
            variants = [(name, flags + ('--srv-ssbo',)) for name, flags in variants]
        rows = []
        for path in inputs(args.paths):
            row = {'shader': path.name}
            for name, flags in variants:
                c = cost(binary, path, *flags)
                if not c:
                    break
                row[name] = {k: c[k] for k in ('instrs', 'alu', 'gprs', 'threads', 'scratch', 'spills',
                                               'hot-mem-blocks', 'hot-branches', 'block', 'shared', 'loops',
                                               'nir-alu', 'nir-mem', 'nir-tex', 'nir-shared', 'barriers',
                                               'atomics', 'root-marked', 'preamble', 'ic',
                                               'crawl-speculated', 'tex-speculated', 'shape') if k in c}
            if len(row) > 1:
                rows.append(row)
        rows.sort(key=lambda r: r[variants[0][0]]['instrs'], reverse=True)
        totals = {name: {k: sum(r[name][k] for r in rows if isinstance(r[name].get(k), int))
                         for k in ('instrs', 'alu', 'spills', 'hot-mem-blocks', 'hot-branches')}
                  for name, _ in variants}
        json.dump(dict(shaders=len(rows), totals=totals, ranking=rows), sys.stdout, indent=1)
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
