"""GPU regressions for the user-controlled visibility of signed IMG offsets."""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay, npk, img_header, rec, legacy, op
from test_default_layer_camera import draw43
from test_layer_camera_clip import camera

ROOT = Path(__file__).resolve().parents[1]


class NegativeImgOffsetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'negative_img_offsets_20261010' / uuid.uuid4().hex[:12]
        cls.folder.mkdir(parents=True)
        shutil.copytree(ROOT / 'assets/shaders', cls.folder / 'assets/shaders')
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        options = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-static',
                   '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor/zlib'),
                   '-I', str(ROOT / 'vendor/freetype/include')]
        names = ('protocol', 'resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine')
        sources = [ROOT / 'src' / (name + '.cpp') for name in names]
        sources.append(ROOT / 'tests/negative_img_offsets_native.cpp')

        def compile_one(source):
            target = cls.folder / (source.stem + '.o')
            result = subprocess.run([str(compiler), *options, '-c', str(source), '-o', str(target)],
                                    capture_output=True, text=True, timeout=120)
            if result.returncode:
                raise RuntimeError(result.stderr)
            return target

        with ThreadPoolExecutor(max_workers=4) as pool:
            objects = list(pool.map(compile_one, sources))
        cls.binary = cls.folder / 'negative_img_offsets_native.exe'
        result = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects),
                                 str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
                                 '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-o', str(cls.binary)],
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stderr)
        cls.counter = 0

    def fixture(self, offsets, scenes, resource='sprite/test/frame.img', links=(), extra_resources=(), npk_resource=None):
        type(self).counter += 1
        folder = self.folder / (self._testMethodName + '_' + str(self.counter))
        folder.mkdir()
        pixels = bytes([0, 0, 255, 255]) * 16
        table = b''.join(rec(16, 5, 4, 4, len(pixels), x=x, y=y, full_w=8, full_h=8)
                         for x, y in offsets)
        table += b''.join(struct.pack('<2i', 17, frame) for frame in links)
        image = img_header(2, len(table), len(offsets) + len(links)) + table + pixels * len(offsets)
        (folder / ('!negative_placeholder.NPK' if npk_resource else 'sprite_test.NPK')).write_bytes(npk(image, npk_resource or resource))
        if npk_resource:
            # The native overlay convention scans packages before the first
            # sprite package; a normal package establishes that boundary.
            (folder / 'sprite_dummy.NPK').write_bytes(npk(image, 'sprite/dummy/frame.img'))
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        header += b'\x0c' + struct.pack('<H', 6) + bytes(126)
        commands = dict(sorted(enumerate(raw for raw, aux in scenes), key=lambda item: len(item[1])))
        path = folder / 'fixture.rep'
        path.write_bytes(pack_replay(1.8, commands, [(n * 100, (n,), aux) for n, (raw, aux) in enumerate(scenes)],
                                     [resource, *extra_resources], header))
        return path, folder

    def render(self, offsets, scenes, enabled=False, links=(), mode=None, extra_resources=(),
               resource='sprite/test/frame.img', npk_resource=None):
        path, assets = self.fixture(offsets, scenes, resource=resource, links=links,
                                    extra_resources=extra_resources, npk_resource=npk_resource)
        result = subprocess.run([str(self.binary), str(self.folder), str(path), str(assets),
                                 mode or ('enabled' if enabled else 'default')],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        path.with_suffix('.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        self.assertEqual((report['width'], report['height']), (16, 16))
        self.assertTrue(report['source_preserved'], 'IMG coordinates and decoded pixels must remain unchanged')
        pixels = path.with_suffix('.rgba').read_bytes()
        self.assertEqual(len(pixels), 16 * 16 * 4)
        return pixels, report

    def assert_rectangles(self, pixels, rectangles):
        for y in range(16):
            for x in range(16):
                visible = any(left <= x < right and top <= y < bottom
                              for left, top, right, bottom in rectangles)
                actual = tuple(pixels[(y * 16 + x) * 4:(y * 16 + x + 1) * 4])
                self.assertEqual(actual, (255, 0, 0, 255) if visible else (0, 0, 0, 0), (x, y, rectangles))

    def test_default_hides_either_negative_img_offset(self):
        draw = legacy(6, 6)
        for offset in ((-2, 0), (0, -2), (-2, -2)):
            with self.subTest(offset=offset):
                pixels, report = self.render([offset], [draw])
                self.assert_rectangles(pixels, [])
                self.assertEqual((report['recorded_images'], report['drawn_images'], report['references']), (1, 0, 1))

    def test_nonnegative_img_offsets_and_negative_rep_position_still_draw(self):
        for offset, position, rectangle in (((0, 0), (2, 2), (2, 2, 6, 6)),
                                             ((2, 3), (-1, -1), (1, 2, 5, 6))):
            with self.subTest(offset=offset):
                pixels, _ = self.render([offset], [legacy(*position)])
                self.assert_rectangles(pixels, [rectangle])

    def test_enabling_keeps_signed_offsets_without_expanding_canvas(self):
        pixels, report = self.render([(-2, -2)], [legacy(6, 6)], enabled=True)
        self.assertTrue(report['supported'], 'Executor switch API must be available')
        self.assert_rectangles(pixels, [(4, 4, 8, 8)])

    def test_mixed_frames_and_link_use_each_resolved_img_offset(self):
        negative, a = legacy(6, 6, frame=0)
        positive, b = legacy(10, 10, frame=1)
        linked, c = legacy(2, 2, frame=2)
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                pixels, _ = self.render([(-2, -2), (0, 0)], [(negative + positive + linked, a + b + c)],
                                        enabled=enabled, links=(0,))
                self.assert_rectangles(pixels, [(10, 10, 14, 14)] + ([(4, 4, 8, 8), (0, 0, 4, 4)] if enabled else []))

    def test_missing_resource_filters_negative_native_placeholder(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                pixels, report = self.render([(-2, -2)] * 92, [legacy(6, 6)], enabled=enabled,
                                            resource='sprite/missing/frame.img', npk_resource='interface/base.img')
                self.assertGreater(report['fallbacks'], 0, 'Fixture must resolve through the native IMG placeholder')
                self.assert_rectangles(pixels, [(4, 4, 8, 8)] if enabled else [])

    def test_grid_and_reflection_filter_before_recursive_geometry(self):
        grid = op(32, struct.pack('<hhB3hB2hIi', 0, 0, 3, 1, 3, 99, 2, 1, 3, 0, 0))
        stretched, aux = legacy(6, 6, scale=(2, 2))
        reflected, reflection_aux = legacy(6, 6, special=1, pivot=(2, 0))
        for raw, data in ((grid + stretched, aux), (reflected, reflection_aux)):
            with self.subTest(grid=raw.startswith(grid)):
                pixels, _ = self.render([(-2, 0)], [(raw, data)])
                self.assert_rectangles(pixels, [])
                enabled, _ = self.render([(-2, 0)], [(raw, data)], enabled=True)
                self.assertTrue(any(enabled[3::4]), 'Enabled recursive draw should retain visible pixels')

    def test_local_negative_mask_does_not_activate_offscreen_capture(self):
        capture = op(58, struct.pack('<Ii7f4B', 0, 0, 1, 1, 6, 6, 0, 0, 0, 0, 0, 0, 0))
        draw, aux = draw43(0, 0, 0, scale=(3, 3))
        params = bytearray(draw[8:]);struct.pack_into('<i', params, 0, 1)
        draw = op(46, struct.pack('<I', 0) + params)
        raw = camera(0) + capture + draw + op(59)
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                pixels, _ = self.render([(-2, -2), (0, 0)], [(raw, aux)], enabled=enabled)
                self.assert_rectangles(pixels, [(4, 4, 8, 8)] if enabled else [])

    def test_global_negative_mask_disables_mask_and_keeps_normal_draw(self):
        capture = op(15, b'\1' + struct.pack('<Ii2f2i3f', 0, 0, 1, 1, 6, 6, 0, 0, 0))
        draw, aux = legacy(0, 0, frame=1, scale=(3, 3))
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                pixels, _ = self.render([(-2, -2), (0, 0)], [(capture + draw, aux)], enabled=enabled)
                self.assert_rectangles(pixels, [(4, 4, 8, 8)] if enabled else [(0, 0, 12, 12)])

    def test_toggle_rebuilds_current_frame_and_preserves_playback_state(self):
        first, a = legacy(6, 6)
        second, b = draw43(6, 6, 2)
        scenes = [(camera(2) + first, a), (second, b), (second, b)]
        for mode in ('paused', 'ended', 'stopped', 'running', 'skipped'):
            with self.subTest(mode=mode):
                pixels, report = self.render([(-2, -2)], scenes, mode='playback-' + mode)
                for field in ('playback_supported', 'state_preserved', 'camera_preserved', 'hidden_preserved',
                              'reference_pixels_equal', 'pixels_changed', 'round_trip_equal'):
                    self.assertTrue(report[field], (mode, field, report))
                self.assert_rectangles(pixels, [(4, 4, 8, 8)])

    def test_toggle_rebuilds_cached_context_from_prior_scene(self):
        mask, a = legacy(6, 6)
        draw, b = legacy(0, 0, frame=1, scale=(3, 3))
        later_mask, c = legacy(10, 2, frame=1)
        effect = op(19, b'\1' + struct.pack('<IIBII', 54, 7, 0, 0, 0) + struct.pack('<7f', 0, 0, 0, 0, 0, 1, 1))
        scenes = [(op(38) + mask + op(39), a),
                  (effect + draw + op(40) + op(38) + later_mask + op(39), b + c),
                  (effect + draw + op(40), b)]
        pixels, report = self.render([(-2, -2), (0, 0)], scenes, mode='playback-paused')
        for field in ('playback_supported', 'state_preserved', 'camera_preserved', 'hidden_preserved',
                      'reference_pixels_equal', 'pixels_changed', 'round_trip_equal'):
            self.assertTrue(report[field], (field, report))
        self.assertTrue(any(pixels[3::4]))

    def test_font_at_negative_rep_position_is_unaffected(self):
        words = (11, 400, 0, 0, 0, 1, 0, 0xffff0000, 0xff000000, 0xffff0000, 0)
        text = op(48, struct.pack('<I11II2fB', 1, *words, 0, -1, 0, 0))
        raw = camera(0) + text
        default, _ = self.render([(-2, -2)], [(raw, b'')], extra_resources=('A',))
        enabled, _ = self.render([(-2, -2)], [(raw, b'')], enabled=True, extra_resources=('A',))
        self.assertTrue(any(default[3::4]), 'Font remains visible despite its negative REP position')
        self.assertEqual(default, enabled)


if __name__ == '__main__':
    unittest.main()
