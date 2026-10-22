"""CN font contracts from read-only CE evidence in validation/cn_fonts_20261021."""
import ctypes as c
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import uuid
import zlib

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay, op
from native_freetype import Rasterizer
from font_atlas import FontAtlas

ROOT = Path(__file__).resolve().parents[1]
CN = Path('E:/WeGameApps/地下城与勇士：创新世纪/Fonts')
SIMSUN = Path(os.environ['WINDIR']) / 'Fonts/simsun.ttc'
FAMILIES = {1: 'NotoSerifSC-SemiBold.otf', 2: 'NotoSerifSC-Bold.otf',
            3: 'NotoSerifSC-Black.otf', 4: 'NotoSansSC-Light.otf',
            5: 'NotoSansSC-Regular.otf', 6: 'NotoSansSC-Bold.otf'}
KINDS = {0: 14, 1: 15, 2: 16, 3: 17, 4: 20, 5: 21, 6: 22,
         7: 14, 8: 14, 9: 15, 10: 14, 11: 14, 12: 14, 13: 14,
         19: 14, 20: 14, 22: 14, 23: 14, 24: 14, 25: 15, 26: 14,
         27: 14, 28: 15, 29: 22, 30: 21, 31: 17, 32: 15, 33: 16,
         34: 14, 35: 14}
CONFIGURATIONS = {0: (12, 400), 1: (15, 400), 2: (15, 400), 3: (15, 400),
                  4: (15, 400), 5: (15, 400), 6: (15, 400), 7: (12, 100),
                  8: (14, 400), 9: (17, 400), 10: (23, 700), 11: (16, 100),
                  12: (34, 700), 13: (15, 700), 19: (17, 700), 20: (29, 700),
                  22: (11, 400), 23: (16, 100), 24: (15, 400), 25: (16, 400),
                  26: (22, 400), 27: (24, 400), 28: (15, 400), 29: (14, 700),
                  30: (14, 700), 31: (12, 400), 32: (12, 400), 33: (12, 400),
                  34: (11, 400), 35: (11, 400)}


