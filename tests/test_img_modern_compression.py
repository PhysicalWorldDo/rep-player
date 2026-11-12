import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]


def words(*values):
    return struct.pack(f'<{len(values)}I', *values)


def img_header(version, table_size, frame_count):
    return b'Neople Img File\0' + words(table_size, 0, version, frame_count)


def flat_bc7_block():
    # Mode 6 with equal endpoints, zero p-bits and all indices zero.
    bits = 1 << 6
    for i, component in enumerate((120, 120, 32, 32, 8, 8, 127, 127)):
        bits |= component << (7 + i * 7)
    return bits.to_bytes(16, 'little')


def bc7_dds(blocks, dxgi):
    header = b'DDS ' + words(124, 0x81007, 4, 4, 16, 0, 1) + bytes(44)
    header += words(32, 4) + b'DX10' + words(0, 0, 0, 0, 0)
    header += words(0x1000, 0, 0, 0, 0, dxgi, 3, 0, 1, 0)
    return header + blocks


def atlas_img(raw, payload):
    # A linked 3x2 frame cropped and rotated from a 4x4 atlas.
    header = img_header(5, 72, 2) + words(1, 0, 0)
    atlas = words(0, 24, 0, len(payload), len(raw), 4, 4)
    frame = words(24, 7, 3, 2, 0, 2, 3, 20, 21, 0, 0, 1, 0, 3, 3, 1)
    return header + atlas + frame + words(17, 0) + payload


class ModernImgCompressionTests(unittest.TestCase):
    def decode(self, data, frame, oodle=False):
        extra = []
        if oodle:
            client = os.environ.get('REP_PLAYER_TEST_OODLE_CLIENT')
            if not client:
                self.skipTest('set REP_PLAYER_TEST_OODLE_CLIENT to a client with oo2core_9_win64.dll')
            extra = ['0', client]
        validation = ROOT / 'validation'
        validation.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=validation) as folder:
            img = Path(folder) / 'texture.img'
            img.write_bytes(data)
            result = subprocess.run(
                [str(ROOT / 'build' / 'rep_resources.exe'), '--img', str(img), str(frame), *extra],
                capture_output=True, text=True, timeout=60,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def assert_atlas_pixels(self, result):
        self.assertEqual(result['metadata'], [2, 3, 3, 2, 20, 21])
        self.assertEqual(result['logical_texture'], [4, 4])
        self.assertEqual(result['rgba_crc32'], zlib.crc32(bytes([240, 64, 16, 254]) * 6))

    def test_bc7_dds_unorm_and_srgb(self):
        for dxgi, wrap in ((98, zlib.compress), (99, bytes)):
            with self.subTest(dxgi=dxgi):
                raw = bc7_dds(flat_bc7_block(), dxgi)
                self.assert_atlas_pixels(self.decode(atlas_img(raw, wrap(raw)), 1))

    def test_oodle_bc7prep_dds(self):
        # BC7Prep mode 8 carries an untransformed BC7 block in the mode stream.
        prep = words(0x7bc, 0, *([0] * 8), 1, 0) + flat_bc7_block() + b'\x08'
        raw = bc7_dds(prep, 98)
        self.assert_atlas_pixels(self.decode(atlas_img(raw, b'\xcc\x0c' + raw), 1, oodle=True))

    def test_zlib_bc7prep_dds(self):
        prep = words(0x7bc, 0, *([0] * 8), 1, 0) + flat_bc7_block() + b'\x08'
        raw = bc7_dds(prep, 98)
        self.assert_atlas_pixels(self.decode(atlas_img(raw, zlib.compress(raw)), 1))

    def test_oodle_indexed_frame(self):
        payload = b'\xcc\x0c\x01\x00'
        palette = words(2) + bytes([0, 0, 255, 255, 255, 0, 0, 255])
        img = img_header(4, 36, 1) + palette + words(14, 6, 2, 1, len(payload), 0, 0, 2, 1) + payload
        decoded = self.decode(img, 0, oodle=True)
        self.assertEqual(decoded['rgba_crc32'], zlib.crc32(palette[8:12] + palette[4:8]))


if __name__ == '__main__':
    unittest.main()
