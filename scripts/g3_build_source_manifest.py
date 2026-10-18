#!/usr/bin/env python3
"""Pin every in-repository Apple AGX build input to one Git commit."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile


PREFIX = "drivers/apple-agx/"


def create_manifest(root: Path, commit: str, manifest_path: Path, archive_path: Path):
    root = root.resolve()
    commit = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "--verify", f"{commit}^{{commit}}"], text=True
    ).strip()
    tree = subprocess.check_output(
        ["git", "-C", str(root), "ls-tree", "-r", "-z", commit, "--", PREFIX]
    )
    files = []
    payloads = []
    for record in tree.split(b"\0"):
        if not record:
            continue
        attributes, raw_path = record.split(b"\t", 1)
        mode, kind, blob = attributes.decode().split()
        path = raw_path.decode()
        if kind != "blob" or mode not in ("100644", "100755") or not path.startswith(PREFIX):
            raise ValueError(f"unsupported source entry: {path}")
        current = root / path
        if current.is_symlink() or not current.is_file():
            raise ValueError(f"missing or linked source: {path}")
        content = current.read_bytes()
        blob_sha1 = hashlib.sha1(f"blob {len(content)}\0".encode() + content).hexdigest()
        if blob_sha1 != blob:
            raise ValueError(f"working tree differs from {commit}: {path}")
        files.append({"path": path, "git_blob_sha1": blob, "sha256": hashlib.sha256(content).hexdigest()})
        payloads.append((path, content))
    if not files:
        raise ValueError("no Apple AGX sources found")
    abi_headers = [item["path"] for item in files if item["path"].endswith(("_abi.h", ".generated.h"))]
    required = {
        "drivers/apple-agx/render-admission/include/hv_guest_ipa_pa_abi.h",
        "drivers/apple-agx/render-admission/include/j313_agx_abi_admission.generated.h",
    }
    if root.joinpath("drivers/apple-agx/render-admission").exists() and not required.issubset(abi_headers):
        raise ValueError("m1n1/guest ABI headers missing from committed input set")
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path, content in payloads:
            info = zipfile.ZipInfo(path, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, content)
    manifest = {
        "repository_commit": commit,
        "source_scope": PREFIX + "**",
        "file_count": len(files),
        "abi_headers": abi_headers,
        "files": files,
        "source_archive_sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest(),
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest, archive_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--commit", required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    args = parser.parse_args()
    manifest, _ = create_manifest(args.root, args.commit, args.manifest, args.archive)
    print(f"{manifest['file_count']} committed Apple AGX inputs at {manifest['repository_commit']}")


if __name__ == "__main__":
    main()
