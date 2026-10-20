"""Direct IMG pixels may be followed by unused decompressed patch data."""
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = bytes((255, 0, 0, 255, 0, 255, 0, 255))
PIXELS = {14: bytes.fromhex('00fce083'), 15: bytes.fromhex('00fff0f0'),
          16: bytes.fromhex('0000ffff00ff00ff')}


def image(fmt, raw):
    payload = zlib.compress(raw)
    header = b'Neople Img File\0' + struct.pack('<4I', 80, 0, 2, 3)
    records = struct.pack('<9i', fmt, 6, 2, 1, len(payload), -3, 4, 32, 32)
    records += struct.pack('<9i', 16, 5, 1, 1, 4, 0, 0, 1, 1)
    records += struct.pack('<2i', 17, 0)
    return header + records + payload + bytes((255, 0, 0, 255))


class ImgPixelSuffixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = ROOT / 'validation' / 'img_pixel_suffix'
        folder.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(dir=folder)
        cls.folder = Path(cls.temporary.name)
        cls.exe = cls.folder / 'resources.exe'
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        subprocess.run([str(compiler), '-std=c++20', '-O2', '-DNOMINMAX', '-municode', '-static',
                        '-I', str(ROOT / 'vendor/zlib'), str(ROOT / 'src/protocol.cpp'),
                        str(ROOT / 'src/resources.cpp'), str(ROOT / 'src/resource_validate.cpp'),
                        str(ROOT / 'vendor/zlib/libz.a'), '-o', str(cls.exe)],
                       check=True, capture_output=True, timeout=120)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def decode(self, payload, index=0):
        path = self.folder / 'fixture.img'
        path.write_bytes(payload)
        return subprocess.run([str(self.exe), '--img', str(path), str(index)],
                              capture_output=True, text=True, timeout=10)

    def assert_pixels(self, payload):
        for index, pixels in ((0, EXPECTED), (1, bytes((0, 0, 255, 255))), (2, EXPECTED)):
            result = self.decode(payload, index)
            self.assertEqual(result.returncode, 0, result.stderr)
            decoded = json.loads(result.stdout)
            self.assertEqual(decoded['rgba_crc32'], zlib.crc32(pixels))
            if index != 1:
                self.assertEqual(decoded['metadata'], [-3, 4, 2, 1, 32, 32])

    def test_decompressed_suffix_is_ignored_without_shifting_next_frame_or_link(self):
        for fmt, raw in PIXELS.items():
            with self.subTest(format=fmt):
                self.assert_pixels(image(fmt, raw + bytes.fromhex('123456789a')))

    def test_exact_pixel_payload_still_decodes(self):
        for fmt, raw in PIXELS.items():
            with self.subTest(format=fmt):
                self.assert_pixels(image(fmt, raw))

    def test_short_decompressed_payload_still_fails(self):
        for fmt, raw in PIXELS.items():
            with self.subTest(format=fmt):
                result = self.decode(image(fmt, raw[:-1]))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('pixel length', result.stderr)


if __name__ == '__main__':
    unittest.main()
