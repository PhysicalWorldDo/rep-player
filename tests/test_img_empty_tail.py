"""Synthetic missing-tail IMG regressions against the independently built C++ CLI."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
VALIDATION = ROOT / 'validation' / 'img_empty_tail_20261010'
LOGICAL = 'sprite/character/test/empty_tail.img'
PACKAGE = '!empty_tail_patch.NPK'
EMPTY = (14, 5, 1, 1, 2, 0, 0, 0, 0)
REAL = (16, 5, 2, 2, 16, 0, 0, 2, 2)
RED_BGRA = bytes((0, 0, 255, 255)) * 4
RED_RGBA = bytes((255, 0, 0, 255)) * 4
GREEN_BGRA = bytes((0, 255, 0, 255)) * 4
GREEN_RGBA = bytes((0, 255, 0, 255)) * 4


def record_bytes(record):
    return struct.pack('<' + 'i' * len(record), *record)


def image(records, payloads=None, *, version=2):
    """Build the actual IMG layout; v1 pixel data follows each frame record."""
    if payloads is None:
        payloads = [b''] * len(records)
    if len(payloads) != len(records):
        raise ValueError('every fixture record needs an explicit pixel payload')
    table = b''.join(record_bytes(record) for record in records)
    header = b'Neople Img File\0' + struct.pack('<4I', len(table), 0, version, len(records))
    if version == 5:
        header += struct.pack('<2I', 0, 0)  # No atlases.
    if version in (4, 5):
        header += struct.pack('<I', 0)  # One empty palette.
    elif version == 6:
        header += struct.pack('<I', 0)  # No palettes.
    if version == 1:
        return header + b''.join(record_bytes(record) + payload
                                 for record, payload in zip(records, payloads))
    return header + table + b''.join(payloads)


def write_package(path, payload, *, declared_length=None):
    """Single-entry NPK with the normal encrypted 256-byte logical filename."""
    seed = (b'puchikon@neople dungeon and fighter ' + b'DNF' * 100)[:255] + b'\0'
    name = LOGICAL.encode('utf-8').ljust(256, b'\0')
    encoded = bytes(a ^ b for a, b in zip(name, seed))
    offset = 20 + 264
    length = len(payload) if declared_length is None else declared_length
    table = struct.pack('<II', offset, length) + encoded
    path.write_bytes(b'NeoplePack_Bill\0' + struct.pack('<I', 1) + table + payload)
    return offset, length


class ImgEmptyTailTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.run_folder = VALIDATION / ('run_' + uuid.uuid4().hex[:12])
        cls.run_folder.mkdir(parents=True)
        cls.exe = cls.run_folder / 'rep_resources.exe'
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        command = [str(compiler), '-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE',
                   '-municode', '-static', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor/zlib'),
                   str(ROOT / 'src/protocol.cpp'), str(ROOT / 'src/resources.cpp'),
                   str(ROOT / 'src/resource_validate.cpp'), str(ROOT / 'vendor/zlib/libz.a'),
                   '-o', str(cls.exe)]
        completed = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=120)
        (cls.run_folder / 'compile.command.json').write_text(json.dumps(command, ensure_ascii=False, indent=2),
                                                        encoding='utf-8')
        (cls.run_folder / 'compile.stdout.txt').write_text(completed.stdout, encoding='utf-8')
        (cls.run_folder / 'compile.stderr.txt').write_text(completed.stderr, encoding='utf-8')
        cls.reports = {}
        print('IMG_EMPTY_TAIL_VALIDATION=' + str(cls.run_folder), flush=True)
        if completed.returncode:
            raise RuntimeError(completed.stderr)

    @classmethod
    def tearDownClass(cls):
        (cls.run_folder / 'observations.json').write_text(json.dumps(cls.reports, ensure_ascii=False, indent=2),
                                                     encoding='utf-8')

    def folder(self):
        path = self.run_folder / self._testMethodName
        path.mkdir(exist_ok=True)
        return path

    def invoke(self, arguments, *, success=True):
        command = [str(self.exe), *map(str, arguments)]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=20)
        records = self.reports.setdefault(self._testMethodName, [])
        index = len(records)
        folder = self.folder()
        (folder / f'probe_{index}.stdout.txt').write_text(result.stdout, encoding='utf-8')
        (folder / f'probe_{index}.stderr.txt').write_text(result.stderr, encoding='utf-8')
        records.append({'command': command, 'exit_code': result.returncode,
                        'stdout': result.stdout, 'stderr': result.stderr})
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)
        self.assertNotEqual(result.returncode, 0, 'a truncated fixture was accepted')
        return result.stderr

    def frame(self, payload, index=0, *, success=True, filename='fixture.img'):
        path = self.folder() / filename
        path.write_bytes(payload)
        return self.invoke(['--img', path, index], success=success)

    def assert_empty(self, result, metadata=(0, 0, 1, 1, 0, 0)):
        self.assertTrue(result['native_empty'])
        self.assertEqual(result['metadata'], list(metadata))
        self.assertEqual(result['effective_texture'], [1, 1])
        self.assertEqual(result['rgba_crc32'], zlib.crc32(bytes(4)))

    def assert_real(self, result, pixels, metadata=(0, 0, 2, 2, 2, 2)):
        self.assertFalse(result['native_empty'])
        self.assertEqual(result['metadata'], list(metadata))
        self.assertEqual(result['rgba_crc32'], zlib.crc32(pixels))

    def test_124_byte_native_empty_and_all_seven_links_are_transparent(self):
        payload = image([EMPTY] + [(17, 0)] * 7)
        self.assertEqual(len(payload), 124)
        for index in range(8):
            with self.subTest(frame=index):
                self.assert_empty(self.frame(payload, index))

    def test_real_pixels_before_missing_tail_empty_and_link_stay_valid(self):
        payload = image([REAL, EMPTY, (17, 1)], [RED_BGRA, b'', b''])
        self.assert_real(self.frame(payload, 0), RED_RGBA)
        self.assert_empty(self.frame(payload, 1))
        self.assert_empty(self.frame(payload, 2))

    def test_present_empty_padding_is_consumed_before_later_real_frame(self):
        payload = image([EMPTY, REAL, (17, 1)], [b'\x1f\x7c', GREEN_BGRA, b''])
        self.assert_empty(self.frame(payload, 0))
        self.assert_real(self.frame(payload, 1), GREEN_RGBA)
        self.assert_real(self.frame(payload, 2), GREEN_RGBA)

    def test_ordinary_one_by_one_without_native_empty_marker_is_rejected(self):
        ordinary = (14, 5, 1, 1, 2, 0, 0, 1, 1)
        error = self.frame(image([ordinary]), success=False)
        self.assertIn('truncated structure at byte 68', error)

    def test_partial_empty_pixel_payload_is_rejected(self):
        error = self.frame(image([EMPTY, (17, 0)], [b'\0', b'']), success=False)
        self.assertIn('truncated structure at byte 76', error)

    def test_missing_real_pixels_after_padded_empty_are_rejected(self):
        error = self.frame(image([EMPTY, REAL], [b'\0\0', b'']), success=False)
        self.assertIn('truncated structure at byte 106', error)

    def test_missing_empty_before_later_nonlink_record_is_rejected(self):
        error = self.frame(image([EMPTY, EMPTY]), success=False)
        self.assertIn('truncated structure at byte 104', error)

    def test_native_marker_compares_x_to_full_width_instead_of_zero(self):
        for fmt in (14, 15, 16):
            with self.subTest(color_format=fmt):
                marker = (fmt, 5, 1, 1, 4 if fmt == 16 else 2, 7, 0, 7, 9)
                self.assert_empty(self.frame(image([marker])), (7, 0, 1, 1, 7, 9))

    def test_native_marker_with_nonzero_y_is_rejected(self):
        nonempty = (14, 5, 1, 1, 2, 0, 1, 0, 0)
        error = self.frame(image([nonempty]), success=False)
        self.assertIn('truncated structure at byte 68', error)

    def test_other_img_versions_keep_strict_missing_payload_checks(self):
        for version in (1, 4, 5, 6):
            with self.subTest(version=version):
                error = self.frame(image([EMPTY], version=version), success=False,
                                   filename=f'version_{version}.img')
                self.assertIn('truncated structure', error)

    def test_other_encodings_keep_strict_missing_payload_checks(self):
        for fmt, compression in ((14, 6), (13, 5), (18, 5), (19, 5), (20, 5)):
            with self.subTest(color_format=fmt, compression=compression):
                record = (fmt, compression, 1, 1, 2, 0, 0, 0, 0)
                error = self.frame(image([record]), success=False,
                                   filename=f'format_{fmt}_compression_{compression}.img')
                self.assertIn('truncated structure at byte 68', error)

    def test_normal_negative_coordinates_preserve_pixels_and_offsets(self):
        negative = (16, 5, 2, 2, 16, -35, -12, 2, 2)
        result = self.frame(image([negative], [RED_BGRA]))
        self.assert_real(result, RED_RGBA, (-35, -12, 2, 2, 2, 2))

    def test_npk_img_parse_error_includes_path_package_offset_and_length(self):
        packs = self.folder() / 'ImagePacks2'
        packs.mkdir()
        (packs / 'sprite.NPK').write_bytes(b'package boundary is not opened')
        ordinary = (14, 5, 1, 1, 2, 0, 0, 1, 1)
        offset, length = write_package(packs / PACKAGE, image([ordinary]))
        error = self.invoke(['--npk', packs, LOGICAL, 0], success=False)
        self.assertIn('truncated structure at byte 68', error)
        for text in (LOGICAL, PACKAGE, f'offset {offset}', f'length {length}'):
            self.assertIn(text, error)

    def test_npk_short_entry_error_includes_path_package_offset_and_length(self):
        packs = self.folder() / 'ImagePacks2'
        packs.mkdir()
        (packs / 'sprite.NPK').write_bytes(b'package boundary is not opened')
        payload = image([REAL], [RED_BGRA])
        offset, length = write_package(packs / PACKAGE, payload, declared_length=len(payload) + 1)
        error = self.invoke(['--npk', packs, LOGICAL, 0], success=False)
        self.assertIn('truncated IMG entry', error)
        for text in (LOGICAL, PACKAGE, f'offset {offset}', f'length {length}'):
            self.assertIn(text, error)


if __name__ == '__main__':
    unittest.main()
