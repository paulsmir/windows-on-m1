import dataclasses
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

from tests.test_agx_frame_fixture import (
    EXPECTED_OUTPUT,
    IDENTITY,
    OBJECT_SIZE,
    OUTPUT_ADDR,
    _base_members,
    _json_bytes,
)


PROGRAM = b"fixed AGX clear producer\n"
PROGRAM_HASH = hashlib.sha256(PROGRAM).hexdigest()


def _write_capture(path, members, *, reverse=False, minute=0):
    names = sorted(members, reverse=reverse)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in names:
            info = zipfile.ZipInfo(name, (2026, 8, 25, 12, minute, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.comment = b"ignored capture metadata"
            archive.writestr(info, members[name])


class CaptureCase(unittest.TestCase):
    def setUp(self):
        from tools.agx_capture_clear import CaptureInput

        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.members = _base_members()
        self.first_frame = self.root / "first.zip"
        self.second_frame = self.root / "second.zip"
        self.first_output = self.root / "first.rgba"
        self.second_output = self.root / "second.rgba"
        self.program = self.root / "clear-producer"
        self.destination = self.root / "fixture"
        _write_capture(self.first_frame, self.members, minute=0)
        _write_capture(self.second_frame, self.members, reverse=True, minute=2)
        self.first_output.write_bytes(EXPECTED_OUTPUT)
        self.second_output.write_bytes(EXPECTED_OUTPUT)
        self.program.write_bytes(PROGRAM)
        self.first = CaptureInput(
            frame_path=self.first_frame,
            final_attachment_path=self.first_output,
            identity=dict(IDENTITY),
            capture_program_sha256=PROGRAM_HASH,
            proxy_identity="proxy-cold-boot-a",
            m1n1_base=0x800000000,
        )
        self.second = CaptureInput(
            frame_path=self.second_frame,
            final_attachment_path=self.second_output,
            identity=dict(IDENTITY),
            capture_program_sha256=PROGRAM_HASH,
            proxy_identity="proxy-cold-boot-b",
            m1n1_base=0x810000000,
        )

    def tearDown(self):
        self.tmp.cleanup()

    def _rewrite_second(self, members):
        _write_capture(self.second_frame, members, reverse=True, minute=2)

    def _compare(self):
        from tools.agx_capture_clear import compare_captures

        return compare_captures(self.first, self.second)


class ReproducibilityTests(CaptureCase):
    def test_live_receipt_accepts_the_exact_complete_raw_bgra_page(self):
        from tools.agx_capture_clear import write_capture_receipt

        output = self.root / "receipt.json"
        receipt = write_capture_receipt(
            output,
            frame_path=self.first_frame,
            final_attachment_path=self.first_output,
            identity=self.first.identity,
            capture_program=self.program,
            proxy_identity=self.first.proxy_identity,
            m1n1_base=self.first.m1n1_base,
        )
        self.assertEqual(receipt, json.loads(output.read_text()))

    def test_live_receipt_rejects_only_the_visible_rgba_pixels(self):
        from tools.agx_capture_clear import write_capture_receipt

        self.first_output.write_bytes(bytes([0x11, 0x22, 0x33, 0xFF]) * 256)
        with self.assertRaisesRegex(Exception, "complete raw BGRA"):
            write_capture_receipt(
                self.root / "receipt.json",
                frame_path=self.first_frame,
                final_attachment_path=self.first_output,
                identity=self.first.identity,
                capture_program=self.program,
                proxy_identity=self.first.proxy_identity,
                m1n1_base=self.first.m1n1_base,
            )

    def test_omitted_zero_objects_are_materialized_canonically(self):
        from tools.agx_capture_clear import package_capture
        from tools.agx_frame_fixture import validate_fixture

        members = dict(self.members)
        objects = json.loads(members["objects.json"])
        omitted = objects[0]
        del members[omitted["file"]]
        omitted["file"] = None
        members["objects.json"] = _json_bytes(objects)
        _write_capture(self.first_frame, members, minute=0)
        _write_capture(self.second_frame, members, reverse=True, minute=2)

        frame, manifest = package_capture(
            self.first,
            self.second,
            capture_program=self.program,
            destination=self.destination,
        )
        validated = validate_fixture(frame, manifest, IDENTITY)
        materialized = next(
            item for item in validated.objects if item.gpu_va == omitted["addr"]
        )
        self.assertEqual(materialized.data, bytes(omitted["size"]))

    def test_non_null_missing_object_member_remains_rejected(self):
        members = dict(self.members)
        objects = json.loads(members["objects.json"])
        del members[objects[0]["file"]]
        members["objects.json"] = _json_bytes(objects)
        _write_capture(self.first_frame, members, minute=0)
        _write_capture(self.second_frame, members, reverse=True, minute=2)

        with self.assertRaisesRegex(Exception, "object member is missing"):
            self._compare()

    def test_omitted_zero_object_requires_valid_size(self):
        members = dict(self.members)
        objects = json.loads(members["objects.json"])
        del members[objects[0]["file"]]
        objects[0]["file"] = None
        objects[0]["size"] = 0
        members["objects.json"] = _json_bytes(objects)
        _write_capture(self.first_frame, members, minute=0)
        _write_capture(self.second_frame, members, reverse=True, minute=2)

        with self.assertRaisesRegex(Exception, "zero object size"):
            self._compare()

    def test_independent_zip_metadata_produces_one_identical_fixture(self):
        from tools.agx_capture_clear import package_capture
        from tools.agx_frame_fixture import validate_fixture

        frame, manifest = package_capture(
            self.first,
            self.second,
            capture_program=self.program,
            destination=self.destination,
        )
        self.assertEqual(frame, self.destination / "frame.agx")
        self.assertEqual(manifest, self.destination / "manifest.json")
        self.assertEqual(
            set(path.name for path in self.destination.iterdir()),
            {"frame.agx", "manifest.json", "provenance.json"},
        )
        validated = validate_fixture(frame, manifest, IDENTITY)
        self.assertEqual(validated.output_gpu_va, OUTPUT_ADDR)
        provenance = json.loads((self.destination / "provenance.json").read_text())
        self.assertEqual(provenance["cold_boots"][0]["proxy_identity"], "proxy-cold-boot-a")
        self.assertEqual(provenance["cold_boots"][1]["proxy_identity"], "proxy-cold-boot-b")

    def test_packaging_is_byte_reproducible(self):
        from tools.agx_capture_clear import package_capture

        first_destination = self.root / "fixture-a"
        second_destination = self.root / "fixture-b"
        package_capture(
            self.first, self.second,
            capture_program=self.program,
            destination=first_destination,
        )
        package_capture(
            self.first, self.second,
            capture_program=self.program,
            destination=second_destination,
        )
        for name in ("frame.agx", "manifest.json", "provenance.json"):
            self.assertEqual(
                (first_destination / name).read_bytes(),
                (second_destination / name).read_bytes(),
            )

    def test_same_proxy_identity_is_rejected(self):
        self.second = dataclasses.replace(
            self.second, proxy_identity=self.first.proxy_identity
        )
        with self.assertRaisesRegex(Exception, "proxy identity"):
            self._compare()

    def test_same_m1n1_base_is_rejected(self):
        self.second = dataclasses.replace(
            self.second, m1n1_base=self.first.m1n1_base
        )
        with self.assertRaisesRegex(Exception, "m1n1 base"):
            self._compare()

    def test_source_identity_change_is_rejected(self):
        changed = dict(IDENTITY)
        changed["mesa_commit"] = "c" * 40
        self.second = dataclasses.replace(self.second, identity=changed)
        with self.assertRaisesRegex(Exception, "source identity"):
            self._compare()

    def test_capture_program_change_is_rejected(self):
        self.second = dataclasses.replace(
            self.second, capture_program_sha256="d" * 64
        )
        with self.assertRaisesRegex(Exception, "capture program"):
            self._compare()

    def test_command_buffer_change_is_rejected(self):
        members = dict(self.members)
        command = json.loads(members["cmdbuf.json"])
        command["encoder_id"] += 1
        members["cmdbuf.json"] = _json_bytes(command)
        self._rewrite_second(members)
        with self.assertRaisesRegex(Exception, "command buffer"):
            self._compare()

    def test_object_address_change_is_rejected(self):
        members = dict(self.members)
        objects = json.loads(members["objects.json"])
        objects[0]["addr"] += OBJECT_SIZE
        members["objects.json"] = _json_bytes(objects)
        self._rewrite_second(members)
        with self.assertRaisesRegex(Exception, "object address"):
            self._compare()

    def test_map_flag_change_is_rejected(self):
        members = dict(self.members)
        objects = json.loads(members["objects.json"])
        objects[0]["map_flags"]["AP"] = 0
        members["objects.json"] = _json_bytes(objects)
        self._rewrite_second(members)
        with self.assertRaisesRegex(Exception, "map flags"):
            self._compare()

    def test_object_byte_change_is_rejected(self):
        members = dict(self.members)
        name = next(name for name in members if name.startswith("obj_"))
        members[name] = bytes([members[name][0] ^ 1]) + members[name][1:]
        self._rewrite_second(members)
        with self.assertRaisesRegex(Exception, "object bytes"):
            self._compare()

    def test_final_attachment_change_is_rejected(self):
        changed = bytearray(EXPECTED_OUTPUT)
        changed[-1] ^= 1
        self.second_output.write_bytes(changed)
        with self.assertRaisesRegex(Exception, "final attachment"):
            self._compare()

    def test_program_bytes_must_match_both_capture_receipts(self):
        from tools.agx_capture_clear import package_capture

        self.program.write_bytes(PROGRAM + b"changed")
        with self.assertRaisesRegex(Exception, "capture program"):
            package_capture(
                self.first, self.second,
                capture_program=self.program,
                destination=self.destination,
            )

    def test_destination_must_be_initially_empty(self):
        from tools.agx_capture_clear import package_capture

        self.destination.mkdir()
        (self.destination / "old").write_text("stale")
        with self.assertRaisesRegex(Exception, "destination.*empty"):
            package_capture(
                self.first, self.second,
                capture_program=self.program,
                destination=self.destination,
            )

    def test_partial_write_failure_leaves_no_accepted_fixture(self):
        import tools.agx_capture_clear as capture

        real_write = capture._write_temporary
        writes = 0

        def fail_second(path, data):
            nonlocal writes
            writes += 1
            if writes == 2:
                raise OSError("injected write failure")
            return real_write(path, data)

        with mock.patch.object(capture, "_write_temporary", side_effect=fail_second):
            with self.assertRaisesRegex(Exception, "injected write failure"):
                capture.package_capture(
                    self.first, self.second,
                    capture_program=self.program,
                    destination=self.destination,
                )
        self.assertFalse((self.destination / "frame.agx").exists())
        self.assertFalse((self.destination / "manifest.json").exists())
        self.assertFalse((self.destination / "provenance.json").exists())

    def test_package_two_cli_uses_explicit_receipts(self):
        first_receipt = self.root / "first.json"
        second_receipt = self.root / "second.json"
        for receipt, capture in (
            (first_receipt, self.first),
            (second_receipt, self.second),
        ):
            receipt.write_text(json.dumps({
                "frame_path": str(capture.frame_path),
                "final_attachment_path": str(capture.final_attachment_path),
                "identity": capture.identity,
                "capture_program_sha256": capture.capture_program_sha256,
                "proxy_identity": capture.proxy_identity,
                "m1n1_base": capture.m1n1_base,
            }))
        result = subprocess.run(
            [
                sys.executable, "-m", "tools.agx_capture_clear", "package-two",
                "--first-receipt", str(first_receipt),
                "--second-receipt", str(second_receipt),
                "--capture-program", str(self.program),
                "--destination", str(self.destination),
            ],
            cwd=Path(__file__).resolve().parents[1],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        summary = json.loads(result.stdout)
        self.assertEqual(summary["fixture"], str(self.destination / "frame.agx"))
        self.assertEqual(summary["manifest"], str(self.destination / "manifest.json"))


class CaptureProgramTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def _fake_program(self, payload):
        program = self.root / "fake-clear"
        program.write_text(
            "#!/bin/sh\n"
            "test \"$#\" -eq 1 || exit 64\n"
            f"printf '{payload}' > \"$1\"\n"
        )
        program.chmod(program.stat().st_mode | stat.S_IXUSR)
        return program

    def test_deterministic_program_boundary_accepts_exact_clear(self):
        from tools.agx_capture_clear import run_capture_program

        payload = "\\021\\042\\063\\377" * 256
        output = run_capture_program(self._fake_program(payload), self.root / "out.rgba")
        self.assertEqual(output.read_bytes(), bytes([0x11, 0x22, 0x33, 0xFF]) * 256)

    def test_program_boundary_rejects_wrong_output_size(self):
        from tools.agx_capture_clear import CaptureError, run_capture_program

        with self.assertRaisesRegex(CaptureError, "1024-byte"):
            run_capture_program(self._fake_program("x"), self.root / "out.rgba")

    def test_requested_real_capture_environment_never_silently_skips(self):
        from tools.agx_capture_clear import CaptureError, capture_program_from_environment

        with mock.patch.dict(os.environ, {"AGX_CAPTURE_PROGRAM": str(self.root / "missing")}):
            with self.assertRaisesRegex(CaptureError, "AGX_CAPTURE_PROGRAM"):
                capture_program_from_environment()

    def test_c_source_contains_the_fixed_clear_contract(self):
        source = (
            Path(__file__).resolve().parents[1] / "tools" / "agx_clear_capture.c"
        ).read_text()
        for token in (
            "EGL_PLATFORM_SURFACELESS_MESA",
            "glViewport(0, 0, 16, 16)",
            "glDisable(GL_SCISSOR_TEST)",
            "glDisable(GL_DEPTH_TEST)",
            "glDisable(GL_STENCIL_TEST)",
            "glDisable(GL_BLEND)",
            "glDisable(GL_DITHER)",
            "glClearColor(17.0f / 255.0f, 34.0f / 255.0f, 51.0f / 255.0f, 1.0f)",
            "glClear(GL_COLOR_BUFFER_BIT)",
            "glFinish()",
            "glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE",
        ):
            self.assertIn(token, source)


if __name__ == "__main__":
    unittest.main()
