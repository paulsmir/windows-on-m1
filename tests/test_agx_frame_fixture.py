import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import warnings
import zipfile


IDENTITY = {
    "board": "J313",
    "chip_generation": "G13",
    "firmware_version": "V13_5",
    "m1n1_commit": "9cd80ac652ac404e92ae279deeaec8c629d7d184",
    "mesa_commit": "a" * 40,
    "adt_sha256": "c57d4c0db26125394409c3b5b518fdef553d8f4dfe2263ae9303e2276b0796a3",
}

ZIP_TIME = (1980, 1, 1, 0, 0, 0)
PIPELINE_ADDR = 0x1100010000
OUTPUT_ADDR = 0x1500000000
OBJECT_SIZE = 0x4000
POISON = bytes([0xA5]) * OBJECT_SIZE
EXPECTED_OUTPUT = bytes([0x11, 0x22, 0x33, 0xFF]) * (OBJECT_SIZE // 4)


def _json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _base_members():
    cmdbuf = {
        "flags": 1,
        "encoder_ptr": PIPELINE_ADDR,
        "encoder_id": 7,
        "cmd_ta_id": 8,
        "cmd_3d_id": 9,
        "ds_flags": 0,
        "depth_buffer": 0,
        "stencil_buffer": 0,
        "fb_width": 16,
        "fb_height": 16,
        "attachments": [
            {"type": 0, "size": OBJECT_SIZE, "pointer": OUTPUT_ADDR},
        ],
        "attachment_count": 1,
    }
    objects = [
        {
            "file": f"obj_{PIPELINE_ADDR:x}.bin",
            "name": "Encoder",
            "addr": PIPELINE_ADDR,
            "size": OBJECT_SIZE,
            "map_flags": {"AP": 1, "AttrIndex": 1},
        },
        {
            "file": f"obj_{OUTPUT_ADDR:x}.bin",
            "name": "Color",
            "addr": OUTPUT_ADDR,
            "size": OBJECT_SIZE,
            "map_flags": {"AP": 0, "AttrIndex": 1},
        },
    ]
    return {
        "cmdbuf.json": _json_bytes(cmdbuf),
        "objects.json": _json_bytes(objects),
        f"obj_{PIPELINE_ADDR:x}.bin": bytes([0x5A]) * OBJECT_SIZE,
        f"obj_{OUTPUT_ADDR:x}.bin": POISON,
    }


def _write_zip(path, members, *, order=None, duplicate=None):
    order = list(order or members)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in order:
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, members[name])
        if duplicate is not None:
            info = zipfile.ZipInfo(duplicate, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                archive.writestr(info, members[duplicate])


def _canonical_zip_sha256(members):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "canonical.agx"
        _write_zip(path, members, order=sorted(members))
        return hashlib.sha256(path.read_bytes()).hexdigest()


def _manifest_for(members):
    objects = json.loads(members["objects.json"])
    return {
        "fixture_version": 1,
        "identity": dict(IDENTITY),
        "capture_program_sha256": "b" * 64,
        "fixture_sha256": _canonical_zip_sha256(members),
        "members": {
            name: {
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            for name, data in sorted(members.items())
        },
        "objects": [
            {
                "member": item["file"],
                "name": item["name"],
                "gpu_va": item["addr"],
                "size": item["size"],
                "map_flags": item["map_flags"],
                "sha256": hashlib.sha256(members[item["file"]]).hexdigest(),
            }
            for item in objects
        ],
        "command_buffer_sha256": hashlib.sha256(
            members["cmdbuf.json"]
        ).hexdigest(),
        "output": {
            "gpu_va": OUTPUT_ADDR,
            "size": OBJECT_SIZE,
            "width": 16,
            "height": 16,
            "format": "RGBA8",
            "poison_sha256": hashlib.sha256(POISON).hexdigest(),
            "expected_output_sha256": hashlib.sha256(EXPECTED_OUTPUT).hexdigest(),
        },
    }


class FixtureCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.members = _base_members()
        self.frame = self.root / "frame.agx"
        self.manifest = self.root / "manifest.json"
        self.install_members(self.members)

    def tearDown(self):
        self.tmp.cleanup()

    def install_members(self, members, mutate_manifest=None):
        self.members = members
        _write_zip(self.frame, members)
        manifest = _manifest_for(members)
        if mutate_manifest is not None:
            mutate_manifest(manifest)
        self.manifest.write_text(json.dumps(manifest))

    def mutate_manifest(self, mutation):
        manifest = json.loads(self.manifest.read_text())
        mutation(manifest)
        self.manifest.write_text(json.dumps(manifest))

    def mutate_cmdbuf(self, mutation):
        members = dict(self.members)
        cmdbuf = json.loads(members["cmdbuf.json"])
        mutation(cmdbuf)
        members["cmdbuf.json"] = _json_bytes(cmdbuf)
        self.install_members(members)

    def mutate_objects(self, mutation):
        members = dict(self.members)
        objects = json.loads(members["objects.json"])
        mutation(objects)
        members["objects.json"] = _json_bytes(objects)
        self.install_members(members)


class SafeZipTests(FixtureCase):
    def _validate(self, frame=None):
        from tools.agx_frame_fixture import validate_fixture

        return validate_fixture(frame or self.frame, self.manifest, IDENTITY)

    def test_valid_fixture_is_read_without_extracting_members(self):
        validated = self._validate()
        self.assertEqual(validated.output_gpu_va, OUTPUT_ADDR)
        self.assertEqual(len(validated.objects), 2)
        self.assertEqual(list(self.root.iterdir()), [self.frame, self.manifest])

    def test_parent_path_member_is_rejected_before_manifest_validation(self):
        members = dict(self.members)
        members["../escape.bin"] = b"escape"
        path = self.root / "traversal.agx"
        _write_zip(path, members)
        with self.assertRaisesRegex(Exception, "path traversal"):
            self._validate(path)

    def test_absolute_path_member_is_rejected(self):
        members = dict(self.members)
        members["/absolute.bin"] = b"escape"
        path = self.root / "absolute.agx"
        _write_zip(path, members)
        with self.assertRaisesRegex(Exception, "path traversal"):
            self._validate(path)

    def test_backslash_path_member_is_rejected(self):
        members = dict(self.members)
        members["dir\\escape.bin"] = b"escape"
        path = self.root / "backslash.agx"
        _write_zip(path, members)
        with self.assertRaisesRegex(Exception, "path traversal"):
            self._validate(path)

    def test_duplicate_member_is_rejected(self):
        path = self.root / "duplicate.agx"
        _write_zip(path, self.members, duplicate="cmdbuf.json")
        with self.assertRaisesRegex(Exception, "duplicate member"):
            self._validate(path)

    def test_unlisted_member_is_rejected(self):
        members = dict(self.members)
        members["extra.bin"] = b"extra"
        path = self.root / "extra.agx"
        _write_zip(path, members)
        with self.assertRaisesRegex(Exception, "unlisted member"):
            self._validate(path)

    def test_missing_member_is_rejected(self):
        members = dict(self.members)
        del members[f"obj_{PIPELINE_ADDR:x}.bin"]
        path = self.root / "missing.agx"
        _write_zip(path, members)
        with self.assertRaisesRegex(Exception, "missing member"):
            self._validate(path)

    def test_changed_member_byte_is_rejected(self):
        members = dict(self.members)
        members[f"obj_{PIPELINE_ADDR:x}.bin"] = b"x" + members[
            f"obj_{PIPELINE_ADDR:x}.bin"
        ][1:]
        path = self.root / "changed.agx"
        _write_zip(path, members)
        with self.assertRaisesRegex(Exception, "member hash"):
            self._validate(path)

    def test_member_over_sixteen_mib_is_rejected(self):
        members = dict(self.members)
        members["huge.bin"] = bytes(16 * 1024 * 1024 + 1)
        path = self.root / "huge.agx"
        _write_zip(path, members)
        with self.assertRaisesRegex(Exception, "compression bomb"):
            self._validate(path)

    def test_aggregate_over_sixty_four_mib_is_rejected(self):
        members = dict(self.members)
        for index in range(5):
            members[f"large-{index}.bin"] = bytes(13 * 1024 * 1024)
        path = self.root / "aggregate.agx"
        _write_zip(path, members)
        with self.assertRaisesRegex(Exception, "compression bomb"):
            self._validate(path)

    def test_malformed_json_is_rejected(self):
        members = dict(self.members)
        members["cmdbuf.json"] = b"{"
        manifest = _manifest_for(self.members)
        manifest["members"]["cmdbuf.json"] = {
            "size": 1,
            "sha256": hashlib.sha256(b"{").hexdigest(),
        }
        path = self.root / "malformed.agx"
        _write_zip(path, members)
        self.manifest.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(Exception, "malformed JSON"):
            self._validate(path)

    def test_boolean_is_not_accepted_as_member_size(self):
        manifest = _manifest_for(self.members)
        manifest["members"]["cmdbuf.json"]["size"] = True
        self.manifest.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(Exception, "member size"):
            self._validate()

    def test_canonical_output_is_independent_of_input_member_order(self):
        from tools.agx_frame_fixture import canonicalize_zip

        reverse = self.root / "reverse.agx"
        _write_zip(reverse, self.members, order=reversed(list(self.members)))
        first = self.root / "first-canonical.agx"
        second = self.root / "second-canonical.agx"
        first_hash = canonicalize_zip(self.frame, first)
        second_hash = canonicalize_zip(reverse, second)
        self.assertEqual(first_hash, second_hash)
        self.assertEqual(first.read_bytes(), second.read_bytes())


class ManifestIsolationTests(FixtureCase):
    def _validate(self):
        from tools.agx_frame_fixture import validate_fixture

        return validate_fixture(self.frame, self.manifest, IDENTITY)

    def _assert_manifest_rejected(self, mutation, boundary):
        self.mutate_manifest(mutation)
        with self.assertRaisesRegex(Exception, boundary):
            self._validate()

    def test_wrong_board_identity_is_rejected(self):
        self._assert_manifest_rejected(
            lambda manifest: manifest["identity"].__setitem__("board", "J314"),
            "identity.*board",
        )

    def test_wrong_mesa_identity_is_rejected(self):
        self._assert_manifest_rejected(
            lambda manifest: manifest["identity"].__setitem__(
                "mesa_commit", "b" * 40
            ),
            "identity.*mesa_commit",
        )

    def test_malformed_source_commit_is_rejected(self):
        self._assert_manifest_rejected(
            lambda manifest: manifest["identity"].__setitem__(
                "m1n1_commit", "9cd80ac"
            ),
            "m1n1_commit",
        )

    def test_manifest_object_address_must_match_objects_json(self):
        self._assert_manifest_rejected(
            lambda manifest: manifest["objects"][0].__setitem__(
                "gpu_va", PIPELINE_ADDR + OBJECT_SIZE
            ),
            "object.*gpu_va",
        )

    def test_manifest_map_flags_must_match_objects_json(self):
        self._assert_manifest_rejected(
            lambda manifest: manifest["objects"][0]["map_flags"].__setitem__(
                "AP", 0
            ),
            "map_flags",
        )

    def test_command_buffer_hash_must_match_member(self):
        self._assert_manifest_rejected(
            lambda manifest: manifest.__setitem__(
                "command_buffer_sha256", "c" * 64
            ),
            "command buffer hash",
        )

    def test_overlapping_objects_are_rejected(self):
        self.mutate_objects(
            lambda objects: objects[1].__setitem__("addr", PIPELINE_ADDR)
        )
        with self.assertRaisesRegex(Exception, "overlapping objects"):
            self._validate()

    def test_unaligned_object_address_is_rejected(self):
        self.mutate_objects(lambda objects: objects[0].__setitem__("addr", 1))
        with self.assertRaisesRegex(Exception, "object gpu_va.*aligned"):
            self._validate()

    def test_unaligned_object_size_is_rejected(self):
        self.mutate_objects(lambda objects: objects[0].__setitem__("size", 1))
        with self.assertRaisesRegex(Exception, "object size.*aligned"):
            self._validate()

    def test_object_outside_private_ranges_is_rejected(self):
        self.mutate_objects(lambda objects: objects[0].__setitem__("addr", 0x800000000))
        with self.assertRaisesRegex(Exception, "private GPU VA"):
            self._validate()

    def test_attachment_count_other_than_one_is_rejected(self):
        self.mutate_cmdbuf(lambda cmdbuf: cmdbuf.__setitem__("attachment_count", 0))
        with self.assertRaisesRegex(Exception, "attachment_count"):
            self._validate()

    def test_depth_attachment_is_rejected(self):
        self.mutate_cmdbuf(lambda cmdbuf: cmdbuf.__setitem__("depth_buffer", OUTPUT_ADDR))
        with self.assertRaisesRegex(Exception, "depth_buffer"):
            self._validate()

    def test_wrong_frame_dimensions_are_rejected(self):
        self.mutate_cmdbuf(lambda cmdbuf: cmdbuf.__setitem__("fb_width", 32))
        with self.assertRaisesRegex(Exception, "frame dimensions"):
            self._validate()

    def test_attachment_pointer_must_resolve_to_output_object(self):
        self.mutate_cmdbuf(
            lambda cmdbuf: cmdbuf["attachments"][0].__setitem__(
                "pointer", OUTPUT_ADDR + OBJECT_SIZE
            )
        )
        with self.assertRaisesRegex(Exception, "attachment pointer"):
            self._validate()

    def test_encoder_pointer_must_resolve_to_frame_object(self):
        self.mutate_cmdbuf(
            lambda cmdbuf: cmdbuf.__setitem__("encoder_ptr", PIPELINE_ADDR - OBJECT_SIZE)
        )
        with self.assertRaisesRegex(Exception, "encoder_ptr"):
            self._validate()

    def test_expected_output_must_differ_from_poison(self):
        self._assert_manifest_rejected(
            lambda manifest: manifest["output"].__setitem__(
                "expected_output_sha256", manifest["output"]["poison_sha256"]
            ),
            "expected output.*poison",
        )

    def test_output_manifest_must_match_attachment(self):
        self._assert_manifest_rejected(
            lambda manifest: manifest["output"].__setitem__(
                "gpu_va", OUTPUT_ADDR + OBJECT_SIZE
            ),
            "output gpu_va",
        )


class BuilderAndCliTests(FixtureCase):
    def test_builder_produces_a_manifest_accepted_by_the_validator(self):
        from tools.agx_frame_fixture import build_manifest, validate_fixture

        manifest = build_manifest(
            self.frame,
            identity=IDENTITY,
            capture_program_sha256="b" * 64,
            expected_output=EXPECTED_OUTPUT,
        )
        self.manifest.write_text(json.dumps(manifest))
        validated = validate_fixture(self.frame, self.manifest, IDENTITY)
        self.assertEqual(
            validated.expected_output_sha256,
            hashlib.sha256(EXPECTED_OUTPUT).hexdigest(),
        )

    def test_builder_rejects_a_partial_attachment_oracle(self):
        from tools.agx_frame_fixture import FixtureError, build_manifest

        with self.assertRaisesRegex(FixtureError, "expected output size"):
            build_manifest(
                self.frame,
                identity=IDENTITY,
                capture_program_sha256="b" * 64,
                expected_output=EXPECTED_OUTPUT[:1024],
            )

    def test_verify_cli_emits_only_validated_summary(self):
        identity_path = self.root / "identity.json"
        identity_path.write_text(json.dumps(IDENTITY))
        command = [
            sys.executable,
            "-m",
            "tools.agx_frame_fixture",
            "verify",
            "--frame",
            str(self.frame),
            "--manifest",
            str(self.manifest),
            "--identity",
            str(identity_path),
        ]
        result = subprocess.run(
            command,
            cwd=Path(__file__).resolve().parents[1],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        summary = json.loads(result.stdout)
        self.assertEqual(summary["fixture_sha256"], _manifest_for(self.members)["fixture_sha256"])
        self.assertEqual(summary["object_count"], 2)
        self.assertNotIn("command_buffer", summary)

    def test_verify_cli_fails_closed_for_changed_manifest(self):
        identity_path = self.root / "identity.json"
        identity_path.write_text(json.dumps(IDENTITY))
        self.mutate_manifest(
            lambda manifest: manifest["identity"].__setitem__("board", "J314")
        )
        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.agx_frame_fixture",
                "verify",
                "--frame",
                str(self.frame),
                "--manifest",
                str(self.manifest),
                "--identity",
                str(identity_path),
            ],
            cwd=Path(__file__).resolve().parents[1],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("identity board", result.stderr)

    def test_canonicalize_cli_is_atomic_and_deterministic(self):
        destination = self.root / "canonical.agx"
        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.agx_frame_fixture",
                "canonicalize",
                "--source",
                str(self.frame),
                "--destination",
                str(destination),
            ],
            cwd=Path(__file__).resolve().parents[1],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), hashlib.sha256(destination.read_bytes()).hexdigest())
        self.assertFalse(destination.with_name(destination.name + ".tmp").exists())


if __name__ == "__main__":
    unittest.main()
