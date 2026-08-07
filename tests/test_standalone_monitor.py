from dataclasses import dataclass
from pathlib import Path
import tempfile
import unittest

from tools import standalone_monitor
from tools.standalone_monitor import (
    MonitorPair,
    MonitorPort,
    PortSelectionError,
    capture_generation,
    generation_directory,
    select_monitor_ports,
)


@dataclass(frozen=True)
class FakePortInfo:
    device: str
    vid: int | None
    pid: int | None
    serial_number: str | None
    location: str | None


def port(device, serial="monitor-test", location="3-1", vid=0x1209, pid=0x316D):
    return FakePortInfo(device, vid, pid, serial, location)


class MonitorPortSelectionTests(unittest.TestCase):
    def test_selects_one_metadata_matched_m1n1_pair_and_ignores_unrelated_ports(self):
        ports = [
            port("/dev/cu.usbmodem-test3"),
            FakePortInfo("/dev/cu.debug-console", 0x05AC, 0x1337, "other", "2-1"),
            port("/dev/cu.usbmodem-test1"),
        ]

        pair = select_monitor_ports(ports)

        self.assertEqual(pair.console.device, "/dev/cu.usbmodem-test1")
        self.assertEqual(pair.vuart.device, "/dev/cu.usbmodem-test3")

    def test_explicit_paths_win_without_port_metadata(self):
        pair = select_monitor_ports(
            [], explicit_console="/dev/cu.explicit-console", explicit_vuart="/dev/cu.explicit-vuart"
        )

        self.assertEqual(pair.console.device, "/dev/cu.explicit-console")
        self.assertEqual(pair.vuart.device, "/dev/cu.explicit-vuart")

    def test_multiple_matching_devices_are_rejected_with_every_candidate(self):
        ports = [
            port("/dev/cu.first-a", serial="first", location="1-1"),
            port("/dev/cu.first-b", serial="first", location="1-1"),
            port("/dev/cu.second-a", serial="second", location="2-1"),
            port("/dev/cu.second-b", serial="second", location="2-1"),
        ]

        with self.assertRaises(PortSelectionError) as caught:
            select_monitor_ports(ports)

        message = str(caught.exception)
        for candidate in ports:
            self.assertIn(candidate.device, message)

    def test_generation_directory_is_stable_and_zero_padded(self):
        self.assertEqual(
            generation_directory(Path("captures"), 7), Path("captures/generation-007")
        )


class FakeSerial:
    def __init__(self, chunks):
        self.chunks = iter(chunks)
        self.closed = False
        self.read_calls = 0

    def read(self, _size):
        self.read_calls += 1
        value = next(self.chunks)
        if isinstance(value, Exception):
            raise value
        return value

    def close(self):
        self.closed = True


class MonitorCaptureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.pair = MonitorPair(
            MonitorPort("console", "test", "1-1"),
            MonitorPort("vuart", "test", "1-1"),
        )

    def make_factory(self, generation):
        opened = {}

        def factory(device, **_kwargs):
            stream = FakeSerial(
                [
                    f"{device}-{generation}-one\n".encode(),
                    f"{device}-{generation}-two\n".encode(),
                    standalone_monitor.serial.SerialException(f"{device} disconnected"),
                ]
            )
            opened[device] = stream
            return stream

        return factory, opened

    def test_capture_writes_exact_raw_timestamped_and_disconnect_logs(self):
        factory, opened = self.make_factory(1)

        capture_generation(
            self.pair,
            self.root,
            1,
            serial_factory=factory,
            timestamp=lambda: "2026-08-07T12:00:00.000Z",
        )

        directory = self.root / "generation-001"
        self.assertEqual(
            (directory / "console.raw").read_bytes(),
            b"console-1-one\nconsole-1-two\n",
        )
        self.assertEqual(
            (directory / "vuart.raw").read_bytes(), b"vuart-1-one\nvuart-1-two\n"
        )
        self.assertIn("console-1-one", (directory / "console.tlog").read_text())
        self.assertIn("vuart-1-two", (directory / "vuart.tlog").read_text())
        events = (directory / "events.log").read_text()
        self.assertIn("opened console=console vuart=vuart", events)
        self.assertIn("disconnect", events)
        self.assertTrue(all(stream.closed for stream in opened.values()))

    def test_next_generation_never_overwrites_the_previous_capture(self):
        first_factory, _ = self.make_factory(1)
        second_factory, _ = self.make_factory(2)

        capture_generation(self.pair, self.root, 1, serial_factory=first_factory)
        capture_generation(self.pair, self.root, 2, serial_factory=second_factory)

        self.assertIn(b"console-1-one", (self.root / "generation-001/console.raw").read_bytes())
        self.assertIn(b"console-2-one", (self.root / "generation-002/console.raw").read_bytes())


if __name__ == "__main__":
    unittest.main()
