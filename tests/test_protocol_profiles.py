"""Strict source-profile protocol tests; fixtures stay inside rep_player."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get("REP_VALIDATE_EXE", ROOT / "build" / "rep_validate.exe"))


def pack_rep(version=1.7, commands=None, resources=(), scenes=(), minor=6, version_bits=None):
    version_data = struct.pack("<I", version_bits) if version_bits is not None else struct.pack("<f", version)
    if version >= 1.4:
        header = b"\x0c" + struct.pack("<H", minor) + bytes(126)
    else:
        header = b""
    commands = commands or {}
    dictionary = struct.pack("<I", len(commands))
    previous, largest = b"", -1
    for key, raw in sorted(commands.items(), key=lambda item: len(item[1])):
        size = len(raw)
        if size > largest:
            encoded, largest = raw, size
        else:
            if len(previous) < size:
                raise ValueError("fixture XOR predecessor shorter than command")
            encoded = bytes(a ^ b for a, b in zip(raw, previous))
        dictionary += struct.pack("<I", key) + struct.pack("<H" if version >= 1.5 else "<I", size) + encoded
        previous = raw
    dictionary += struct.pack("<I", len(resources))
    for resource in resources:
        if version >= 1.5:
            dictionary += bytes([resource is not None])
            if resource is None:
                continue
        raw = resource if isinstance(resource, bytes) else (resource or "").encode("ascii")
        dictionary += struct.pack("<H" if version >= 1.5 else "<I", len(raw)) + raw + b"\0"
    timeline = b"".join(struct.pack("<iI", stamp, 4 * len(ids)) + struct.pack("<" + "I" * len(ids), *ids)
                        + struct.pack("<I", len(aux)) + aux for stamp, ids, aux in scenes)
    payload = b"\1" + version_data + b"\0" + struct.pack("<I", len(header)) + header
    for chunk in (timeline, dictionary):
        encoded = zlib.compress(chunk)
        payload += struct.pack("<I", len(encoded)) + encoded
    return struct.pack("<II", zlib.crc32(payload), len(payload)) + payload


class ProtocolProfileTests(unittest.TestCase):
    def invoke(self, data, *flags):
        self.assertTrue(EXE.is_file(), "native validator must be built before protocol-profile tests")
        destination = ROOT / "validation" / "protocol_profiles"
        destination.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination) as folder:
            fixture = Path(folder) / "fixture.rep"
            fixture.write_bytes(data)
            return subprocess.run([str(EXE), "--dump", *flags, str(fixture)],
                                  capture_output=True, text=True, encoding="utf-8")

    def parsed(self, data, *flags):
        result = self.invoke(data, *flags)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def failed(self, data, *flags):
        result = self.invoke(data, *flags)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        return result.stderr

    def test_registered_18_preserves_real_float_bits_and_grid40(self):
        params = struct.pack("<iI6f", 2, 3, 4., 5., 120., 80., 0., 1.) + struct.pack("<2f", .75, 1.25)
        command = struct.pack("<II", 57, 0) + params + struct.pack("<QQII", 0, 0, 0, 0)
        row = self.parsed(pack_rep(1.8, {7: command}, resources=["grid.img"], scenes=[(0, [7], b"")]))
        self.assertEqual(row["version"], 1.8)
        self.assertEqual(row["version_bits"], "0x3fe66666")
        instruction = row["dictionary"][0]["instructions"][0]
        self.assertEqual(instruction["payload_bytes"], 68)
        self.assertEqual(instruction["native_hex"], params.hex())
        self.assertTrue(row["exact_eof"])

    def test_default_profile_retains_legacy_dfo_codepage949(self):
        row = self.parsed(pack_rep(commands={7: struct.pack("<I", 22)}))
        self.assertIn("profile", row)
        self.assertEqual(row["profile"], "dfo")
        self.assertEqual(row["resource_codepage"], 949)
        self.assertEqual(row["dictionary"][0]["instructions"][0]["payload_bytes"], 0)

    def test_cn22_fixed_word_is_preserved_instead_of_becoming_legal_opcode1(self):
        row = self.parsed(pack_rep(1.4, {7: struct.pack("<II", 22, 1)}, scenes=[(0, [7], b"")]), "--profile", "dnf-july")
        instructions = row["dictionary"][0]["instructions"]
        self.assertEqual(len(instructions), 1)
        self.assertEqual(instructions[0]["payload_bytes"], 4)
        self.assertEqual(instructions[0]["native_hex"], "01000000")
        self.assertEqual(row["profile"], "dnf-july")
        self.assertEqual(row["resource_codepage"], 0)

    def test_cn22_missing_fixed_word_is_not_accepted(self):
        error = self.failed(pack_rep(commands={7: struct.pack("<I", 22)}), "--profile", "dnf-july")
        self.assertIn("command 7", error)
        self.assertIn("opcode 22", error)

    def test_dfo22_does_not_swallow_unregistered_next_opcode(self):
        error = self.failed(pack_rep(commands={7: struct.pack("<II", 22, 53551)}), "--profile", "dfo")
        self.assertIn("opcode 53551", error)
        self.assertIn("byte 4", error)

    def test_cn25_retains_both_words_and_first_resource_reference(self):
        row = self.parsed(pack_rep(commands={7: struct.pack("<III", 25, 0, 53551)}, resources=["lookup.img"]), "--profile", "dnf-july")
        instruction = row["dictionary"][0]["instructions"][0]
        self.assertEqual(instruction["payload_bytes"], 8)
        self.assertEqual(instruction["native_hex"], "000000002fd10000")
        self.assertEqual(instruction["resource_id"], 0)

    def test_context_markers_are_selected_by_explicit_profile(self):
        data = pack_rep(commands={7: struct.pack("<3I", 38, 39, 40)})
        for profile, expected in (("dfo", False), ("dnf-july", True)):
            row = self.parsed(data, "--profile", profile)
            instructions = row["dictionary"][0]["instructions"]
            self.assertEqual([item["native_context_state"] for item in instructions], [expected] * 3)
            self.assertEqual([item["payload_bytes"] for item in instructions], [0] * 3)
            self.assertEqual([item["aux_bytes"] for item in instructions], [0] * 3)

    def test_structure_mode_preserves_non_ascii_bytes_without_forced_decode(self):
        data = pack_rep(resources=[b"\x81", b"a\0tail"])
        row = self.parsed(data, "--profile", "dfo", "--structural")
        self.assertFalse(row["resource_strings_decoded"])
        self.assertEqual(row["resource_bytes_hex"], ["81", "61007461696c"])
        self.assertTrue(row["exact_eof"])

    def test_explicit_codepage_decodes_utf8_and_preserves_original_resource_bytes(self):
        raw = "魔法.img".encode("utf-8")
        row = self.parsed(pack_rep(resources=[raw]), "--profile", "dnf-july", "--codepage", "65001")
        self.assertEqual(row["resource_codepage"], 65001)
        self.assertEqual(row["resources"], {"0": "魔法.img"})
        self.assertEqual(row["resource_bytes_hex"], [raw.hex()])

    def test_bad_explicit_utf8_is_rejected_without_replacement(self):
        error = self.failed(pack_rep(resources=[b"\x81"]), "--codepage", "65001")
        self.assertIn("resource string", error)
        self.assertIn("65001", error)

    def test_nearby_unregistered_float32_bits_do_not_match_registered_version(self):
        for bits in (0x3FD9999B, 0x3FE66667):
            error = self.failed(pack_rep(version_bits=bits))
            self.assertIn("unsupported REP version", error)

    def test_unknown66_and_movie48_fail_with_command_opcode_and_byte_context(self):
        cases = ((struct.pack("<IB", 66, 0), 66), (struct.pack("<I", 23) + bytes(48), 23))
        for raw, opcode in cases:
            error = self.failed(pack_rep(commands={18: raw}), "--profile", "dnf-july")
            self.assertIn("command 18", error)
            self.assertIn(f"opcode {opcode}", error)
            self.assertIn("byte 0", error)

    def test_source_profile_keeps_scene_aux_contract_and_duplicate_timestamp_order(self):
        command = struct.pack("<II", 22, 1) + struct.pack("<II", 3, 4) + struct.pack("<I", 0)
        data = pack_rep(commands={7: command}, resources=["draw.img"],
                        scenes=[(7, [7, 999, 7], struct.pack("<4h", 1, 2, 3, 4)), (7, [], b""), (-1, [], b"")])
        row = self.parsed(data, "--profile", "dnf-july")
        self.assertEqual((row["scenes"], row["references"], row["aux_bytes"]), (3, 3, 8))
        self.assertEqual(row["timestamps"], [7, 7, -1])
        self.assertEqual(row["opcode_counts"], {"3": 2, "22": 2})


if __name__ == "__main__":
    unittest.main()
