"""GPU geometry regressions derived from the two native REP readers."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay, npk, img_header, rec, legacy, op

ROOT = Path(__file__).resolve().parents[1]


def grid57(version, extent=(14, 14), source_scale=(1, 1), position=(1, 2),
           xs=(2, 4), ys=(2, 4)):
    # resource DWORD followed by the 32/40-byte versioned parameter body.
    params = bytearray(40 if version >= 1.8 else 32)
    struct.pack_into('<I', params, 0, 0)
    struct.pack_into('<i', params, 4, 0)
    struct.pack_into('<2f', params, 8, *position)
    struct.pack_into('<2f', params, 16, *extent)
    struct.pack_into('<I', params, 28, 0xffffffff)
    if version >= 1.8:
        struct.pack_into('<2f', params, 32, *source_scale)
    arrays = struct.pack('<QQ', len(xs), len(ys))
    arrays += struct.pack('<' + 'i' * len(xs), *xs)
    arrays += struct.pack('<' + 'i' * len(ys), *ys)
    arrays += struct.pack('<2I', 0, 0)
    return op(57, struct.pack('<I', 0) + params + arrays)


class ProtocolRenderAdaptationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'protocol_102' / ('render_' + uuid.uuid4().hex[:8])
        cls.assets = cls.folder / 'assets'
        cls.assets.mkdir(parents=True)
        # Full IMG canvas 12x12, visible cropped rectangle [2,2,6,6].
        raw = bytes([0, 0, 255, 255]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw),
                                          x=2, y=2, full_w=12, full_h=12) + raw
        (cls.assets / 'sprite_grid.NPK').write_bytes(npk(image, 'sprite/grid/frame.img'))

    def render(self, raw, version=1.8, aux=b'', profile='dfo'):
        header = b'\x0b\0' + struct.pack('<8h', *([32, 32] * 4))
        header += b'\x0c' + struct.pack('<H', 6) + bytes(126)
        path = self.folder / (self._testMethodName + '.rep')
        path.write_bytes(pack_replay(version, {0: raw}, [(0, (0,), aux)],
                                    ['sprite/grid/frame.img'], header))
        rgba, report = path.with_suffix('.rgba'), path.with_suffix('.json')
        command = [str(ROOT / 'build' / 'rep_gpu.exe'), '--consume', str(path),
                   str(report), str(rgba), str(self.assets), '--profile', profile]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.report = json.loads(report.read_text(encoding='utf-8'))
        self.assertTrue(self.report['exact_eof'])
        pixels = rgba.read_bytes()
        self.assertEqual(len(pixels), 32 * 32 * 4)
        return lambda x, y: tuple(pixels[(y * 32 + x) * 4:(y * 32 + x) * 4 + 3])

    def assert_rectangle(self, pixel, rectangle):
        left, top, right, bottom = rectangle
        for y in range(32):
            for x in range(32):
                expected = (255, 0, 0) if left <= x < right and top <= y < bottom else (0, 0, 0)
                self.assertEqual(pixel(x, y), expected, (x, y, rectangle))

    def test_v18_grid57_uses_direct_target_extent(self):
        # Native >=1.8 forwards14 directly, stretching [2,4] from2 to4;
        # visible source [2,6] becomes [2,8], then position is added.
        pixel = self.render(grid57(1.8))
        self.assert_rectangle(pixel, (3, 4, 9, 10))

    def test_v18_grid57_source_scale_changes_origin_and_extent(self):
        pixel = self.render(grid57(1.8, source_scale=(2, 1)))
        self.assert_rectangle(pixel, (5, 4, 17, 10))

    def test_v18_grid57_signed_source_scale_keeps_native_origin(self):
        pixel = self.render(grid57(1.8, source_scale=(-1, 1), position=(10, 2)))
        self.assert_rectangle(pixel, (2, 4, 8, 10))

    def test_v18_grid57_zero_source_scale_draws_nothing(self):
        pixel = self.render(grid57(1.8, source_scale=(0, 1)))
        self.assert_rectangle(pixel, (0, 0, 0, 0))

    def test_legacy_grid57_converts_stored_float_to_extent(self):
        # full12-cropped4+cropped4*1.5 gives14, the same target as above.
        pixel = self.render(grid57(1.7, extent=(1.5, 1.5)), version=1.7)
        self.assert_rectangle(pixel, (3, 4, 9, 10))

    def test_v18_ordinary_grid_keeps_legacy_scale_conversion(self):
        grid = op(32, struct.pack('<hhB2hB2hIi', 0, 0, 2, 2, 4, 2, 2, 4, 0, 0))
        draw, aux = legacy(1, 2, scale=(1.5, 1.5))
        pixel = self.render(grid + draw, aux=aux)
        self.assert_rectangle(pixel, (3, 4, 9, 10))


if __name__ == '__main__':
    unittest.main()
