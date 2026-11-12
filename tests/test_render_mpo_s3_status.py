"""S3-B (EXP2374): dxgkrnl's Set3 thunk handles only STATUS_SUCCESS and
STATUS_DEVICE_BUSY (defined retry state); any other status leaves the flip
entry unadvanced and dxgkrnl offers it again forever, freezing the session.
Contract: for a configuration Check3 accepted, every Set3 outcome is SUCCESS
or DEVICE_BUSY; STATUS_INVALID_PARAMETER only for dxgkrnl contract
violations (rows A-C, F of the audit), each after AdmissionMpoRefuseSet3.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/render-admission/src"


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise AssertionError(signature)


def returns(body):
    return [m.group(1).strip() for m in re.finditer(r"\breturn\s+([^;]*);", body)]


class S3StatusContract(unittest.TestCase):
    def setUp(self):
        self.mpo = (SRC / "mpo_windows.c").read_text()
        self.scanout = (SRC / "scanout_windows.c").read_text()

    def test_commit_helpers_answer_busy_for_runtime_obstacles(self):
        commit = function(self.scanout, "NTSTATUS AdmissionScanoutMpoCommit(")
        queue = function(self.scanout, "static NTSTATUS AdmissionScanoutMpoQueueHeld(")
        off = function(self.scanout, "NTSTATUS AdmissionScanoutMpoPlaneOffSet(")
        for name, body, allowed_invalid in (("commit", commit, 1), ("queue", queue, 0), ("off", off, 1)):
            values = returns(body)
            invalid = [v for v in values if v == "STATUS_INVALID_PARAMETER"]
            self.assertEqual(len(invalid), allowed_invalid, name)
            for v in values:
                self.assertTrue(v in ("STATUS_DEVICE_BUSY", "STATUS_SUCCESS", "STATUS_INVALID_PARAMETER")
                                or v.startswith("AdmissionScanoutMpoQueueHeld("), (name, v))
        # The only INVALID_PARAMETER in commit/off is the argument guard (Records/Completion/Count).
        self.assertRegex(commit, r"if \(Records == NULL \|\| Completion == NULL \|\| \(Count != 1u && Count != 2u\)\)\s*return STATUS_INVALID_PARAMETER;")
        self.assertRegex(off, r"if \(Completion == NULL\)\s*return STATUS_INVALID_PARAMETER;")

    def test_set3_fails_only_for_contract_violations(self):
        body = function(self.mpo, "NTSTATUS AdmissionMpoSetVidPnSourceAddress3(")
        for v in returns(body):
            if v == "STATUS_INVALID_PARAMETER":
                continue
            self.assertTrue(v.startswith("AdmissionMpoBusyNote(Context, &completion, "), v)
            self.assertIn(v.split(",")[-1].strip().rstrip(")").strip(), ("STATUS_DEVICE_BUSY", "status"), v)
        # Every INVALID_PARAMETER follows AdmissionMpoRefuseSet3 (rows A, B, C: three sites;
        # FS-1 turned row F, LayerIndex 1 alone, into the lone application plane).
        sites = re.findall(r"(\w+)\(Context\);\s*return STATUS_INVALID_PARAMETER;", body)
        self.assertEqual(sites, ["AdmissionMpoRefuseSet3"] * 3)
        self.assertEqual(len(re.findall(r"return STATUS_INVALID_PARAMETER;", body)), 3)
        for bad in ("STATUS_DEVICE_HARDWARE_ERROR", "STATUS_DEVICE_NOT_READY", "STATUS_INVALID_ADDRESS"):
            self.assertNotIn("return " + bad, body)

    def test_status_variable_comes_only_from_busy_or_success_helpers(self):
        body = function(self.mpo, "NTSTATUS AdmissionMpoSetVidPnSourceAddress3(")
        sources = re.findall(r"\bstatus = ([A-Za-z_]+)", body)
        self.assertTrue(set(sources) <= {"AdmissionScanoutMpoCommit", "AdmissionScanoutMpoPlaneOffSet",
                                         "AdmissionDdiSetVidPnSourceAddress", "STATUS_DEVICE_BUSY"}, sources)
        # H2: the V2 path's failures other than a foreign handle become DEVICE_BUSY.
        self.assertRegex(body, r"if \(status != STATUS_DEVICE_BUSY && status != STATUS_INVALID_HANDLE\) \{[^}]*status = STATUS_DEVICE_BUSY;")


if __name__ == "__main__":
    unittest.main()
