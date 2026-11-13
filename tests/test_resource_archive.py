"""Verify native archive output with independent byte-level NPK parsing."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
IMG = ('sprite/test/real.img', b'\0S\xff\1')
OGG = ('sounds/test/effect.ogg', b'OggS\0\xff')
NPK_KEY = (b'puchikon@neople dungeon and fighter ' + b'DNF' * 100)[:255] + b'\0'


class ResourceArchiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'resource_archive' / uuid.uuid4().hex[:10]
        cls.folder.mkdir(parents=True)
        cls.exe = cls.folder / 'resource_archive.exe'
        compiler = ROOT / 'toolchain' / 'llvm-mingw-20260616-ucrt-x86_64' / 'bin' / 'clang++.exe'
        flags = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static']
        subprocess.run([str(compiler), *flags, '-I', str(ROOT / 'src'),
                        str(ROOT / 'tests' / 'resource_archive_native.cpp'),
                        str(ROOT / 'src' / 'resource_archive.cpp'),
                        str(ROOT / 'build' / 'protocol.o'), str(ROOT / 'vendor' / 'zlib' / 'libz.a'),
                        '-lbcrypt', '-o', str(cls.exe)], check=True, timeout=60,
                       env=dict(os.environ, TMP=str(cls.folder), TEMP=str(cls.folder)))

    def run_export(self, mode, target):
        return subprocess.run([str(self.exe), mode, str(target)], capture_output=True,
                              text=True, timeout=15)

    def entries(self, path):
        archive = path.read_bytes()
        self.assertEqual(archive[:16], b'NeoplePack_Bill\0')
        count, = struct.unpack_from('<I', archive, 16)
        header_size = 20 + 264 * count
        self.assertEqual(archive[header_size:header_size + 32],
                         hashlib.sha256(archive[:header_size // 17 * 17]).digest())
        entries = []
        for index in range(count):
            row = 20 + index * 264
            offset, length = struct.unpack_from('<II', archive, row)
            self.assertGreaterEqual(offset, header_size + 32)
            self.assertLessEqual(offset + length, len(archive))
            raw_name = bytes(key ^ byte for key, byte in zip(NPK_KEY, archive[row + 8:row + 264]))
            name = raw_name.split(b'\0', 1)[0].decode('ascii')
            entries.append((name, archive[offset:offset + length]))
        return entries

    def test_raw_entries_and_header_round_trip(self):
        target = self.folder / '测试资源.npk'
        result = self.run_export('normal', target)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '2')
        self.assertEqual(self.entries(target), [IMG, OGG])

    def test_duplicate_resolved_paths_are_written_once(self):
        target = self.folder / 'aliases.npk'
        result = self.run_export('duplicate', target)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '2')
        self.assertEqual(self.entries(target), [IMG, OGG])

    def test_unavailable_entries_allow_partial_archive(self):
        target = self.folder / 'available.npk'
        result = self.run_export('skip', target)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '1')
        self.assertEqual(self.entries(target), [IMG])

    def test_empty_failed_or_cancelled_export_leaves_no_file(self):
        for mode, error in (('empty', 'No resources'), ('load-error', 'fixture unavailable'),
                            ('cancel', 'cancelled')):
            with self.subTest(mode=mode):
                target = self.folder / f'{mode}.npk'
                before = set(self.folder.iterdir())
                result = self.run_export(mode, target)
                self.assertEqual(result.returncode, 1)
                self.assertIn(error, result.stderr)
                self.assertEqual(set(self.folder.iterdir()), before)

    def test_existing_file_is_preserved(self):
        target = self.folder / 'already.npk'
        target.write_bytes(b'keep me')
        before = set(self.folder.iterdir())
        result = self.run_export('normal', target)
        self.assertEqual(result.returncode, 1)
        self.assertIn('already exists', result.stderr)
        self.assertEqual(target.read_bytes(), b'keep me')
        self.assertEqual(set(self.folder.iterdir()), before)

    def test_publishing_cannot_replace_a_concurrent_export(self):
        target = self.folder / 'race.npk'
        before = set(self.folder.iterdir())
        result = self.run_export('destination-race', target)
        self.assertEqual(result.returncode, 1)
        self.assertIn('already exists', result.stderr)
        self.assertEqual(target.read_bytes(), b'competing export')
        self.assertEqual(set(self.folder.iterdir()), before | {target})


if __name__ == '__main__':
    unittest.main()
