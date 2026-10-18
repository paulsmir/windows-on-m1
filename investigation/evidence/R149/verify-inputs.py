"""Verify the saved EXP862 inputs without contacting the Air or changing them."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import zipfile

root = Path(__file__).resolve().parents[3]
experiment = Path(sys.argv[1]).resolve()
sha = lambda data: hashlib.sha256(data).hexdigest()
manifest = json.loads((experiment / "source-manifest.json").read_text())
assert sha((experiment / "source.zip").read_bytes()) == manifest["source_archive_sha256"]
with zipfile.ZipFile(experiment / "source.zip") as archive:
    for item in manifest["files"]:
        assert sha(archive.read(item["path"])) == item["sha256"], item["path"]
guest = []
for item in json.loads((experiment / "hardware-evidence/artifact-hashes.json").read_text(encoding="utf-8-sig")):
    path = experiment / "hardware-evidence" / item["Name"]
    actual = sha(path.read_bytes())
    assert actual == item["SHA256"].lower(), path
    assert path.stat().st_size == item["Bytes"], path
    guest.append({"path": str(path.relative_to(experiment)), "bytes": item["Bytes"], "sha256": actual})
receipt = json.loads((experiment / "kmd-build-receipt.json").read_text(encoding="utf-8-sig"))
for name, expected in receipt["ArtifactSha256"].items():
    assert sha((experiment / "package" / name).read_bytes()) == expected, name
assert receipt["SourceCommit"] == receipt["NativeArchiveCommit"] == manifest["repository_commit"]
def git(*args):
    return subprocess.check_output(["git", *args], cwd=root)
result = {
    "base_commit": git("rev-parse", "HEAD").decode().strip(),
    "package_source": receipt["SourceCommit"],
    "verified_source_files": len(manifest["files"]),
    "guest_files": guest,
    "package_hashes": receipt["ArtifactSha256"],
    "hardware_result_sha256": sha((experiment / "hardware-result.json").read_bytes()),
    "review_sha256": sha((experiment.parents[1] / "tandem/REVIEW.md").read_bytes()),
    "nested_dirty_diff_sha256": {
        name: sha(subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=root / name))
        for name in ("m1n1_windows", "mu")
    },
}
(Path(__file__).parent / "inputs.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"PASS: {len(manifest['files'])} source files, {len(guest)} guest originals, four package artifacts")
