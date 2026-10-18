#!/usr/bin/env python3
"""Claude helper: derive the next G3 experiment directory from a previous one.

Usage:
  mkexp.py source  NEW_ID NEW_DIR PACKAGE_BUILD            -> manifest, zip, build-kmd.ps1
  mkexp.py clone   OLD_ID OLD_DIR NEW_ID NEW_DIR           -> scripts, package, manifests
"""
import hashlib, json, os, re, shutil, subprocess, sys, zipfile, datetime

EXP = '/Users/pavel/public_windows/.local/experiments'
WT = '/Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler'
GIT = '/opt/homebrew/bin/git'
PKG = ['AppleAgxRenderAdmission.inf', 'AppleAgxRenderAdmission.sys',
       'AppleAgxRenderAdmissionUmd.dll', 'appleagxrenderadmission.cat']


def h(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


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
    text = text.replace('--output $nout --gpuva *>','--output $nout --gpuva --source-manifest $manifest *>')
    text = text.replace('-NativeRuntimeProps (Join-Path $nout', '-NativeProvenancePython $py `\n    -NativeRuntimeProps (Join-Path $nout')
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


def source(new_id, new_dir, build):
    d = os.path.join(EXP, new_dir)
    os.makedirs(d, exist_ok=True)
    commit = subprocess.check_output([GIT, '-C', WT, 'rev-parse', 'HEAD'], text=True).strip()
    tree = subprocess.check_output([GIT, '-C', WT, 'ls-tree', '-r', commit, '--', 'drivers/apple-agx'], text=True)
    files = []
    with zipfile.ZipFile(os.path.join(d, 'source.zip'), 'w', zipfile.ZIP_DEFLATED) as zf:
        for line in tree.splitlines():
            meta, path = line.split('\t', 1)
            _, typ, blob = meta.split()
            if typ != 'blob':
                continue
            data = subprocess.check_output([GIT, '-C', WT, 'cat-file', 'blob', blob])
            files.append({'git_blob_sha1': blob, 'path': path, 'sha256': hashlib.sha256(data).hexdigest()})
            zi = zipfile.ZipInfo(path, (1980, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(zi, data)
    old = json.load(open(os.path.join(EXP, 'EXP834-r114-private/source-manifest.json')))
    manifest = {'abi_headers': old['abi_headers'], 'file_count': len(files), 'files': files,
                'repository_commit': commit, 'source_archive_sha256': h(os.path.join(d, 'source.zip')),
                'source_scope': 'drivers/apple-agx/**'}
    json.dump(manifest, open(os.path.join(d, 'source-manifest.json'), 'w'), indent=1)
    msha = h(os.path.join(d, 'source-manifest.json'))
    tpl = render_build_script(open(os.path.join(EXP, 'EXP853-r133-unpublished/build-kmd.ps1')).read(),
                              new_id, new_dir, commit, msha, manifest['source_archive_sha256'], build)
    open(os.path.join(d, 'build-kmd.ps1'), 'w').write(tpl)
    print(json.dumps({'commit': commit, 'files': len(files), 'manifest': msha,
                      'archive': manifest['source_archive_sha256'], 'build_script': h(os.path.join(d, 'build-kmd.ps1'))}))


def clone(old_id, old_dir, new_id, new_dir):
    src, dst = os.path.join(EXP, old_dir), os.path.join(EXP, new_dir)
    old = {n: h(os.path.join(src, 'package', n)) for n in PKG}
    new = {n: h(os.path.join(dst, n)) for n in PKG}

    def sub(text):
        text = text.replace(old_dir, new_dir).replace(old_id, new_id)
        for n in PKG:
            text = text.replace(old[n], new[n]).replace(old[n].upper(), new[n].upper())
        return text

    for f in os.listdir(src):
        if f.endswith(('.ps1', '.py', '.sh')) and f not in ('build-kmd.ps1',):
            if f.startswith(('emergency', 'ordinary')) or not f.endswith('.sh') or f == 'full-owner-direct.sh':
                pass
            p = os.path.join(dst, f)
            open(p, 'w').write(sub(open(os.path.join(src, f)).read()))
            os.chmod(p, os.stat(os.path.join(src, f)).st_mode)
    for f in ['pinned.cer', 'AgxR105AllocateProbe.exe']:
        shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
    os.makedirs(os.path.join(dst, 'package'), exist_ok=True)
    for n in PKG:
        shutil.copy2(os.path.join(dst, n), os.path.join(dst, 'package', n))
    shutil.copytree(os.path.join(src, 'firmware'), os.path.join(dst, 'firmware'), dirs_exist_ok=True)
    g = json.load(open(os.path.join(src, 'guest-transfer-manifest.json')))
    g['files'] = {n: h(os.path.join(dst, n)) for n in g['files']}
    json.dump(g, open(os.path.join(dst, 'guest-transfer-manifest.json'), 'w'), indent=1)
    m = json.loads(sub(open(os.path.join(src, 'hardware-manifest.json')).read()))
    m['experiment'] = new_id
    m['utc_before'] = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    m['source_manifest_sha256'] = h(os.path.join(dst, 'source-manifest.json'))
    m['build_command'] = rf'C:\Users\pauls\{new_dir}\build-kmd.ps1 SHA256 ' + h(os.path.join(dst, 'build-kmd.ps1'))
    m['build_result'] = json.load(open(os.path.join(dst, 'kmd-build-receipt.json'), encoding='utf-8-sig'))
    m['artifact_sha256'] = {n: h(os.path.join(dst, n)) for n in m['artifact_sha256']}
    m['inf_sha256'] = m['artifact_sha256']['package/AppleAgxRenderAdmission.inf']
    m['launcher'] = 'full-owner-direct.sh'
    m['operator'] = 'Claude direct execution'
    json.dump(m, open(os.path.join(dst, 'hardware-manifest.json'), 'w'), indent=1)
    print(json.dumps({'hardware_manifest': h(os.path.join(dst, 'hardware-manifest.json')),
                      'guest_manifest': h(os.path.join(dst, 'guest-transfer-manifest.json')),
                      'package': new}))


if __name__ == '__main__':
    {'source': lambda a: source(a[0], a[1], a[2]), 'clone': lambda a: clone(*a)}[sys.argv[1]](sys.argv[2:])
