#!/usr/bin/env python3
"""Refresh EXP866 preparation in dependency order, without touching binaries."""
import hashlib
import io
import json
import re
import sys
import tarfile
import zipfile
from pathlib import Path

base = Path(sys.argv[1])


def sha(name):
    return hashlib.sha256((base / name).read_bytes()).hexdigest()


def read(name):
    return json.loads((base / name).read_text(encoding="utf-8-sig"))


def write(name, value):
    (base / name).write_text(json.dumps(value, indent=2) + "\n")


def pin(script, marker, target):
    lines = (base / script).read_text().splitlines(keepends=True)
    selected = [i for i, line in enumerate(lines) if marker in line]
    assert len(selected) == 1, (script, marker)
    i = selected[0]
    lines[i], count = re.subn(r"(?<![0-9a-fA-F])[0-9a-fA-F]{64}(?![0-9a-fA-F])", sha(target), lines[i])
    assert count == 1, (script, marker, count)
    (base / script).write_text("".join(lines))


pin("resume-stage.ps1", "throw 'autologger script mismatch'", "autologger-stage.ps1")

guest = read("guest-transfer-manifest.json")
guest["files"] = {name: sha(name) for name in guest["files"]}
write("guest-transfer-manifest.json", guest)

hardware = read("hardware-manifest.json")
hardware["artifact_sha256"] = {name: sha(name) for name in hardware["artifact_sha256"]}
write("hardware-manifest.json", hardware)

pin("verify-transfer.ps1", "throw 'hardware manifest mismatch'", "hardware-manifest.json")
pin("verify-transfer.ps1", "throw 'transfer manifest mismatch'", "guest-transfer-manifest.json")

package = {"AppleAgxRenderAdmission.inf", "AppleAgxRenderAdmission.sys", "AppleAgxRenderAdmissionUmd.dll", "appleagxrenderadmission.cat"}
names = ["package/" + name if name in package else name for name in guest["files"]]
names += ["guest-transfer-manifest.json", "hardware-manifest.json", "verify-transfer.ps1"]
assert len(names) == len(set(names)) == 42
with tarfile.open(base / "guest-payload.tar", "w", format=tarfile.USTAR_FORMAT) as archive:
    for name in sorted(names):
        data = (base / name).read_bytes()
        info = tarfile.TarInfo(name)
        info.size, info.mode, info.mtime = len(data), 0o644, 0
        archive.addfile(info, io.BytesIO(data))
pin("extract-verify.ps1", "throw 'payload mismatch'", "guest-payload.tar")

for name in ("readiness-manifest.json", "preparation-result.json"):
    result = read(name)
    for key, target in (("hardware_manifest_sha256", "hardware-manifest.json"),
                        ("guest_transfer_manifest_sha256", "guest-transfer-manifest.json"),
                        ("payload_sha256", "guest-payload.tar")):
        result[key] = sha(target)
    if "artifact_sha256" in result:
        result["artifact_sha256"] = {name: sha(name) for name in result["artifact_sha256"]}
    write(name, result)

with zipfile.ZipFile(base / "parse-scripts.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for file in sorted(base.glob("*.ps1")):
        info = zipfile.ZipInfo(file.name, (1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, file.read_bytes())
print("EXP866_PREPARATION_REFRESHED")
