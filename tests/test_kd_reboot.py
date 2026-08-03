import struct
import unittest

from tools.kd.kd_proclist import API_REBOOT, DATA_LEADER, KD, PKT_STATE_MANIPULATE


class FakeSerial:
    def __init__(self):
        self.writes = []

    def write(self, data):
        self.writes.append(bytes(data))


class KdRebootPacketTest(unittest.TestCase):
    def test_reboot_sends_fire_and_forget_manipulate_packet(self):
        serial = FakeSerial()
        kd = KD(serial)
        initial_id = kd.host_id

        kd.reboot()

        self.assertEqual(len(serial.writes), 1)
        packet = serial.writes[0]
        leader, packet_type, count, packet_id, _checksum = struct.unpack_from(
            "<IHHII", packet
        )
        self.assertEqual(leader, DATA_LEADER)
        self.assertEqual(packet_type, PKT_STATE_MANIPULATE)
        self.assertEqual(count, 56)
        self.assertEqual(packet_id, initial_id)
        self.assertEqual(struct.unpack_from("<I", packet, 16)[0], API_REBOOT)
        self.assertEqual(packet[-1], 0xAA)
        self.assertEqual(kd.host_id, initial_id ^ 1)


if __name__ == "__main__":
    unittest.main()
