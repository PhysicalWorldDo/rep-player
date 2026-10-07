"""Pixel regressions: canvas expansion adds pixels without changing REP projection."""
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
from test_layer_camera_clip import camera, clip

ROOT = Path(__file__).resolve().parents[1]


def effect(kind, values):
    return op(19, b'\1' + struct.pack('<IIBII', kind, len(values), 0, 0, 0)
              + struct.pack('<' + 'f' * len(values), *values))


class CanvasPixelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'canvas_20261007' / ('gpu_' + uuid.uuid4().hex[:8])
        cls.assets = cls.folder / 'assets'
        (cls.folder / 'build').mkdir(parents=True)
        cls.binary = cls.folder / 'build' / 'rep_gpu.exe'
        shutil.copy2(ROOT / 'build' / 'rep_gpu.exe', cls.binary)
        shutil.copytree(ROOT / 'assets' / 'shaders', cls.assets / 'shaders')
        pixels = bytes([0, 0, 255, 255]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(pixels),
                                         x=0, y=0, full_w=4, full_h=4) + pixels
        (cls.assets / 'sprite_test.NPK').write_bytes(npk(image))
        cls.counter = 0

    def replay(self, scenes):
        type(self).counter += 1
        path = self.folder / (self._testMethodName + '_' + str(self.counter) + '.rep')
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        header += b'\x0c' + struct.pack('<H', 6) + bytes(126)
        commands = dict(sorted(enumerate(raw for raw, aux in scenes), key=lambda item: len(item[1])))
        path.write_bytes(pack_replay(1.8, commands,
                                    [(n * 100, (n,), aux) for n, (raw, aux) in enumerate(scenes)],
                                    ['sprite/test/frame.img'], header))
        return path

    def render(self, scenes, options=(), size=(16, 16), profile='dfo'):
        path = self.replay(scenes)
        report, rgba = path.with_suffix('.json'), path.with_suffix('.rgba')
        # The valid snapshot ordinal lets an old validator render successfully
        # while ignoring unknown trailing canvas options: RED is actual 16x16
        # output, rather than a command-line parsing exception.
        command = [str(self.binary), '--consume', str(path), str(report), str(rgba),
                   str(self.assets), str(len(scenes) - 1), '--profile', profile, *options]
        result = subprocess.run(command, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        data = json.loads(report.read_bytes())
        self.assertTrue(data['exact_eof'])
        self.assertEqual(data['fallbacks'], 0)
        pixels = rgba.read_bytes()
        self.assertEqual(len(pixels), size[0] * size[1] * 4,
                         'canvas option must change the actual GPU render target dimensions')
        return pixels, data

    def assert_rectangles(self, pixels, size, rectangles):
        width, height = size
        for y in range(height):
            for x in range(width):
                visible = any(l <= x < r and t <= y < b for l, t, r, b in rectangles)
                actual = tuple(pixels[(y * width + x) * 4:(y * width + x) * 4 + 3])
                self.assertEqual(actual, (255, 0, 0) if visible else (0, 0, 0),
                                 (x, y, rectangles))

    def test_padding_reveals_negative_legacy_coordinates_without_scaling(self):
        draw, aux = legacy(-2, -1)
        pixels, _ = self.render([(draw, aux)], ['--canvas-padding', '4,3,5,6'], (25, 25))
        self.assert_rectangles(pixels, (25, 25), [(2, 2, 6, 6)])

    def test_padding_reveals_right_and_bottom_without_scaling(self):
        draw, aux = draw43(15, 15, 2)
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                pixels, data = self.render([(camera(2) + draw, aux)],
                                           ['--canvas-padding', '4,3,5,6'], (25, 25), profile)
                self.assert_rectangles(pixels, (25, 25), [(19, 18, 23, 22)])
                self.assertEqual(data['camera_updates'], 1)

    def test_factor_and_size_add_centered_pixels_preserving_camera_projection(self):
        draw, aux = draw43(3, 4, 2)
        for options, size, rect in ((['--canvas-scale', '2'], (32, 32), (11, 12, 15, 16)),
                                    (['--canvas-size', '25x23'], (25, 23), (7, 7, 11, 11))):
            with self.subTest(options=options):
                pixels, _ = self.render([(camera(2) + draw, aux)], options, size)
                self.assert_rectangles(pixels, size, [rect])

    def test_missing_camera_keeps_identity_clip_projection(self):
        # Identity projection maps x=1 to original screen x=16. It must not
        # become x=expandedWidth when adding right-side pixels.
        draw, aux = draw43(1, 0, 20, scale=(.25, .25))
        pixels, _ = self.render([(draw, aux)], ['--canvas-padding', '4,3,8,6'], (28, 25))
        self.assert_rectangles(pixels, (28, 25), [(20, 3, 28, 11)])

    def test_finite_world_clip_retains_camera_transform_and_padding_translation(self):
        draw, aux = draw43(2, -1, 2)
        raw = camera(2, y=-4) + clip((-16, -16, 16, 2)) + draw
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                pixels, _ = self.render([(raw, aux)], ['--canvas-padding', '4,3,5,6'], (25, 25), profile)
                self.assert_rectangles(pixels, (25, 25), [(6, 6, 10, 9)])

    def test_large_finite_legacy_clip_stays_visible_after_padding_translation(self):
        # These are legal signed op60 edges, not the special +/-1000000 reset.
        # Previously the GPU clamped them to the viewport. Adding positive
        # padding must not wrap right/bottom into a negative scissor rectangle.
        draw, aux = legacy(-2, -1)
        raw = clip((-2147483648, -2147483648, 2147483647, 2147483647)) + draw
        pixels, _ = self.render([(raw, aux)], ['--canvas-padding', '4,3,5,6'], (25, 25))
        self.assert_rectangles(pixels, (25, 25), [(2, 2, 6, 6)])

    def test_full_unbounded_reset_extends_canvas_but_partial_sentinels_keep_clip(self):
        draw, aux = draw43(-2, -2, 2)
        for rectangle, expected in (((-1000000, -1000000, 1000000, 1000000), (2, 1, 6, 5)),
                                    ((-1000000, -1000000, 1000000, 2), (4, 3, 6, 5))):
            with self.subTest(rectangle=rectangle):
                pixels, _ = self.render([(camera(2) + clip(rectangle) + draw, aux)],
                                        ['--canvas-padding', '4,3,5,6'], (25, 25))
                self.assert_rectangles(pixels, (25, 25), [expected])

    def test_zero_padding_is_byte_identical_to_original_output(self):
        draw, aux = draw43(6, -1, 2)
        raw = camera(2, y=-4, zoom=2) + clip((-16, -16, 16, 2)) + draw
        original, _ = self.render([(raw, aux)])
        expanded, _ = self.render([(raw, aux)], ['--canvas-padding', '0,0,0,0'])
        self.assertEqual(expanded, original)

    def test_grid_and_reflection_apply_canvas_offset_once(self):
        reflection, first = legacy(-2, 0, special=1, pivot=(2, 0))
        grid = op(32, struct.pack('<hhB3hB2hIi', 0, 0, 3, 1, 3, 99, 2, 1, 3, 0, 0))
        stretched, second = legacy(-2, -2, scale=(2, 2))
        for raw, aux, rectangle in ((reflection, first, (2, 3, 6, 7)),
                                     (grid + stretched, second, (2, 1, 10, 9))):
            with self.subTest(rectangle=rectangle):
                pixels, _ = self.render([(raw, aux)], ['--canvas-padding', '4,3,5,6'], (25, 25))
                self.assert_rectangles(pixels, (25, 25), [rectangle])

    def test_local_capture_mask_and_offscreen_sprite_expand_together(self):
        capture = op(58, struct.pack('<Ii7f4B', 0, 0, 1, 1, -2, -1, 0, 0, 0, 0, 0, 0, 0))
        draw, aux = draw43(-4, -3, 0, scale=(3, 3))
        draw = op(46) + draw[4:]
        raw = camera(0) + capture + draw + op(59)
        pixels, _ = self.render([(raw, aux)], ['--canvas-padding', '4,3,5,6'], (25, 25))
        self.assert_rectangles(pixels, (25, 25), [(2, 2, 6, 6)])

    def test_type54_context_mask_uses_expanded_texture_coordinates(self):
        mask, first = legacy(-2, -1)
        draw, second = legacy(-4, -3, scale=(3, 3))
        # Native mode1 masks alpha; mode0/2 preserve source without masking.
        raw = op(38) + mask + op(39) + effect(54, (0, 0, 0, 0, 0, 1, 1)) + draw + op(40)
        pixels, data = self.render([(raw, first + second)], ['--canvas-padding', '4,3,5,6'],
                                   (25, 25), 'dnf-july')
        self.assert_rectangles(pixels, (25, 25), [(2, 2, 6, 6)])
        self.assertEqual(data['context_allocations'], 1)
        self.assertEqual(data['shader_counts'][54], 1)

    def test_captured_canvas_shader_preserves_original_pixels_after_translation(self):
        background, first = legacy(2, 2, scale=(2, 2))
        foreground, second = legacy(3, 3)
        raw = background + effect(29, (.5, .25)) + foreground + op(20)
        original, _ = self.render([(raw, first + second)])
        expanded, _ = self.render([(raw, first + second)], ['--canvas-padding', '4,3,5,6'], (25, 25))
        self.assertTrue(any(original[n] for n in range(0, len(original), 4)))
        for y in range(16):
            for x in range(16):
                before = original[(y * 16 + x) * 4:(y * 16 + x + 1) * 4]
                after = expanded[((y + 3) * 25 + x + 4) * 4:((y + 3) * 25 + x + 5) * 4]
                self.assertEqual(after, before, (x, y))


class CanvasPlaybackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'canvas_20261007' / ('playback_' + uuid.uuid4().hex[:8])
        cls.assets = cls.folder / 'assets'
        cls.folder.mkdir(parents=True)
        shutil.copytree(ROOT / 'assets' / 'shaders', cls.assets / 'shaders')
        pixels = bytes([0, 0, 255, 255]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(pixels),
                                         x=0, y=0, full_w=4, full_h=4) + pixels
        (cls.assets / 'sprite_test.NPK').write_bytes(npk(image))
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        header += b'\x0c' + struct.pack('<H', 6) + bytes(126)
        first, a = draw43(2, 2, 2)
        second, b = draw43(2, -1, 2)
        third, c = draw43(15, 15, 2)
        commands = {0: camera(2) + first, 1: camera(2, y=-4) + second, 2: third}
        cls.replay = cls.folder / 'persistent_camera.rep'
        cls.replay.write_bytes(pack_replay(1.8, dict(sorted(commands.items(), key=lambda item: len(item[1]))),
                                          [(0, (0,), a), (100, (1,), b), (200, (2,), c)],
                                          ['sprite/test/frame.img'], header))
        mask, a = legacy(-2, -1)
        draw, b = legacy(-4, -3, scale=(3, 3))
        later_mask, c = legacy(10, 2)
        commands = {0: op(38) + mask + op(39),
                    1: effect(54, (0, 0, 0, 0, 0, 1, 1)) + draw + op(40)
                       + op(38) + later_mask + op(39)}
        cls.context_replay = cls.folder / 'cached_context.rep'
        cls.context_replay.write_bytes(pack_replay(1.8, commands, [(0, (0,), a), (100, (1,), b + c)],
                                                  ['sprite/test/frame.img'], header))
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        objects = []
        for name in ('protocol', 'resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine'):
            target = cls.folder / (name + '.o')
            shutil.copy2(ROOT / 'build' / (name + '.o'), target)
            objects.append(target)
        probe = cls.folder / 'canvas_playback_native.o'
        options = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
                   '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor/zlib'),
                   '-I', str(ROOT / 'vendor/freetype/include')]
        result = subprocess.run([str(compiler), *options, '-c', str(ROOT / 'tests/canvas_playback_native.cpp'),
                                 '-o', str(probe)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stderr)
        cls.binary = cls.folder / 'canvas_playback_native.exe'
        result = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects), str(probe),
                                 str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
                                 '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-o', str(cls.binary)],
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def test_resize_preserves_navigation_state_hidden_images_and_persistent_dependencies(self):
        for mode in ('paused', 'hidden', 'ended', 'stopped', 'running', 'context'):
            with self.subTest(mode=mode):
                output = self.folder / mode
                output.mkdir()
                # Each probe uses its own cache and evidence paths. Shader files
                # are read-only workspace inputs, copied into the run root.
                shutil.copytree(self.assets / 'shaders', output / 'assets/shaders')
                result = subprocess.run([str(self.binary), str(output),
                                         str(self.context_replay if mode == 'context' else self.replay),
                                         str(self.assets), mode], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                report = json.loads(result.stdout)
                (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
                self.assertEqual((report['width'], report['height']), (25, 25))
                for field in ('state_preserved', 'camera_preserved', 'hidden_preserved', 'reference_pixels_equal'):
                    self.assertTrue(report[field], (mode, field, report))


if __name__ == '__main__':
    unittest.main()
