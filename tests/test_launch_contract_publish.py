"""WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT covers only an image without the launch-contract opcodes.

Every bench launcher sets the switch (launch-kbd.sh, launch-wifi*.sh, launch-hidden.sh), so it
is the bench's normal state: no failure of the publish/probe/capture path may turn into
"skipped for legacy A/B".  Fake proxies model each failure with the switch set and unset.
"""
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from tools.launch_contract import (
    LEGACY_LINE,
    LaunchContractError,
    LAYOUTS,
    capture_checkpoint,
    decode_records,
    publish_descriptor,
)


class BadCommand(Exception):
    """Stands in for m1n1.proxy.ProxyCommandError (S_BADCMD: opcode unknown)."""


class OtherRemoteError(Exception):
    """Stands in for any other ProxyRemoteError / transport failure."""


class RecordError(Exception):
    """Stands in for proxyclient launch_record.LaunchRecordError."""


def v_record(checkpoint, sequence, version):
    size = LAYOUTS[version].size
    body = bytes(size - 24)
    return struct.pack("<IHHIIII", 0x4A43314C, version, 24, size - 24, checkpoint, sequence,
                       zlib.crc32(body)) + body


class FakeProxy:
    P_HV_LAUNCH_CAPTURE = 0xC1E

    def __init__(self, publish=True, probe=0, capture=None):
        self.publish, self.probe, self.capture = publish, probe, capture
        self.requests = []

    def hv_launch_publish(self, descriptor):
        if isinstance(self.publish, type) and issubclass(self.publish, BaseException):
            raise self.publish("publish")
        return self.publish

    def request(self, opcode, *args):
        self.requests.append((opcode, args))
        if isinstance(self.probe, type) and issubclass(self.probe, BaseException):
            raise self.probe("probe")
        return self.probe

    def hv_launch_capture(self, checkpoint, sequence):
        if isinstance(self.capture, type) and issubclass(self.capture, BaseException):
            raise self.capture("capture")
        if self.capture == "v1":
            return v_record(checkpoint, sequence, 1)
        if self.capture == "v2":
            return v_record(checkpoint, sequence, 2)
        return self.capture


def publish(proxy, allow_legacy):
    lines = []
    result = publish_descriptor(proxy, b"d", allow_legacy=allow_legacy, command_error=BadCommand,
                                log=lines.append)
    return result, lines


class PublishTests(unittest.TestCase):
    def test_contract_images_publish_with_and_without_the_switch(self):
        for switch in (False, True):
            result, lines = publish(FakeProxy(publish=True), switch)
            self.assertIs(result, True)
            self.assertEqual(lines, [])

    def test_the_switch_covers_only_an_image_without_both_opcodes(self):
        proxy = FakeProxy(publish=BadCommand, probe=BadCommand)
        result, lines = publish(proxy, True)
        self.assertIs(result, False)
        self.assertEqual(lines, [LEGACY_LINE])
        self.assertEqual(proxy.requests, [(0xC1E, (0, 0, 0, 0))])  # the size probe, nothing else

    def test_without_the_switch_an_unknown_publish_stops(self):
        with self.assertRaises(BadCommand):
            publish(FakeProxy(publish=BadCommand, probe=BadCommand), False)

    def test_switch_set_every_other_failure_stops(self):
        cases = [
            FakeProxy(publish=False),                          # descriptor refused
            FakeProxy(publish=0),
            FakeProxy(publish=OtherRemoteError),               # any other proxy error
            FakeProxy(publish=RecordError),
            FakeProxy(publish=BadCommand, probe=0),            # v1 image knows capture
            FakeProxy(publish=BadCommand, probe=5784),         # v2 image knows capture
            FakeProxy(publish=BadCommand, probe=4000),         # unexpected size
            FakeProxy(publish=BadCommand, probe=OtherRemoteError),
        ]
        for switch in (True, False):
            for proxy in cases:
                lines = []
                with self.subTest(switch=switch, publish=proxy.publish, probe=proxy.probe):
                    with self.assertRaises((LaunchContractError, OtherRemoteError, RecordError,
                                            BadCommand)):
                        publish_descriptor(proxy, b"d", allow_legacy=switch,
                                           command_error=BadCommand, log=lines.append)
                    self.assertNotIn(LEGACY_LINE, lines)


class CaptureTests(unittest.TestCase):
    def test_capture_writes_decodable_v1_and_v2_records(self):
        for version in ("v1", "v2"):
            with tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / "contract.bin"
                lines = []
                for cp in range(4):
                    capture_checkpoint(FakeProxy(capture=version), cp, cp + 1, True, out,
                                       log=lines.append)
                records = decode_records(out.read_bytes())
                self.assertEqual([r.checkpoint for r in records], [0, 1, 2, 3])
                self.assertEqual(len(lines), 4)
                self.assertTrue(all(line.endswith("captured") for line in lines))

    def test_capture_failures_stop_the_launch_whatever_the_switch(self):
        # capture_checkpoint has no switch parameter: once the contract is in force (publish
        # returned True, which the switch cannot change for a contract image) nothing is skipped.
        for capture, error in ((None, LaunchContractError), (RecordError, RecordError),
                               (BadCommand, BadCommand), (OtherRemoteError, OtherRemoteError),
                               (b"short", Exception)):
            lines = []
            with self.subTest(capture=capture):
                with self.assertRaises(error):
                    capture_checkpoint(FakeProxy(capture=capture), 0, 1, True, None, log=lines.append)
                self.assertFalse(any("skipped" in line for line in lines))

    def test_only_a_legacy_launch_skips(self):
        lines = []
        self.assertIsNone(capture_checkpoint(FakeProxy(capture=RecordError), 2, 3, False, None,
                                             log=lines.append))
        self.assertEqual(lines, ["LAUNCH_CONTRACT checkpoint=2 skipped for legacy A/B"])


if __name__ == "__main__":
    unittest.main()
