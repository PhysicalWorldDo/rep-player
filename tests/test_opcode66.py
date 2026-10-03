"""Optional real-input structural regression; inputs are never bundled.

Run build.ps1 first: the probe links the current native protocol object.
Passing this gate does not establish opcode66 state or rendering semantics.
"""
import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(os.environ.get('REP_OPCODE66_CLIENT', r'E:\WeGameApps\地下城与勇士：创新世纪'))
SAMPLE = Path(os.environ.get('REP_OPCODE66_SAMPLE', r'C:\Users\CAO\Downloads\202610040227.rep'))


@unittest.skipUnless(CLIENT.is_dir() and SAMPLE.is_file(), 'local opcode66 inputs unavailable')
class Opcode66RealProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'opcode66_20261004' / ('protocol_' + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        cls.exe = cls.folder / 'protocol_probe.exe'
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        result = subprocess.run([
            str(compiler), '-std=c++20', '-O2', '-municode', '-static', '-DNOMINMAX',
            '-I', str(ROOT / 'src'), str(ROOT / 'tests/opcode66_protocol_probe.cpp'),
            str(ROOT / 'build/protocol.o'), str(ROOT / 'vendor/zlib/libz.a'), '-o', str(cls.exe),
        ], capture_output=True, text=True, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(result.stderr)

    def test_user_recording_parses_all_scenes_with_client_selection(self):
        result = subprocess.run([str(self.exe), str(CLIENT), str(SAMPLE)],
                                capture_output=True, text=True, encoding='utf-8', timeout=60)
        (self.folder / 'result.json').write_text(json.dumps({
            'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
        }, ensure_ascii=False, indent=2), encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report['exact_eof'])
        self.assertEqual((report['scenes'], report['references'], report['aux_bytes']),
                         (3909, 10683196, 10697088))
        self.assertEqual(report['timeline_crc32'], 3501111890)
        self.assertEqual((report['opcode66_commands'], report['opcode66_references']), (1, 116869))


if __name__ == '__main__':
    unittest.main()
