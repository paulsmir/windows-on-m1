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

    def test_legacy_launch_contract_fallback_is_explicit_and_fail_closed(self):
        runner = (ROOT / "run_uefi.py").read_text()

        self.assertIn("WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT", runner)
        self.assertIn("except ProxyCommandError", runner)
        self.assertIn("legacy launch-contract opcodes unavailable", runner)
        self.assertIn("launch_contract_available = False", runner)
        self.assertIn("if not launch_contract_available:", runner)


if __name__ == "__main__":
    unittest.main()
