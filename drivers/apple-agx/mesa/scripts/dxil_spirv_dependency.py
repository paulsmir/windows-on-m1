#!/usr/bin/env python3
"""Original C15 immutable source-cache gate. No compiler, network or GPU calls.

Only already downloaded, pinned primary archives enter this helper. It validates
all archive members before extracting, verifies every cached byte against those
archives, then checks the narrower reviewed runtime inputs. A private full source
cache is not authorization to distribute that whole source tree.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import tarfile
import tempfile
from urllib.parse import urlparse


class DependencyError(ValueError):
    pass


TARGETS = {'dxil-debug', 'dxil-utils', 'spirv-module', 'dxil-converter',
           'dxil-spirv-c-static', 'llvm-bc', 'glslang-spirv-builder',
           'bc-decoder', 'dxbc-spirv'}
OPTIONS = {'DXIL_SPIRV_CLI', 'DXIL_SPIRV_NATIVE_LLVM', 'DXIL_SPV_MISC_CLI',
           'SPIRV_HEADERS_ENABLE_TESTS', 'SPIRV_HEADERS_ENABLE_INSTALL'}
GROUPS = {f'N{i:02d}' for i in range(1, 15)}
HEX40 = re.compile(r'[0-9a-f]{40}\Z')
HEX64 = re.compile(r'[0-9a-f]{64}\Z')
RESERVED = {'CON', 'PRN', 'AUX', 'NUL', 'CONIN$', 'CONOUT$', 'CLOCK$'} | {f'{p}{n}' for p in ('COM', 'LPT') for n in range(1, 10)}


def _require(condition, message):
    if not condition:
        raise DependencyError(message)


def _path(value):
    # All three reviewed archives use ASCII paths. Reject Unicode normalization
    # ambiguity rather than silently broadening the Windows/macOS cache domain.
    _require(isinstance(value, str) and value and value.isascii() and '\\' not in value and '\x00' not in value,
             f'unsafe path: {value!r}')
    p = PurePosixPath(value)
    _require(bool(p.parts) and not p.is_absolute() and str(p) == value and all(
        part not in {'.', '..'} and part.casefold() != '.git' and not part.endswith(('.', ' ')) and
        not any(c in part for c in ':<>"|?*') and not any(ord(c) < 32 or ord(c) == 127 for c in part) and
        part.split('.')[0].rstrip(' ').upper() not in RESERVED for part in p.parts), f'unsafe path: {value!r}')
    return p


def _digest(value, pattern=HEX64):
    return isinstance(value, str) and pattern.fullmatch(value) is not None


def _is_link(path):
    # Windows junctions are reparse-point directories, not symbolic links.
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def sha256_file(path):
    path = Path(path)
    _require(path.is_file() and not _is_link(path) and path.stat().st_nlink == 1, f'not a regular file: {path}')
    h = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def lock_digest(lock):
    return hashlib.sha256(json.dumps(lock, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def _no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        _require(key not in result, f'duplicate JSON key: {key}')
        result[key] = value
    return result


def load_lock(path, *, require_archive_digests=True):
    try:
        lock = json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=_no_duplicate_keys)
    except (OSError, json.JSONDecodeError) as error:
        raise DependencyError(f'cannot read lock: {error}') from error
    validate_lock(lock, require_archive_digests=require_archive_digests)
    return lock


def validate_lock(lock, *, require_archive_digests=True):
    _require(lock.get('schema_version') == 1, 'unsupported lock schema')
    components = lock.get('components', [])
    names = [c['name'] for c in components]
    _require(len(names) == 3 and set(names) == {'dxil-spirv', 'dxbc-spirv', 'spirv-headers'}, 'component graph')
    for c in components:
        _path(c['name'])
        _require(_digest(c['revision'], HEX40), 'full component revision required')
        _path(c['archive_root'])
        repository = urlparse(c['repository'])
        archive = urlparse(c['archive_url'])
        _require(repository.scheme == 'https' and repository.netloc == 'github.com' and
                 len(repository.path.strip('/').split('/')) == 2 and not repository.query and not repository.fragment,
                 'primary repository URL')
        _require(archive.scheme == 'https' and archive.netloc == 'codeload.github.com' and
                 archive.path == repository.path + '/tar.gz/' + c['revision'] and
                 not archive.query and not archive.fragment, 'immutable primary archive URL')
        _require(c['archive_root'] == repository.path.split('/')[-1] + '-' + c['revision'], 'archive root identity')
        _require((not require_archive_digests and c.get('archive_sha256') is None) or
                 _digest(c.get('archive_sha256')), 'locked archive digest required')
        _require(isinstance(c['max_members'], int) and 0 < c['max_members'] <= 100000 and
                 isinstance(c['max_unpacked_bytes'], int) and 0 < c['max_unpacked_bytes'] <= 1024**3,
                 'archive limit policy')
    files = lock.get('files', [])
    seen = set()
    for f in files:
        p = _path(f['path'])
        _require(p.parts[0] in names and len(p.parts) > 1 and f['path'].casefold() not in seen,
                 'reviewed path collision/component')
        seen.add(f['path'].casefold())
        _require(_digest(f['sha256']) and f['role'] in {'TU', 'H', 'IDE', 'metadata'} and
                 (f['notice'] in GROUPS if f['role'] != 'metadata' else f['notice'] is None), 'reviewed input metadata')
    _require(len(files) == lock.get('expected_file_count'), 'reviewed file count')
    by_path = {f['path']: f for f in files}
    _require(set(lock.get('targets', {})) == TARGETS, 'selected target graph')
    tus = []
    for paths in lock['targets'].values():
        _require(len(paths) == len(set(paths)), 'duplicate target source')
        for p in paths:
            _require(p in by_path, f'unreviewed target input: {p}')
            if p.endswith(('.cpp', '.c')):
                _require(by_path[p]['role'] == 'TU', 'target TU role')
                tus.append(p)
    _require(len(tus) == len(set(tus)) == lock.get('expected_tu_count') and
             set(tus) == {p for p, f in by_path.items() if f['role'] == 'TU'}, 'translation unit graph')
    _require(set(lock.get('gitlinks', {})) == set(names), 'gitlink component inventory')
    for links in lock['gitlinks'].values():
        for path, revision in links.items():
            _path(path)
            _require(_digest(revision, HEX40), 'full gitlink revision required')
    _require(set(lock['build']['options']) == OPTIONS and
             all(v == 'OFF' for v in lock['build']['options'].values()) and
             lock['build']['target'] == 'dxil-spirv-c-static', 'selected runtime options')
    archives = lock['build']['windows_archives']
    _require(len(archives) == len(set(archives)) == 9, 'nine static archives required')
    for path in archives:
        _path(path)
        _require(path.endswith('.lib'), 'Windows archive name')
    toolchain = lock['build']['toolchain']
    _require(toolchain['machine'] == 'ARM64' and toolchain['crt'] == 'MultiThreadedDLL' and
             set(toolchain['tool_sha256']) == {'cl.exe', 'link.exe', 'lib.exe', 'dumpbin.exe', 'cmake.exe'} and
             all(_digest(h) for h in toolchain['tool_sha256'].values()), 'toolchain/CRT provenance')
    _require(set(lock.get('notice_groups', {})) == GROUPS and
             set(lock['packaging']['notice_groups_required']) == GROUPS, 'N01-N14 notice inventory')
    for group, info in lock['notice_groups'].items():
        _require(_digest(info['text_sha256']) and info['file_count'] ==
                 sum(f['notice'] == group for f in files), 'notice file/text index')
    _require(lock['packaging']['notices_required_before_catalog_signing'] is True and
             lock['packaging']['microsoft_tool_crt_redistribution_authorized'] is False and
             lock['packaging']['driver_shader_model_advertisement_authorized'] is False,
             'packaging authorization boundary')


def archive_filename(component):
    return component['name'] + '-' + component['revision'] + '.tar.gz'


def inspect_archive(component, archive):
    """Return complete byte/directory inventory; no filesystem extraction."""
    archive = Path(archive)
    _require(archive.is_file() and not _is_link(archive), 'archive must be regular')
    files, directories, seen = {}, set(), set()
    total = count = 0
    try:
        with tarfile.open(archive, 'r:gz') as source:
            for member in source:
                count += 1
                _require(count <= component['max_members'], 'archive member limit')
                name = member.name.rstrip('/') if member.isdir() else member.name
                path = _path(name)
                _require(path.parts[0] == component['archive_root'], 'archive root mismatch')
                _require(member.isdir() or (member.isfile() and not member.issparse()), 'archive link/special member refused')
                key = name.casefold()
                _require(key not in seen, f'archive path collision: {name}')
                seen.add(key)
                relative = PurePosixPath(*path.parts[1:])
                if len(path.parts) == 1:
                    _require(member.isdir(), 'archive root must be directory')
                    continue
                rel = str(relative)
                for parent in relative.parents:
                    if str(parent) != '.':
                        directories.add(str(parent))
                if member.isdir():
                    directories.add(rel)
                    continue
                total += member.size
                _require(member.size >= 0 and total <= component['max_unpacked_bytes'], 'archive size limit')
                data = source.extractfile(member)
                _require(data is not None, 'missing archive file data')
                h = hashlib.sha256()
                for chunk in iter(lambda: data.read(1024 * 1024), b''):
                    h.update(chunk)
                files[rel] = h.hexdigest()
    except (OSError, tarfile.TarError, EOFError) as error:
        raise DependencyError(f'invalid archive: {error}') from error
    # Parent directory spelling and file/directory collisions must also be
    # portable to case-insensitive Windows/macOS filesystems.
    folded = {}
    for kind, paths in [('file', files), ('directory', directories)]:
        for p in paths:
            key = p.casefold()
            _require(key not in folded or folded[key] == (kind, p), f'archive path collision: {p}')
            folded[key] = (kind, p)
    return {'files': files, 'directories': sorted(directories)}


def _extract(component, archive, destination, expected):
    destination.mkdir()
    for directory in sorted(expected['directories'], key=lambda p: (p.count('/'), p)):
        (destination / directory).mkdir()
    with tarfile.open(archive, 'r:gz') as source:
        for member in source:
            if not member.isfile():
                continue
            relative = str(PurePosixPath(*PurePosixPath(member.name).parts[1:]))
            _require(relative in expected['files'], 'archive changed after inspection')
            with (destination / relative).open('xb') as target:
                shutil.copyfileobj(source.extractfile(member), target)
    _verify_tree(destination, expected)


def _verify_tree(root, expected):
    _require(root.is_dir() and not _is_link(root), f'cache tree missing/link: {root}')
    files, directories = {}, set()
    for path in root.rglob('*'):
        _require(not _is_link(path), f'cache tree link: {path}')
        relative = path.relative_to(root).as_posix()
        if path.is_dir():
            directories.add(relative)
        else:
            _require(stat.S_ISREG(path.stat().st_mode), 'cache tree special file')
            files[relative] = sha256_file(path)
    _require(files == expected['files'] and directories == set(expected['directories']), f'cache tree drift: {root}')


def _verify_inputs(lock, trees):
    for f in lock['files']:
        component, relative = f['path'].split('/', 1)
        _require(sha256_file(trees / component / relative) == f['sha256'], f'reviewed input drift: {f["path"]}')


def _receipt(lock):
    return {'schema_version': 1, 'lock_sha256': lock_digest(lock),
            'reviewed_files': len(lock['files']), 'translation_units': lock['expected_tu_count'],
            'archives': {c['name']: c['archive_sha256'] for c in lock['components']},
            'status': 'source_cache_verified_not_built'}


def stage_cache(lock, archive_directory, cache_directory):
    validate_lock(lock)
    archives, cache = Path(archive_directory), Path(cache_directory)
    _require(not cache.exists() and not _is_link(cache), 'cache already exists; refusing replacement')
    cache.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.dxil-stage-', dir=cache.parent) as temporary:
        staged = Path(temporary) / 'cache'
        (staged / 'archives').mkdir(parents=True)
        (staged / 'trees').mkdir()
        for c in lock['components']:
            original = archives / archive_filename(c)
            _require(sha256_file(original) == c['archive_sha256'], f'archive digest mismatch: {c["name"]}')
            copied = staged / 'archives' / archive_filename(c)
            shutil.copyfile(original, copied)
            _require(sha256_file(copied) == c['archive_sha256'], 'archive digest changed while copying')
            expected = inspect_archive(c, copied)
            _extract(c, copied, staged / 'trees' / c['name'], expected)
        _verify_inputs(lock, staged / 'trees')
        verify_declared_targets(lock, staged / 'trees')
        receipt = _receipt(lock)
        (staged / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        # Exclusive publication directory: do not replace even a concurrently
        # created empty cache. A crash during moves leaves an incomplete cache
        # that verification refuses; publish the receipt last.
        try:
            cache.mkdir()
        except FileExistsError as error:
            raise DependencyError('cache appeared while staging') from error
        for path in sorted(staged.iterdir(), key=lambda p: p.name == 'receipt.json'):
            path.rename(cache / path.name)
    return receipt


def verify_cache(lock, cache_directory):
    validate_lock(lock)
    cache = Path(cache_directory)
    _require(cache.is_dir() and not _is_link(cache), 'cache missing/link')
    _require({p.name for p in cache.iterdir()} == {'archives', 'trees', 'receipt.json'}, 'cache root drift')
    for folder in ('archives', 'trees'):
        _require((cache / folder).is_dir() and not _is_link(cache / folder), 'cache directory link')
    _require({p.name for p in (cache / 'trees').iterdir()} == {c['name'] for c in lock['components']}, 'cache tree set drift')
    _require({p.name for p in (cache / 'archives').iterdir()} == {archive_filename(c) for c in lock['components']}, 'cache archive set drift')
    for c in lock['components']:
        archive = cache / 'archives' / archive_filename(c)
        _require(sha256_file(archive) == c['archive_sha256'], 'cache archive digest mismatch')
        _verify_tree(cache / 'trees' / c['name'], inspect_archive(c, archive))
    _verify_inputs(lock, cache / 'trees')
    verify_declared_targets(lock, cache / 'trees')
    try:
        _require(not _is_link(cache / 'receipt.json') and (cache / 'receipt.json').stat().st_nlink == 1, 'cache receipt link')
        receipt = json.loads((cache / 'receipt.json').read_text(encoding='utf-8'), object_pairs_hook=_no_duplicate_keys)
    except (OSError, json.JSONDecodeError) as error:
        raise DependencyError(f'cache receipt: {error}') from error
    _require(receipt == _receipt(lock), 'cache receipt mismatch')
    return receipt


def verify_gitlinks(lock, actual):
    _require(actual == lock['gitlinks'], 'gitlink inventory mismatch')


def _configured_source(path, source_root):
    path = path.replace('\\', '/')
    if source_root is not None:
        prefix = str(source_root).replace('\\', '/').rstrip('/') + '/'
        if path.casefold().startswith(prefix.casefold()):
            path = path[len(prefix):]
    _path(path)
    for prefix, component in [('subprojects/dxbc-spirv/', 'dxbc-spirv/'),
                              ('third_party/spirv-headers/', 'spirv-headers/')]:
        if path.startswith(prefix):
            return component + path[len(prefix):]
    return 'dxil-spirv/' + path


def verify_declared_targets(lock, trees):
    """Read pinned literal STATIC source lists; never execute upstream CMake."""
    trees = Path(trees)
    found = {}
    for relative in ('CMakeLists.txt', 'bc/CMakeLists.txt', 'third_party/CMakeLists.txt'):
        text = (trees / 'dxil-spirv' / relative).read_text(encoding='utf-8')
        parent = str(PurePosixPath('dxil-spirv', relative).parent)
        for name in lock['targets']:
            match = re.search(r'add_library\(' + re.escape(name) + r'\s+STATIC\s+(.*?)\)', text, re.S)
            if not match:
                continue
            _require(name not in found, 'duplicate declared target')
            paths = []
            for token in match.group(1).split():
                if not token.endswith(('.cpp', '.c', '.hpp', '.h')):
                    raise DependencyError(f'nonliteral target source: {name}: {token}')
                if token.startswith('${DXBC_SPV_SOURCE_DIR}/'):
                    path = 'dxbc-spirv/' + token[len('${DXBC_SPV_SOURCE_DIR}/'):]
                else:
                    path = parent + '/' + token
                _path(path)
                paths.append(path)
            _require(paths == lock['targets'][name], f'declared target source drift: {name}')
            found[name] = paths
    _require(set(found) == set(lock['targets']), 'missing declared target')
    return lock['expected_tu_count']


def verify_codemodel(lock, reply_directory, source_root=None):
    """Check exact JSON target identity, never a filename-prefix wildcard."""
    targets = {}
    for path in Path(reply_directory).glob('*.json'):
        value = json.loads(path.read_text(encoding='utf-8'))
        if value.get('name') in lock['targets']:
            _require(value['name'] not in targets, 'duplicate configured target identity')
            targets[value['name']] = value
    _require(set(targets) == set(lock['targets']), 'missing configured target identity')
    count = 0
    for name, paths in lock['targets'].items():
        expected = {p for p in paths if p.endswith(('.cpp', '.c'))}
        actual = [_configured_source(s['path'], source_root) for s in targets[name]['sources']
                  if s['path'].endswith(('.cpp', '.c'))]
        _require(len(actual) == len(set(actual)) and set(actual) == expected, f'target source drift: {name}')
        count += len(actual)
    _require(count == lock['expected_tu_count'], 'configured TU count drift')
    return count


MOUNTS = {'dxil-spirv': '', 'dxbc-spirv': 'subprojects/dxbc-spirv',
          'spirv-headers': 'third_party/spirv-headers'}


def _assembly_inventory(lock, cache):
    expected = {'files': {}, 'directories': set()}
    for component in lock['components']:
        inventory = inspect_archive(component, Path(cache) / 'archives' / archive_filename(component))
        mount = MOUNTS[component['name']]
        if mount:
            parent = PurePosixPath(mount)
            expected['directories'].add(mount)
            expected['directories'].update(str(p) for p in parent.parents if str(p) != '.')
        for path, digest in inventory['files'].items():
            destination = mount + '/' + path if mount else path
            _require(destination not in expected['files'], 'source mount file collision')
            expected['files'][destination] = digest
        expected['directories'].update(mount + '/' + p if mount else p for p in inventory['directories'])
    return expected


def verify_build_source(lock, cache_directory, source_directory):
    verify_cache(lock, cache_directory)
    _verify_tree(Path(source_directory), _assembly_inventory(lock, cache_directory))
    return {'status': 'source_materialized_not_built', 'lock_sha256': lock_digest(lock)}


def materialize_source(lock, cache_directory, source_directory):
    """Original source assembly only; never run CMake or change immutable cache."""
    verify_cache(lock, cache_directory)
    source = Path(source_directory)
    _require(not source.exists() and not _is_link(source), 'build source already exists')
    source.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.dxil-source-', dir=source.parent) as temporary:
        staged = Path(temporary) / 'source'
        shutil.copytree(Path(cache_directory) / 'trees' / 'dxil-spirv', staged)
        for component in ('dxbc-spirv', 'spirv-headers'):
            destination = staged / MOUNTS[component]
            _require(not destination.exists() or (destination.is_dir() and not _is_link(destination) and
                     not any(destination.iterdir())), 'nonempty upstream submodule mount')
            shutil.copytree(Path(cache_directory) / 'trees' / component, destination, dirs_exist_ok=True)
        _verify_tree(staged, _assembly_inventory(lock, cache_directory))
        try:
            source.mkdir()
        except FileExistsError as error:
            raise DependencyError('build source appeared while staging') from error
        for path in staged.iterdir():
            path.rename(source / path.name)
    return verify_build_source(lock, cache_directory, source)


def collect_notices(lock, review_text):
    texts = dict(re.findall(r'^### (N\d+) [^\n]+\n\n```text\n(.*?)\n```', review_text, re.M | re.S))
    _require(set(texts) == GROUPS, 'notice review groups missing')
    for group, text in texts.items():
        _require(hashlib.sha256(text.encode()).hexdigest() == lock['notice_groups'][group]['text_sha256'],
                 f'notice text drift: {group}')
    return texts


def build_contract(lock, cache_directory):
    receipt = verify_cache(lock, cache_directory)
    return {'status': 'prepared_not_built', 'source_receipt': receipt,
            'helper_sha256': sha256_file(Path(__file__)),
            'target': lock['build']['target'], 'options': lock['build']['options'],
            'archives': lock['build']['windows_archives'], 'toolchain': lock['build']['toolchain'],
            'source_mounts': {'dxil-spirv': '.', 'dxbc-spirv': 'subprojects/dxbc-spirv',
                              'spirv-headers': 'third_party/spirv-headers'},
            'required_notice_groups': sorted(GROUPS),
            'required_build_receipts': ['exact configured target identity/TU graph',
                'compiler-observed project paths/hashes', 'nine archive paths/hashes',
                'tool version and SHA256 / SDK / explicit CRT policy',
                'C caller link + ARM64 PE machine/imports + CRT directives',
                'complete notices/licenses before catalog/signing'],
            'production_linkage_authorized': False, 'shader_model_caps_authorized': False,
            'runtime_execution_performed': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lock', type=Path, default=Path(__file__).with_name('dxil_spirv_dependency_lock.json'))
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('verify-cache', 'build-contract'):
        command = sub.add_parser(name)
        command.add_argument('--cache', type=Path, required=True)
    for name in ('materialize-source', 'verify-source', 'verify-codemodel'):
        command = sub.add_parser(name)
        command.add_argument('--cache', type=Path, required=True)
        command.add_argument('--source', type=Path, required=True)
        if name == 'verify-codemodel':
            command.add_argument('--reply', type=Path, required=True)
    stage = sub.add_parser('stage-cache')
    stage.add_argument('--archives', type=Path, required=True)
    stage.add_argument('--cache', type=Path, required=True)
    args = parser.parse_args()
    try:
        lock = load_lock(args.lock)
        # All CLI cache writes stay under this checkout's ignored .local.
        if args.command == 'stage-cache':
            local = Path(__file__).resolve().parents[4] / '.local'
            _require(args.cache.resolve().is_relative_to(local.resolve()), 'cache writes require ignored .local')
            result = stage_cache(lock, args.archives, args.cache)
        elif args.command == 'materialize-source':
            local = Path(__file__).resolve().parents[4] / '.local'
            _require(args.source.resolve().is_relative_to(local.resolve()), 'source writes require ignored .local')
            result = materialize_source(lock, args.cache, args.source)
        elif args.command in ('verify-source', 'verify-codemodel'):
            result = verify_build_source(lock, args.cache, args.source)
            if args.command == 'verify-codemodel':
                result['configured_translation_units'] = verify_codemodel(lock, args.reply, args.source)
        elif args.command == 'verify-cache':
            result = verify_cache(lock, args.cache)
        else:
            result = build_contract(lock, args.cache)
        print(json.dumps(result, indent=2))
    except (DependencyError, OSError, KeyError, TypeError) as error:
        parser.exit(1, f'DXIL_DEPENDENCY_REFUSED: {error}\n')


if __name__ == '__main__':
    main()
