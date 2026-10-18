"""Fail a package build if its native archive lacks matching source provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


RESOURCE = "src/gallium/frontends/d3d10umd/Resource.cpp"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify(manifest_path, props_path, result_path, prepared_path):
    source = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    result = json.loads(result_path.read_text(encoding="utf-8-sig"))
    prepared = json.loads(prepared_path.read_text(encoding="utf-8-sig"))
    root = ET.parse(props_path).getroot()
    fields = {node.tag.rsplit("}", 1)[-1]: node.text for node in root.iter()}
    commit = source["repository_commit"]
    manifest_hash = sha256(manifest_path)
    archive = Path(fields["NativeRuntimeLibrary"])
    expected_archive_hash = fields["NativeRuntimeLibrarySha256"]
    resource = result["resource_source"]
    checks = {
        "source commit": len(commit) == 40 and result["source_commit"] == commit
                         and fields["NativeRuntimeSourceCommit"] == commit,
        "source manifest": result["source_manifest_sha256"] == manifest_hash
                           and fields["NativeRuntimeSourceManifestSha256"] == manifest_hash,
        "archive identity": Path(result["library"]["path"]) == archive
                            and result["library"]["sha256"] == expected_archive_hash
                            and sha256(archive) == expected_archive_hash,
        "prepared projection": result["prepared_result_sha256"] == sha256(prepared_path),
        "compiled Resource.cpp": sha256(resource["path"]) == resource["sha256"]
                                 and prepared["overlays"][RESOURCE]["final_sha256"] == resource["sha256"],
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ValueError("native runtime provenance mismatch: " + ", ".join(failed))
    print("native runtime provenance PASS commit={} archive={} Resource.cpp={}".format(
        commit, expected_archive_hash, resource["sha256"]))


def main():
    parser = argparse.ArgumentParser()
    for name in ("source-manifest", "props", "result", "prepared-result"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        verify(args.source_manifest, args.props, args.result, args.prepared_result)
    except (OSError, ValueError, KeyError, ET.ParseError) as error:
        print("native runtime provenance rejected: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
