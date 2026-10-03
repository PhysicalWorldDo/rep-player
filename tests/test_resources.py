import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from npk_reader import _Img, NpkAssets
from test_npk_reader import img_header, rec
ROOT = Path(__file__).resolve().parents[1]

class NativeImageTests(unittest.TestCase):
    def compare(self, data, index=0, palette=0):
        exe = ROOT / 'build' / 'rep_resources.exe'
        self.assertTrue(exe.is_file(), 'native IMG decoder is not implemented')
        with tempfile.TemporaryDirectory(dir=ROOT / 'validation') as folder:
            path = Path(folder) / 'fixture.img'
            path.write_bytes(data)
            out = subprocess.run([str(exe), '--img', str(path), str(index), str(palette)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            got = json.loads(out.stdout)
        expected = _Img(data, 'oracle').frame(index, palette=palette)
        self.assertEqual(got['metadata'], [expected.offset_x, expected.offset_y, expected.width, expected.height, expected.full_width, expected.full_height])
        self.assertEqual(got['rgba_crc32'], zlib.crc32(expected.rgba.tobytes()))
        self.assertEqual(got['native_empty'], expected.native_empty)

    def test_raw_compressed_link_palette_and_native_empty(self):
        payload = zlib.compress(struct.pack('<2H', 0xFC00, 0x83E0))
        self.compare(img_header(2, 44, 2) + rec(14, 6, 2, 1, len(payload)) + struct.pack('<2i', 17, 0) + payload, 1)
        self.compare(b'Neople Image File\0' + struct.pack('<H3I', 0, 0, 1, 1) + rec(16, 5, 1, 1, 0) + bytes([3, 2, 1, 255]))
        palettes = struct.pack('<I', 2) + struct.pack('<I', 2) + bytes([0, 0, 0, 0, 255, 0, 0, 255])
        palettes += struct.pack('<I', 2) + bytes([0, 0, 0, 0, 0, 255, 0, 255])
        payload = zlib.compress(bytes([1]))
        data = img_header(6, 44, 2) + palettes + rec(14, 6, 1, 1, len(payload)) + struct.pack('<2i', 17, 0) + payload
        self.compare(data, 1, 0)
        self.compare(data, 1, 1)
        self.compare(img_header(4, 36, 1) + struct.pack('<I', 1) + bytes(4) + rec(14, 6, 1, 1, 4, x=500, y=0) + bytes.fromhex('789cedd7'))

    def test_dxt_atlas_and_rotation_preserve_full_texture(self):
        dds = b'DDS ' + struct.pack('<7I', 124, 0x81007, 4, 4, 8, 0, 0) + bytes(44)
        dds += struct.pack('<II4s5I', 32, 4, b'DXT1', 0, 0, 0, 0, 0)
        dds += struct.pack('<5I', 0x1000, 0, 0, 0, 0) + struct.pack('<HHI', 0xF800, 0, 0)
        zipped = zlib.compress(dds)
        data = img_header(5, 64, 1) + struct.pack('<3I', 1, 0, 0)
        data += struct.pack('<7I', 1, 18, 0, len(zipped), len(dds), 4, 4)
        data += rec(18, 7, 2, 3, 0) + struct.pack('<7i', 0, 0, 1, 0, 3, 3, 0) + zipped
        self.compare(data)

    def test_native_small_texture_pool_effective_dimensions(self):
        for w,h,expected in [(4,3,[16,16]),(257,400,[512,512]),(513,3,[513,3])]:
            data=img_header(2,36,1)+rec(16,5,w,h,w*h*4,x=0,y=0,full_w=w,full_h=h)+bytes([0,0,255,255])*w*h
            path=ROOT/'validation'/f'pool_{w}_{h}.img';path.write_bytes(data)
            got=json.loads(subprocess.check_output([str(ROOT/'build'/'rep_resources.exe'),'--img',str(path),'0'],text=True))
            self.assertEqual(got.get('effective_texture'),expected)
            self.assertEqual(got.get('logical_texture'),[w,h])
        raw = bytes([0, 0, 255, 255, 0, 255, 0, 255]); zipped = zlib.compress(raw)
        data = img_header(5, 64, 1) + struct.pack('<3I', 1, 0, 0)
        data += struct.pack('<7I', 1, 16, 0, len(zipped), len(raw), 2, 1)
        data += rec(16, 7, 1, 2, 0) + struct.pack('<7i', 0, 0, 0, 0, 2, 1, 1) + zipped
        self.compare(data)

    def test_same_client_native_index_and_missing_fallback(self):
        exe = ROOT / 'build' / 'rep_resources.exe'
        self.assertTrue(exe.is_file(), 'native NPK resource store is not implemented')
        oracle = NpkAssets(r'D:\115us\client\ImagePacks2')
        for path, frame in [('interface/base.img', 91), ('sprite/character/defaultfaces.img', 0), ('no_such_file.img', 123)]:
            expected = oracle.native_frame(path, frame)
            out = subprocess.run([str(exe), '--npk', r'D:\115us\client\ImagePacks2', path, str(frame)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            got = json.loads(out.stdout)
            self.assertEqual(got['rgba_crc32'], zlib.crc32(expected.rgba.tobytes()), path)

if __name__ == '__main__':
    unittest.main()
