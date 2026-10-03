"""Decoded movie files live only while a decoder needs them."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


def wrapped(data):
    padded = (len(data) + 1023) // 1024 * 1024
    encoded = bytearray(data.ljust(padded, b'\0'))
    for n in range(padded - 1, 1023, -1):
        encoded[n] ^= encoded[n - 1024]
    return b'Neople Video Fil' + struct.pack('<4I', 1, 1, len(data), padded) + encoded


class MovieCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'release_103' / ('movies_' + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        cls.client = cls.folder / '客户端'
        cls.client.mkdir()
        cls.cache = cls.folder / 'cache'
        cls.cache.mkdir()
        (cls.cache / 'preserve.txt').write_text('existing cache stays unchanged', encoding='utf8')
        compiler = ROOT / 'toolchain' / 'llvm-mingw-20260616-ucrt-x86_64' / 'bin' / 'clang++.exe'
        cls.probe = cls.folder / 'movie_cache_probe.exe'
        command = [compiler, '-std=c++20', '-O2', '-DNOMINMAX', '-municode', '-static', '-I', ROOT / 'src',
                   ROOT / 'tests' / 'movie_cache_probe.cpp', ROOT / 'src' / 'movies.cpp',
                   ROOT / 'build' / 'protocol.o', ROOT / 'vendor' / 'zlib' / 'libz.a', '-o', cls.probe]
        subprocess.run(list(map(str, command)), check=True, capture_output=True)
        shutil.copy2(ROOT / 'build' / 'ffmpeg.exe', cls.folder / 'ffmpeg.exe')
        avi = cls.client / 'plain.avi'
        subprocess.run([str(cls.folder / 'ffmpeg.exe'), '-v', 'error', '-f', 'lavfi', '-i',
                        'testsrc2=size=32x24:rate=10:duration=1', '-c:v', 'rawvideo', '-pix_fmt', 'yuv420p',
                        str(avi)], check=True, capture_output=True)
        cls.original = avi.read_bytes()
        (cls.client / 'wrapped.avi').write_bytes(wrapped(cls.original))
        (cls.client / 'invalid.avi').write_bytes(wrapped(b'invalid movie stream'))
        result = subprocess.run([str(cls.probe), str(cls.client), str(cls.cache)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.got = json.loads(result.stdout)
        (cls.folder / 'result.json').write_text(json.dumps(cls.got, indent=2), encoding='utf8')

    def test_shared_and_parallel_decoders_keep_their_payloads(self):
        self.assertEqual(self.got['shared'], 1)
        self.assertEqual(self.got['after_first_stop'], 1)
        self.assertEqual(self.got['parallel'], 2)
        self.assertEqual(self.got['after_last_stop'], 1)
        self.assertTrue(self.got['parallel_readable'])

    def test_reset_and_repeated_playback_release_disk_files(self):
        self.assertEqual(self.got['after_reset'], 0)
        self.assertEqual(self.got['peak'], 1)
        self.assertEqual(self.got['after_cycles'], 0)
        self.assertTrue(self.got['rewind'])

    def test_failure_and_shutdown_release_only_owned_files(self):
        self.assertEqual(self.got['after_failure'], 0)
        self.assertEqual(self.got['after_exit'], 0)
        self.assertEqual((self.cache / 'preserve.txt').read_text(encoding='utf8'), 'existing cache stays unchanged')
        self.assertEqual((self.client / 'plain.avi').read_bytes(), self.original)
        self.assertEqual((self.client / 'wrapped.avi').read_bytes(), wrapped(self.original))


if __name__ == '__main__':
    unittest.main()
