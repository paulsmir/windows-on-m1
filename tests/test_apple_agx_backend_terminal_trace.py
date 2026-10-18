from pathlib import Path
import importlib.util
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/render-admission/scripts/decode-backend-terminal-trace.py"


class BackendTerminalTraceTests(unittest.TestCase):
    def test_decodes_channel_terminal_and_exit_fields(self):
        spec = importlib.util.spec_from_file_location("terminal_trace", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        words = (
            0x54600001000100FF,
            0x54A00100010300FF,
            0x54B0017F010300FF,
        )
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "host.log"
            log.write_text("\n".join(
                f"TTY> HV: AGX power receipt seq={word} cmd=0 state=3 result=0"
                for word in words
            ))
            records = module.decode(log)
        self.assertEqual(records[0], {
            "kind": "channel_progress", "ta_read": 1, "d3_read": 1,
            "fence": 255,
        })
        self.assertEqual(records[1]["kind"], "terminal_observation")
        self.assertEqual(records[1]["source"], 1)
        self.assertEqual(records[1]["runtime_phase"], 1)
        self.assertEqual(records[1]["provider_phase"], 3)
        self.assertEqual(records[2]["kind"], "worker_exit")
        self.assertEqual(records[2]["valid_mask"], 0x7F)


if __name__ == "__main__":
    unittest.main()
