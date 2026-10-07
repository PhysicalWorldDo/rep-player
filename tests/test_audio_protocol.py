"""Native audio ABI contracts; all probe products stay in rep_player."""
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
SAMPLE = Path(os.environ.get("REP_AUDIO_SAMPLE", r"C:\Users\CAO\Downloads\202610040227.rep"))


class AudioProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / "validation" / "audio_20261007" / ("protocol_" + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        cls.exe = cls.folder / "audio_protocol_probe.exe"
        compiler = ROOT / "toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe"
        environment = dict(os.environ, TEMP=str(cls.folder), TMP=str(cls.folder))
        result = subprocess.run([
            str(compiler), "-std=c++20", "-O2", "-municode", "-static", "-DNOMINMAX",
            "-I", str(ROOT / "src"), "-I", str(ROOT / "vendor/zlib"),
            str(ROOT / "tests/audio_protocol_probe.cpp"), str(ROOT / "src/protocol.cpp"),
            str(ROOT / "vendor/zlib/libz.a"), "-o", str(cls.exe),
        ], capture_output=True, text=True, encoding="utf-8", env=environment)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def parsed(self, raw, profile="dfo", version=17, minor=6):
        result = subprocess.run([str(self.exe), profile, str(version), str(minor), raw.hex()],
                                capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_request_retains_all_nine_native_words_and_resource(self):
        payload = struct.pack("<9i", 73, -2, 35, 1234, 19, -8, 2, 25, 100)
        raw = struct.pack("<II", 6, len(payload)) + payload
        report = self.parsed(raw)
        i = report["instructions"][0]
        self.assertEqual(i["resource"], 73)
        self.assertEqual(i["stored_size"], 36)
        self.assertEqual(i["native"], payload.hex())
        self.assertEqual((i["payload_bytes"], i["aux_bytes"], report["aux_bytes"]), (40, 0, 0))
        self.assertEqual(report["raw"], raw.hex())

    def test_every_legal_prefix_keeps_trailing_abi_defaults(self):
        source = bytes(range(36))
        defaults = bytearray(36)
        struct.pack_into("<ii", defaults, 28, -1, -1)
        for size in range(37):
            with self.subTest(size=size):
                raw = struct.pack("<II", 6, size) + source[:size]
                i = self.parsed(raw)["instructions"][0]
                expected = defaults.copy()
                expected[:size] = source[:size]
                self.assertEqual(i["native"], expected.hex())
                self.assertEqual(i["stored_size"], size)
                self.assertEqual(i["resource"], struct.unpack_from("<I", expected)[0])

    def test_control_retains_type_selectors_flag_padding_and_threshold(self):
        for kind in (0, 1, 2, 3, 0xffffffff):
            with self.subTest(kind=kind):
                payload = struct.pack("<IIIB3sI", kind, 0xf00000e8, 19, 1, b"\xab\xcd\xef", 2)
                i = self.parsed(struct.pack("<I", 7) + payload)["instructions"][0]
                self.assertEqual(i["native"], payload.hex())
                self.assertEqual(i["stored_size"], 20)
                self.assertEqual((i["payload_bytes"], i["aux_bytes"], i["resource"]), (20, 0, -1))

    def test_audio_does_not_consume_following_instruction_or_auxiliary_xy(self):
        request = struct.pack("<II", 6, 20) + struct.pack("<5i", 4, 0, 0, 77, -1)
        control = struct.pack("<6I", 7, 0, 77, 19, 1, 2)
        draw = struct.pack("<II", 3, 0)
        report = self.parsed(request + control + struct.pack("<I", 41) + draw)
        self.assertEqual([i["opcode"] for i in report["instructions"]], [6, 7, 41, 3])
        self.assertEqual([i["aux_bytes"] for i in report["instructions"]], [0, 0, 0, 4])
        self.assertEqual(report["aux_bytes"], 4)
        self.assertEqual(report["instructions"][2]["native"], "")

    def test_same_audio_abi_is_available_for_every_profile_and_registered_version(self):
        payload = struct.pack("<5i", 316, 0, 0, 1114118, 2)
        raw = struct.pack("<II", 6, 20) + payload
        for profile in ("dfo", "dnf-july", "dnf-compatible"):
            for version in range(10, 19):
                with self.subTest(profile=profile, version=version):
                    i = self.parsed(raw, profile, version)["instructions"][0]
                    self.assertEqual(i["resource"], 316)
                    self.assertEqual(i["native"], (payload + bytes(8) + bytes([255]) * 8).hex())

    def test_invalid_lengths_and_truncated_audio_still_fail(self):
        for raw in (struct.pack("<II", 6, 37) + bytes(37), struct.pack("<II", 6, 36) + bytes(35),
                    struct.pack("<I", 7) + bytes(19)):
            with self.subTest(raw=raw.hex()):
                result = subprocess.run([str(self.exe), "dfo", "17", "6", raw.hex()],
                                        capture_output=True, text=True, encoding="utf-8")
                self.assertNotEqual(result.returncode, 0)

    @unittest.skipUnless(SAMPLE.is_file(), "local user recording unavailable")
    def test_user_recording_has_audio_resources_without_protocol_consumption_changes(self):
        result = subprocess.run([str(self.exe), "--replay", "dnf-compatible", str(SAMPLE)],
                                capture_output=True, text=True, encoding="utf-8", timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual((report["scenes"], report["last_ms"], report["timeline_crc"]),
                         (3909, 66508, 3501111890))
        self.assertEqual((report["op6"], report["op7"], report["op41"], report["tags"]),
                         (7491, 3860, 1, 154))
        self.assertTrue(report["music"])
        self.assertTrue(report["ambient"])
        self.assertTrue(report["exact_eof"])


if __name__ == "__main__":
    unittest.main()
