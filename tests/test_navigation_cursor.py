"""A cancelled navigation cursor never consumes the currently playing stream."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_protocol_profiles import pack_rep

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / 'validation/seek_native_20261029/cursor'


class NavigationCursorTests(unittest.TestCase):
    def test_independent_scan_can_cancel_or_commit(self):
        FOLDER.mkdir(parents=True, exist_ok=True)
        fixture = FOLDER / 'cursor.rep'
        fixture.write_bytes(pack_rep(commands={7: struct.pack('<II', 3, 0)},
            resources=['sprite/test.img'], scenes=[(t, [7], bytes(4)) for t in (0, 10, 20, 30)]))
        exe = FOLDER / 'navigation_cursor.exe'
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        build = subprocess.run(list(map(str, [compiler, '-std=c++20', '-O2', '-DNOMINMAX', '-municode', '-static',
            '-I', ROOT / 'src', '-I', ROOT / 'vendor/zlib', ROOT / 'tests/navigation_cursor_probe.cpp',
            ROOT / 'src/protocol.cpp', ROOT / 'vendor/zlib/libz.a', '-o', exe])),
            capture_output=True, text=True, encoding='utf8', timeout=120)
        self.assertEqual(build.returncode, 0, build.stderr)
        run = subprocess.run([str(exe), str(fixture)], capture_output=True, text=True, timeout=20)
        self.assertEqual(run.returncode, 0, run.stderr)
        got = json.loads(run.stdout)
        (FOLDER / ('green.json' if got.get('implemented') else 'red.json')).write_text(json.dumps(got, indent=2), encoding='utf8')
        for field in ('implemented', 'cancelled', 'unchanged', 'adopted'):
            self.assertTrue(got.get(field), got)


if __name__ == '__main__':
    unittest.main()
