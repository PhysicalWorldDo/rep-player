"""Bounded opcode 66 compatibility; every fixture/output stays in rep_player."""
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest
import uuid
import zlib

from test_protocol_profiles import pack_rep

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get("REP_VALIDATE_EXE", ROOT / "build" / "rep_validate.exe"))
EXACT_66 = bytes.fromhex("4200000000")
COMPATIBLE = "dnf-compatible"


class Opcode66CompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / "validation" / "opcode66_20261004" / ("compatibility_structural_" + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        cls.counter = 0

    def invoke(self, data, profile=COMPATIBLE, *options):
        self.assertTrue(EXE.is_file(), "Build the native REP validator before compatibility tests")
        type(self).counter += 1
        fixture = self.folder / f"{self.counter:03d}_{self._testMethodName}.rep"
        fixture.write_bytes(data)
        argv = [str(EXE), "--dump", "--profile", profile, *options, str(fixture)]
        result = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8", timeout=20)
        record = {"test": self._testMethodName, "argv": argv, "returncode": result.returncode,
                  "stdout": result.stdout, "stderr": result.stderr}
        with (self.folder / "invocations.jsonl").open("a", encoding="utf-8") as output:
            output.write(json.dumps(record, ensure_ascii=False) + "\n")
        return result

    def parsed(self, data, profile=COMPATIBLE, *options):
        result = self.invoke(data, profile, *options)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def rejected(self, data, profile=COMPATIBLE, *options):
        result = self.invoke(data, profile, *options)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        return result.stderr

    def assert_unknown_66(self, data, profile=COMPATIBLE, offset=0):
        error = self.rejected(data, profile)
        self.assertIn("command 18", error)
        self.assertIn("opcode 66", error)
        self.assertIn(f"byte {offset}", error)

    def test_exact_standalone_shape_is_counted_without_scene_aux(self):
        data = pack_rep(1.8, {18: EXACT_66, 21: struct.pack("<I", 17)},
                        scenes=[(0, [18, 21, 18], b""), (7, [21, 18], b""), (7, [], b"")], minor=6)
        row = self.parsed(data)
        self.assertEqual((row["version"], row["minor"], row["profile"]), (1.8, 6, COMPATIBLE))
        self.assertEqual(row["resource_codepage"], 0)
        self.assertEqual((row["scenes"], row["references"], row["aux_bytes"]), (3, 5, 0))
        self.assertEqual(row["timestamps"], [0, 7, 7])
        self.assertTrue(row["exact_eof"])
        self.assertEqual(row["dictionary_opcode_counts"], {"17": 1, "66": 1})
        self.assertEqual(row["opcode_counts"], {"17": 2, "66": 3})
        self.assertEqual(row["compatibility_ignored_commands"], 1)
        self.assertEqual(row["compatibility_ignored_references"], 3)
        commands = {entry["id"]: entry for entry in row["dictionary"]}
        self.assertEqual(commands[18]["raw_crc32"], zlib.crc32(EXACT_66))
        self.assertEqual(len(commands[18]["instructions"]), 1)
        instruction = commands[18]["instructions"][0]
        self.assertEqual((instruction["opcode"], instruction["payload_bytes"], instruction["aux_bytes"]), (66, 1, 0))
        self.assertEqual(instruction["native_hex"], "00")
        self.assertTrue(instruction["compatibility_ignored"])
        self.assertFalse(instruction["native_context_state"])
        self.assertEqual(instruction["resource_id"], -1)
        self.assertFalse(commands[21]["instructions"][0]["compatibility_ignored"])

    def test_separate_following_draw_preserves_aux_boundary(self):
        draw = struct.pack("<III", 3, 4, 0)
        aux = struct.pack("<4h", 12, -4, 20, 8)
        data = pack_rep(1.8, {18: EXACT_66, 21: draw}, resources=["sprite/a.img"],
                        scenes=[(0, [18, 21, 18, 21], aux)], minor=6)
        row = self.parsed(data)
        self.assertEqual((row["scenes"], row["references"], row["aux_bytes"]), (1, 4, 8))
        self.assertEqual(row["opcode_counts"], {"3": 2, "66": 2})
        self.assertEqual(row["compatibility_ignored_references"], 2)
        self.assertTrue(row["exact_eof"])

    def test_strict_profiles_keep_rejecting_the_exact_record(self):
        data = pack_rep(1.8, {18: EXACT_66}, scenes=[(0, [18], b"")], minor=6)
        for profile in ("dfo", "dnf-july"):
            with self.subTest(profile=profile):
                self.assert_unknown_66(data, profile)

    def test_other_lengths_and_values_are_not_generalized(self):
        cases = {"missing_byte": EXACT_66[:4], "extra_byte": EXACT_66 + b"\0",
                 "value_one": EXACT_66[:4] + b"\1", "value_255": EXACT_66[:4] + b"\xff"}
        for label, raw in cases.items():
            with self.subTest(case=label):
                self.assert_unknown_66(pack_rep(1.8, {18: raw}, minor=6))

    def test_old_versions_remain_strict(self):
        for version in (1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7):
            with self.subTest(version=version):
                self.assert_unknown_66(pack_rep(version, {18: EXACT_66}, minor=6))

    def test_other_extension_minors_remain_strict(self):
        for minor in (5, 7):
            with self.subTest(minor=minor):
                self.assert_unknown_66(pack_rep(1.8, {18: EXACT_66}, minor=minor))

    def test_embedded_opcode66_is_never_skipped(self):
        noop = struct.pack("<I", 17)
        for raw, offset in ((EXACT_66 + noop, 0), (noop + EXACT_66, 4), (EXACT_66 + EXACT_66, 0)):
            with self.subTest(raw_hex=raw.hex()):
                self.assert_unknown_66(pack_rep(1.8, {18: raw}, minor=6), offset=offset)

    def test_known_dnf_profile_instructions_keep_their_original_contract(self):
        known = struct.pack("<II", 22, 1) + struct.pack("<III", 25, 0, 53551)
        known += struct.pack("<4I", 38, 39, 40, 17)
        data = pack_rep(1.8, {21: known}, resources=["lookup.img"], scenes=[(0, [21], b"")], minor=6)
        row = self.parsed(data)
        strict = self.parsed(data, "dnf-july")
        instructions = row["dictionary"][0]["instructions"]
        self.assertEqual(instructions, strict["dictionary"][0]["instructions"])
        self.assertEqual([item["opcode"] for item in instructions], [22, 25, 38, 39, 40, 17])
        self.assertEqual([item["payload_bytes"] for item in instructions], [4, 8, 0, 0, 0, 0])
        self.assertEqual(instructions[0]["native_hex"], "01000000")
        self.assertEqual(instructions[1]["native_hex"], "000000002fd10000")
        self.assertEqual(instructions[1]["resource_id"], 0)
        self.assertEqual([item["native_context_state"] for item in instructions], [False, False, True, True, True, False])
        self.assertTrue(all(not item["compatibility_ignored"] for item in instructions))
        self.assertEqual((row["compatibility_ignored_commands"], row["compatibility_ignored_references"]), (0, 0))
        self.assertEqual(row["aux_bytes"], 0)
        self.assertTrue(row["exact_eof"])

    def test_aux_mismatch_is_still_rejected(self):
        draw = struct.pack("<II", 3, 0)
        for ids, aux in (([18], b"\0"), ([18, 21], b""), ([18, 21], bytes(8))):
            with self.subTest(ids=ids, aux_bytes=len(aux)):
                data = pack_rep(1.8, {18: EXACT_66, 21: draw}, scenes=[(0, ids, aux)], minor=6)
                self.assertIn("scene auxiliary stream length mismatch", self.rejected(data))

    def test_timeline_trailing_zlib_bytes_are_still_rejected(self):
        data = pack_rep(1.8, {18: EXACT_66}, scenes=[(0, [18], b"")], minor=6)
        payload = bytearray(data[8:])
        header_bytes = struct.unpack_from("<I", payload, 6)[0]
        timeline_size_offset = 10 + header_bytes
        timeline_bytes = struct.unpack_from("<I", payload, timeline_size_offset)[0]
        timeline_end = timeline_size_offset + 4 + timeline_bytes
        payload[timeline_end:timeline_end] = b"\0"
        struct.pack_into("<I", payload, timeline_size_offset, timeline_bytes + 1)
        bad = struct.pack("<II", zlib.crc32(payload), len(payload)) + payload
        self.assertIn("trailing bytes after zlib stream", self.rejected(bad))


if __name__ == "__main__":
    unittest.main()
