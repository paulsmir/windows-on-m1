#!/usr/bin/env python3
"""Freeze the driver source of a commit for a builder run.

Usage: package_source.py NEW_ID NEW_DIR PACKAGE_BUILD [--repo CHECKOUT]
                         [--template BUILD_KMD_PS1] [--abi-manifest SOURCE_MANIFEST]

Writes <experiments>/NEW_DIR/{source.zip, source-manifest.json, build-kmd.ps1}:
drivers/apple-agx/** of the checkout's HEAD as a deterministic archive, its
per-file manifest, and the builder script derived from the EXP853 template that
rebuilds the native Asahi runtime from this source and checks both hashes.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from labenv import experiments, lab_env  # noqa: E402


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def render_build_script(template, new_id, new_dir, commit, manifest_sha, archive_sha, build):
    """Retain the EXP853 self-built runtime flow and bind it to this source."""
    if 'build-native-asahi-state.py' not in template or 'build-asahi-runtime-closure.py' not in template:
        raise ValueError('template must rebuild the native runtime')
    text = template.replace('EXP853-r133-unpublished', new_dir)
    for label, replacement in (('source manifest mismatch', manifest_sha),
                               ('source archive mismatch', archive_sha)):
        match = re.search(r"(?<=-ne ')[0-9a-f]{64}(?='\) \{ throw '" + label + r"')", text)
        if not match:
            raise ValueError('template lacks ' + label)
        text = text.replace(match.group(), replacement)
    old_commit = re.search(r"(?<=SourceCommit=')[0-9a-f]{40}(?=')", text)
    if not old_commit:
        raise ValueError('template lacks source commit')
    text = text.replace(old_commit.group(), commit)
    text = text.replace('--output $nout --gpuva *>', '--output $nout --gpuva --source-manifest $manifest *>')
    text = text.replace('-NativeRuntimeProps (Join-Path $nout',
                        '-NativeProvenancePython $py `\n    -NativeRuntimeProps (Join-Path $nout')
    text = text.replace('-PackageBuild 853', f'-PackageBuild {build}').replace('PackageBuild=853', f'PackageBuild={build}')
    text = text.replace(' -Incremental -SourceManifestPath', ' -SourceManifestPath')
    text = text.replace("[ordered]@{Verdict='R85_BUILD_ONLY';SourceCommit=",
                        "$nativeResult=Get-Content (Join-Path $nout 'result.json') -Raw | ConvertFrom-Json\n"
                        "[ordered]@{NativeArchiveCommit=$nativeResult.source_commit;"
                        "NativeArchiveSha256=$nativeResult.library.sha256;"
                        "NativeResourceSourceSha256=$nativeResult.resource_source.sha256;"
                        "NativePreparedResultSha256=$nativeResult.prepared_result_sha256;"
                        "Verdict='R85_BUILD_ONLY';SourceCommit=")
    text = text.replace('EXP853 R85 PACKAGE853', f'{new_id} R85 PACKAGE{build}')
    for required in (manifest_sha, archive_sha, commit, '--source-manifest $manifest',
                     '-NativeProvenancePython $py', 'NativeResourceSourceSha256='):
        if required not in text:
            raise ValueError('derived build script lacks ' + required)
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('new_id')
    parser.add_argument('new_dir')
    parser.add_argument('build', help='package build number (driver version 30.0.<build>.0)')
    parser.add_argument('--repo', default=None, help='checkout whose HEAD is packaged (default AGX_REPO)')
    parser.add_argument('--template', default=None)
    parser.add_argument('--abi-manifest', default=None)
    a = parser.parse_args()
    exp = experiments()
    repo = a.repo or lab_env('AGX_REPO')
    template = Path(a.template or exp / 'EXP853-r133-unpublished' / 'build-kmd.ps1')
    abi = Path(a.abi_manifest or exp / 'EXP834-r114-private' / 'source-manifest.json')
    d = exp / a.new_dir
    d.mkdir(parents=True, exist_ok=True)
    git = lambda *args, **kw: subprocess.check_output(['git', '-C', repo, *args], **kw)  # noqa: E731
    commit = git('rev-parse', 'HEAD', text=True).strip()
    tree = git('ls-tree', '-r', commit, '--', 'drivers/apple-agx', text=True)
    files = []
    with zipfile.ZipFile(d / 'source.zip', 'w', zipfile.ZIP_DEFLATED) as zf:
        for line in tree.splitlines():
            meta, path = line.split('\t', 1)
            _, typ, blob = meta.split()
            if typ != 'blob':
                continue
            data = git('cat-file', 'blob', blob)
            files.append({'git_blob_sha1': blob, 'path': path, 'sha256': hashlib.sha256(data).hexdigest()})
            zi = zipfile.ZipInfo(path, (1980, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(zi, data)
    manifest = {'abi_headers': json.loads(abi.read_text())['abi_headers'], 'file_count': len(files),
                'files': files, 'repository_commit': commit,
                'source_archive_sha256': sha256(d / 'source.zip'), 'source_scope': 'drivers/apple-agx/**'}
    (d / 'source-manifest.json').write_text(json.dumps(manifest, indent=1))
    msha = sha256(d / 'source-manifest.json')
    script = render_build_script(template.read_text(), a.new_id, a.new_dir, commit, msha,
                                 manifest['source_archive_sha256'], a.build)
    (d / 'build-kmd.ps1').write_text(script)
    print(json.dumps({'commit': commit, 'files': len(files), 'manifest': msha,
                      'archive': manifest['source_archive_sha256'], 'build_script': sha256(d / 'build-kmd.ps1')}))


if __name__ == '__main__':
    main()
