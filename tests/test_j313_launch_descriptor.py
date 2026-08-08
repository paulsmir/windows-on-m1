import struct
import unittest

from tools.j313_launch_descriptor import (
    DESCRIPTOR_SIZE,
    J313_MPIDRS,
    pack_descriptor,
)


class J313LaunchDescriptorTests(unittest.TestCase):
    def test_packs_the_c_abi_without_host_padding(self):
        blob = pack_descriptor(
            boot=(0x850000000, 0x1B0000000, 0x8510B4000, (0x854000000, 0, 0, 0)),
            regions=[(0, 0, 0x850000000, 0x1B0000000)],
            devices=(0x690000000, 0, 0x502280000, 0x502F80000, 0x235200000,
                     0x85F000000, 2560, 1600, 10240, 0),
            adt_size=0x100000,
            adt_digest=bytes(range(32)),
        )
        self.assertEqual(len(blob), DESCRIPTOR_SIZE)
        self.assertEqual(struct.unpack_from("<II", blob, 0), (0x3331334A, 1))
        self.assertEqual(struct.unpack_from("<II", blob, 112), (1, 8))
        self.assertEqual(struct.unpack_from("<8Q", blob, 504), J313_MPIDRS)

    def test_rejects_invalid_capacities_and_digest(self):
        common = dict(
            boot=(1, 2, 3, (4, 5, 6, 7)),
            devices=(0,) * 10,
            adt_size=0,
            adt_digest=b"\0" * 32,
        )
        with self.assertRaisesRegex(ValueError, "at most 16 regions"):
            pack_descriptor(regions=[(0, 0, 0, 0)] * 17, **common)
        with self.assertRaisesRegex(ValueError, "32 bytes"):
            pack_descriptor(regions=[], **{**common, "adt_digest": b"short"})


if __name__ == "__main__":
    unittest.main()
