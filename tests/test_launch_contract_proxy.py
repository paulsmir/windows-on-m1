from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LaunchContractProxyTests(unittest.TestCase):
    def test_proxy_uses_explicit_non_overlapping_opcodes(self):
        source = (ROOT / "m1n1_windows/proxyclient/m1n1/proxy.py").read_text()
        self.assertIn("P_HV_LAUNCH_PUBLISH = 0xc1d", source)
        self.assertIn("P_HV_LAUNCH_CAPTURE = 0xc1e", source)

    def test_c_dispatch_validates_descriptor_and_snapshot_sizes(self):
        source = (ROOT / "m1n1_windows/src/proxy.c").read_text()
        self.assertIn("sizeof(struct hv_launch_j313_descriptor)", source)
        self.assertIn("sizeof(struct hv_contract_snapshot)", source)
        self.assertIn("hv_launch_j313_publish_descriptor", source)
        self.assertIn("hv_launch_j313_capture", source)

    def test_user_interrupt_dumps_before_proxy_without_nested_request(self):
        hv_source = (ROOT / "m1n1_windows/src/hv.c").read_text()
        runner = (ROOT / "run_uefi.py").read_text()
        self.assertIn('printf("HV: User interrupt', hv_source)
        user_interrupt = hv_source.split('printf("HV: User interrupt', 1)[1].split(
            "hv_exc_proxy", 1
        )[0]
        self.assertIn("hv_watchdog_snapshot_dump();", user_interrupt)
        self.assertNotIn("p.hv_watchdog_dump()", runner)
        self.assertIn("return EXC_RET.UNHANDLED", runner)

    def test_capture_path_has_no_silent_fallback(self):
        # Contract v2: the launcher delegates to tools/launch_contract.py; run_uefi adds no
        # exception handling around publish or capture (tests/test_launch_contract_publish.py
        # covers the behaviour with the legacy switch set and unset).
        runner = (ROOT / "run_uefi.py").read_text()
        block = runner.split("launch_contract_available = publish_descriptor(", 1)[1].split(
            "\ncapture_contract(0, 1)", 1)[0]
        self.assertIn('allow_legacy=os.environ.get("WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT") == "1"', block)
        self.assertIn("command_error=ProxyCommandError", block)
        self.assertIn("capture_checkpoint(p, checkpoint, sequence, launch_contract_available", block)
        self.assertNotIn("try:", block)
        self.assertNotIn("except", block)

    def test_legacy_launch_contract_fallback_is_explicit_and_fail_closed(self):
        runner = (ROOT / "run_uefi.py").read_text()
        tool = (ROOT / "tools/launch_contract.py").read_text()

        self.assertIn("WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT", runner)
        self.assertNotIn("except ProxyCommandError", runner)
        self.assertIn("legacy launch-contract opcodes unavailable", tool)
        self.assertIn("p.request(p.P_HV_LAUNCH_CAPTURE, 0, 0, 0, 0)", tool)
        self.assertIn("if not available:", tool)

if __name__ == "__main__":
    unittest.main()
