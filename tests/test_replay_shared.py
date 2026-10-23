"""Shared REP ownership and cancellation contracts; isolated native objects."""
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest
import uuid

from test_protocol_profiles import pack_rep

ROOT = Path(__file__).resolve().parents[1]


class ReplaySharedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / "validation/load_stage1_20261023/protocol" / uuid.uuid4().hex[:8]
        cls.folder.mkdir(parents=True)
        cls.exe = cls.folder / "replay_shared.exe"
        compiler = ROOT / "toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe"
        environment = dict(os.environ, TEMP=str(cls.folder), TMP=str(cls.folder))
        result = subprocess.run([
            str(compiler), "-std=c++20", "-O2", "-municode", "-static", "-DNOMINMAX",
            "-I", str(ROOT / "src"), "-I", str(ROOT / "vendor/zlib"),
            str(ROOT / "tests/replay_shared_native.cpp"), str(ROOT / "src/protocol.cpp"),
            str(ROOT / "vendor/zlib/libz.a"), "-o", str(cls.exe),
        ], capture_output=True, text=True, encoding="utf-8", env=environment)
        (cls.folder / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(result.stderr)
        draw = struct.pack("<II", 3, 0)
        # Enough dictionary entries to request cancellation while decoding it.
        commands = {key: draw for key in range(7, 1007)}
        cls.fixture = cls.folder / "fixture.rep"
        cls.fixture.write_bytes(pack_rep(commands=commands, resources=["sprite/shared.img"],
                                        scenes=[(stamp, [7], bytes([1, 2, 3, 4])) for stamp in (0, 17, 33)]))

    def invoke(self, mode):
        result = subprocess.run([str(self.exe), mode, str(self.fixture)], capture_output=True,
                                text=True, encoding="utf-8", timeout=30)
        (self.folder / (mode + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_fork_shares_parsed_storage_and_outlives_source(self):
        self.assertTrue(self.invoke("shared")["shared"])

    def test_each_fork_starts_at_zero_and_rewinds_independently_on_parallel_threads(self):
        self.assertEqual(self.invoke("cursors")["scenes"], 3)

    def test_cancelled_before_read_never_publishes_partial_replay(self):
        self.assertTrue(self.invoke("cancel-before")["cancelled"])

    def test_cancelled_during_dictionary_does_not_poison_next_open(self):
        self.assertTrue(self.invoke("cancel-dictionary")["cancelled"])


if __name__ == "__main__":
    unittest.main()