@unittest.skipUnless(CN.is_dir() and SIMSUN.is_file(), 'CN fonts unavailable')
class ChineseFontTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/cn_fonts_20261021' / uuid.uuid4().hex[:8]
        cls.folder.mkdir(parents=True)
        cls.reference = Rasterizer()
        cls.paths = {}
        for name in FAMILIES.values():
            cls.paths[name] = cls.folder / name
            shutil.copyfile(CN / name, cls.paths[name])

    def run_json(self, *args):
        p = subprocess.run([str(ROOT / 'build/rep_gpu.exe'), *map(str, args)],
                           capture_output=True, text=True, timeout=20)
        self.assertEqual(p.returncode, 0, p.stderr)
        return json.loads(p.stdout)

    def reference_glyph(self, handle, size, text, outline=0, default_face=None):
        kind = KINDS[handle]
        name = FAMILIES.get(handle)
        if name is None and kind != 14:
            name = FAMILIES[{15: 1, 16: 2, 17: 3, 20: 4, 21: 5, 22: 6}[kind]]
        path = default_face or (SIMSUN if kind == 14 else self.paths[name])
        ref = self.reference
        face = ref.face(path, size)
        default_face = ref.face(default_face or path, size)
        index = ref.api.FT_Get_Char_Index(face, ord(text))
        if not index:
            face = ref.face(self.paths[FAMILIES[1]], size)
            index = ref.api.FT_Get_Char_Index(face, ord(text))
        self.assertNotEqual(index, 0)
        mono = kind == 14 and size in (11, 12, 16, 17)
        ref.check(ref.api.FT_Load_Glyph(face, index, 0x21000 if mono else 0))
        slot = face.contents.glyph
        ref.check(ref.api.FT_Render_Glyph(slot, 2 if mono else 0))
        if outline and slot.contents.bitmap.buffer:
            ref.check(ref.api.FT_GlyphSlot_Own_Bitmap(slot))
            ref.api.FT_Bitmap_Embolden(ref.library, c.byref(slot.contents.bitmap), outline << 6, outline << 6)
        b = slot.contents.bitmap
        pixels = np.zeros((b.rows, b.width), np.uint8)
        for y in range(b.rows):
            line = c.string_at(b.buffer, abs(b.pitch) * b.rows)[y * abs(b.pitch):(y + 1) * abs(b.pitch)]
            for x in range(b.width):
                pixels[y, x] = (255 if line[x // 8] & (0x80 >> (x % 8)) else 0) if b.pixel_mode == 1 else line[x]
        baseline = int(default_face.contents.ascender * default_face.contents.size.contents.metrics.y_scale / 4194304)
        top = baseline - slot.contents.bitmap_top
        if kind == 14:
            top -= 1
            xs = np.flatnonzero(np.any(pixels > (0 if mono else 12), axis=0))
            left = -int(xs[0]) if len(xs) else slot.contents.bitmap_left
            # Current CN callback uses exclusive inkRight + outline/2 - inkLeft.
            advance = int(xs[-1] + 1 - xs[0] + 1) if len(xs) and text != ' ' else (2 * (size >> 1) + 5) // 3
        else:
            left = slot.contents.bitmap_left
            advance = slot.contents.advance.x >> 6 if text != ' ' and np.any(pixels) else (2 * (size >> 1) + 5) // 3
            top -= {10: 2, 11: 2, 12: 2, 13: 3, 14: 4, 15: 3, 16: 3, 17: 4, 18: 4,
                    19: 3, 20: 4, 21: 5, 22: 4, 23: 4, 24: 4, 25: 4, 26: 3,
                    27: 5, 28: 4, 29: 4, 30: 5, 31: 5, 32: 4, 33: 5, 34: 5, 35: 4, 36: 4, 37: 4, 38: 4}.get(size, 0)
        return pixels, [left, top, advance]

    def test_registered_handles_resolve_to_cn_families(self):
        for handle in KINDS:
            with self.subTest(handle=handle):
                expected = [self.reference_glyph(handle, 15, ch)[1] for ch in 'Ab中文']
                actual = self.run_json('--font', handle, 15, 400, 0, 2, 1, 1, 'Ab中文', CN)
                self.assertEqual(actual['metrics'], expected)

    def test_simsun_small_sizes_have_native_mono_pixels(self):
        for size in (11, 12, 16, 17):
            with self.subTest(size=size):
                atlas = FontAtlas()
                for ch in 'A中%':
                    normal = self.reference_glyph(0, size, ch)[0]
                    border = self.reference_glyph(0, size, ch, 2)[0]
                    atlas.register((ch,), normal, border)
                actual = self.run_json('--font', 0, size, 400, 0, 2, 1, 1, 'A中%', CN)
                self.assertEqual(actual['atlas_crc32'], zlib.crc32(atlas.texture_rgba().tobytes()))

    def test_simsun_bitmap_thin_weights_keep_native_bitmap_pixels(self):
        # CN 14737A6C0 ignores the bitmap-weight adjustment result. Its
        # emboldener rejects negative pixel strengths before changing the bitmap.
        for handle, size in ((23, 16), (7, 12), (0, 17)):
            atlas = FontAtlas()
            expected = []
            for ch in '实力认证':
                normal, metric = self.reference_glyph(handle, size, ch)
                border = self.reference_glyph(handle, size, ch, 2)[0]
                atlas.register((ch,), normal, border)
                expected.append(metric)
            for weight in (100, 200, 300):
                with self.subTest(handle=handle, size=size, weight=weight):
                    actual = self.run_json('--font', handle, size, weight, 0, 2, 1, 1, '实力认证', CN)
                    self.assertEqual(actual['metrics'], expected)
                    self.assertEqual(actual['atlas_crc32'], zlib.crc32(atlas.texture_rgba().tobytes()))

    def test_registered_thin_defaults_render_cjk_in_first_frame(self):
        for handle in (7, 11, 23):
            with self.subTest(handle=handle):
                height, _ = CONFIGURATIONS[handle]
                expected = self.layout(handle, height, 400, flags=1, text='中文')
                self.assertEqual(self.layout(handle, 0, 0, flags=1, text='中文'), expected)

    def test_positive_bitmap_weight_adjustment_still_emboldens(self):
        normal = self.run_json('--font', 23, 16, 400, 0, 2, 1, 1, '实力认证', CN)
        bold = self.run_json('--font', 23, 16, 700, 0, 2, 1, 1, '实力认证', CN)
        self.assertNotEqual(bold['atlas_crc32'], normal['atlas_crc32'])

    def layout(self, handle, size, weight=400, flags=0, text='AA'):
        words = (size, weight, handle, 0, 2, 1, 0, 0xffffffff, 0xff000000, 0, flags)
        cmd = op(48, struct.pack('<I11II2fB', 0, *words, 0, 0, 0, 0))
        p = self.folder / 'text.rep'
        p.write_bytes(pack_replay(1.8, {0: cmd}, [(0, (0,), b'')], [text], b'\x0c' + struct.pack('<H', 6) + bytes(126)))
        return self.run_json('--text-layout', p, CN)

    def test_default_sizes_weights_and_tracking_follow_cn_registry(self):
        for handle, (height, weight) in CONFIGURATIONS.items():
            with self.subTest(handle=handle):
                self.assertEqual(self.layout(handle, 0, 0), self.layout(handle, height, weight))
        quads = self.layout(2, 15)
        self.assertEqual(quads[1]['x'] - quads[0]['x'], self.reference_glyph(2, 15, 'A')[1][2])

    def test_cn_simsun_large_text_keeps_requested_size(self):
        q = self.layout(0, 22)[0]
        pixels, (left, top, _) = self.reference_glyph(0, 22, 'A')
        self.assertEqual((q['scale'], q['width'], q['height'], q['x'], q['y']),
                         (1, pixels.shape[1], pixels.shape[0], left, top))

    def test_noto_size_specific_baselines(self):
        for size in (10, 13, 14, 15, 17, 21, 26, 30, 38, 39):
            with self.subTest(size=size):
                got = self.run_json('--font', 1, size, 400, 0, 2, 1, 1, 'A中', CN)
                self.assertEqual(got['metrics'], [self.reference_glyph(1, size, ch)[1] for ch in 'A中'])

    def test_simsun_missing_glyph_uses_its_serif_fallback_and_baseline(self):
        client = self.folder / 'fallback_client'
        fonts = client / 'Fonts'
        fonts.mkdir(parents=True)
        (client / 'DNF.exe').write_bytes(b'')
        # A real face without CJK glyphs forces the registered second face.
        primary = fonts / 'SimSun.ttc'
        shutil.copyfile(Path(os.environ['WINDIR']) / 'Fonts/tahoma.ttf', primary)
        shutil.copyfile(CN / FAMILIES[1], fonts / FAMILIES[1])
        expected = self.reference_glyph(0, 15, '中', default_face=primary)[1]
        actual = self.run_json('--font', 0, 15, 400, 0, 2, 1, 1, '中', fonts)
        self.assertEqual(actual['metrics'], [expected])

    def test_cn_underlines_use_each_registered_default_face(self):
        for handle in (0, 1, 2, 4):
            with self.subTest(handle=handle):
                path = SIMSUN if handle == 0 else self.paths[FAMILIES[handle]]
                f = self.reference.face(path, 15).contents
                base = int(f.ascender * f.size.contents.metrics.y_scale / 4194304)
                u = int(f.underline_position * f.size.contents.metrics.y_ppem / f.units_per_EM)
                q = self.layout(handle, 15, flags=1 << 16)[-1]
                self.assertEqual(q['y'], base + int(u / 2) - u)

    def test_legacy_text_styles_use_cn_registered_defaults(self):
        for style, handle, height, weight in ((1, 8, 14, 400), (2, 9, 17, 400), (4, 11, 16, 100)):
            with self.subTest(style=style):
                expected = self.layout(handle, height, weight)
                payload = struct.pack('<IBBBBIII', 0, style, 0, 1, 0, 0xffffffff, 0xffffffff, 0)
                cmd = op(5, struct.pack('<I', 20) + payload)
                p = self.folder / 'legacy.rep'
                p.write_bytes(pack_replay(1.8, {0: cmd}, [(0, (0,), bytes(4))], ['AA'],
                                          b'\x0c' + struct.pack('<H', 6) + bytes(126)))
                self.assertEqual(self.run_json('--text-layout', p, CN), expected)


if __name__ == '__main__':
    unittest.main()
