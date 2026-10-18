#!/usr/bin/env python3
"""Audit every SHA-256 literal by its semantic target, never by hash lookup.

Output is TSV (outside the scanned extensions); unknown references fail closed.
Source inputs are checked against actual members of the pinned source archive.
Native build inputs are byte copies fetched from the EXP866 builder directory.
"""
import argparse
import csv
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

HEX = re.compile(r"(?<![0-9a-fA-F])[0-9a-fA-F]{64}(?![0-9a-fA-F])")
INF = "AppleAgxRenderAdmission.inf"
SYS = "AppleAgxRenderAdmission.sys"
UMD = "AppleAgxRenderAdmissionUmd.dll"
CAT = "appleagxrenderadmission.cat"
PKG = [INF, SYS, UMD, CAT]
RECOVERY = "../EXP810-g4-package817/recovery/"
HIDDEN = "../EXP-20260903-385-hvc-single-page/recovery/J313_EFI-no-agx-autoboot.fd"

# Each ordered reference was identified from the operand of the corresponding
# comparison/argument, including multiple comparisons on a single source line.
SCRIPT_TARGETS = {
    "autologger-stage.ps1": [INF],
    "build-kmd.ps1": ["source-manifest.json", "source.zip", "source-manifest.json"],
    "diagnostics-clean.ps1": [INF],
    "emergency-hidden.sh": [RECOVERY + "m1n1-exp377.macho", RECOVERY + "J313_EFI-exp392.fd", HIDDEN],
    "extract-verify.ps1": ["guest-payload.tar"],
    "hidden-before-cleanup.ps1": [INF, SYS, UMD],
    "invoke-hidden-cleanup.ps1": [INF, SYS, UMD],
    "invoke-hidden-diagnostics-clean.ps1": [INF],
    "invoke-verified-hidden-diagnostics-clean-exp866.ps1": [INF],
    "invoke-verified-hidden-diagnostics-clean.ps1": [INF],
    "ordinary-direct-final.sh": [RECOVERY + "m1n1-exp377.macho", RECOVERY + "J313_EFI-exp392.fd"],
    "resume-stage.ps1": PKG + ["autologger-stage.ps1"],
    "stage.ps1": PKG + ["pinned.cer", "../EXP726-air-signing/signtool-arm64.exe"],
    "verify-transfer.ps1": ["hardware-manifest.json", "guest-transfer-manifest.json"],
}
FIELDS = {
    "source_manifest_sha256": "source-manifest.json",
    "SourceManifestSha256": "source-manifest.json",
    "source_archive_sha256": "source.zip",
    "inf_sha256": INF,
    "build_receipt_sha256": "kmd-build-receipt.json",
    "BuildLogSha256": "arm64-build.log",
    "NativeArchiveSha256": "audit-inputs/native_runtime.lib",
    "NativeResourceSourceSha256": "audit-inputs/Resource.cpp",
    "NativePreparedResultSha256": "audit-inputs/native-prepared-result.txt",
    "reference_contract_sha256": "../EXP855E-r141-attachment-envelope/contract.bin",
    "source_diff_sha256": "candidate-code.diff",
    "diff_sha256": "candidate-code.diff",
    "hardware_manifest_sha256": "hardware-manifest.json",
    "guest_transfer_manifest_sha256": "guest-transfer-manifest.json",
    "payload_sha256": "guest-payload.tar",
}


def json_targets(value, path=(), parent=None):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from json_targets(child, path + (key,), value)
    elif isinstance(value, list):
        for i, child in enumerate(value):
            yield from json_targets(child, path + (str(i),), value)
    elif isinstance(value, str):
        for match in HEX.finditer(value):
            key = path[-1]
            target = FIELDS.get(key)
            if len(path) >= 2 and path[-2] in ("files", "artifact_sha256", "ArtifactSha256", "package_sha256"):
                target = key
            if len(path) >= 2 and path[-2] == "firmware_sha256":
                target = "firmware/" + key
            if len(path) >= 2 and path[-2] == "immutable_reference_sha256":
                target = "../" + key
            if key == "sha256" and isinstance(parent, dict) and "git_blob_sha1" in parent:
                target = "source.zip!" + parent["path"]
            yield match.group(), "/".join(path), target


def audit(base):
    rows = []
    scanned = []
    for file in sorted(base.rglob("*")):
        if not file.is_file() or file.suffix not in (".ps1", ".sh", ".py", ".json"):
            continue
        name = file.relative_to(base).as_posix()
        scanned.append(name)
        text = file.read_text(encoding="utf-8-sig")
        literals = list(HEX.finditer(text))
        if file.suffix == ".json":
            refs = list(json_targets(json.loads(text)))
            if [m.group() for m in literals] != [r[0] for r in refs]:
                raise ValueError("unmapped JSON lexical literal: " + name)
        else:
            targets = SCRIPT_TARGETS.get(name, [])
            refs = [(m.group(), "comparison/argument " + str(i + 1), targets[i] if i < len(targets) else None)
                    for i, m in enumerate(literals)]
            if len(targets) != len(literals):
                # Never let a newly inserted literal inherit an unrelated target.
                refs = [(m.group(), "unexpected literal count", None) for m in literals]
        for match, (literal, reference, target) in zip(literals, refs):
            actual = ""
            status = "UNRESOLVED"
            if target:
                try:
                    if target.startswith("source.zip!"):
                        with zipfile.ZipFile(base / "source.zip") as archive:
                            data = archive.read(target.split("!", 1)[1])
                    else:
                        data = (base / target).read_bytes()
                    actual = hashlib.sha256(data).hexdigest()
                    status = "MATCH" if actual == literal.lower() else "MISMATCH"
                except (OSError, KeyError) as exc:
                    status = "MISSING: " + str(exc)
            rows.append(dict(file=name, line=text.count("\n", 0, match.start()) + 1,
                             literal=literal, reference=reference, target=target or "",
                             actual_sha256=actual, status=status))
    return scanned, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    scanned, rows = audit(args.base.resolve())
    with args.output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    args.output.with_suffix(".files.txt").write_text("\n".join(scanned) + "\n")
    bad = [r for r in rows if r["status"] != "MATCH"]
    print(f"FILES {len(scanned)} LITERALS {len(rows)} MATCH {len(rows)-len(bad)} BAD {len(bad)}")
    for row in bad:
        print(f"{row['file']}:{row['line']} {row['status']} -> {row['target']}")
    return bool(bad)


if __name__ == "__main__":
    sys.exit(main())
