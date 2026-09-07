from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"


class RetainedClassActivationTests(unittest.TestCase):
    def test_command_arena_is_adopted_before_the_only_graph_map(self):
        source = SOURCE.read_text()
        start = source.index("static unsigned char AdmissionRetainedActivate(")
        end = source.index("static unsigned char AdmissionFirmwareCreateUat(", start)
        body = source[start:end]
        ordered = (
            "AGX_RR_ACTIVATE",
            "AGX_RR_ARENA_COMMAND",
            "AGX_RR_ARENA_SHARED",
            "AppleAgxInitdataMemoryApplyQueueArenas",
            "AppleAgxRenderSharedMemoryBindRelocationObjects",
            "AppleAgxApplyRelocations",
            "AppleAgxContext0BrokerMap",
        )
        positions = [body.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(body.count("AppleAgxContext0BrokerMap("), 1)
        self.assertNotIn("AppleAgxUatMap(", body)
        self.assertNotIn("AGX_RR_ARENA_TIMESTAMP", body)

    def test_query_validates_exact_version_class_and_broker_range(self):
        source = SOURCE.read_text()
        start = source.index("static BOOLEAN AdmissionRetainedQueryArena(")
        end = source.index("\n}\n", start) + 3
        body = source[start:end]
        for token in (
            "AGX_RR_QUERY_ARENA",
            "AGX_RR_ARENA_VERSION",
            "ArenaClass",
            "ArenaVa",
            "ArenaBytes",
            "response.Epoch",
            "response.Root",
        ):
            self.assertIn(token, body)
        self.assertIn("request.Va = ArenaClass", body)
        self.assertIn("request.Epoch = runtime->RetainedEpoch", body)
        self.assertIn("AGX_RR_ARENA_COMMAND", body)


if __name__ == "__main__":
    unittest.main()
