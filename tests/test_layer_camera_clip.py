"""GPU regressions for world clips attached to modern camera-layer draws."""
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
from test_default_layer_camera import draw43

ROOT = Path(__file__).resolve().parents[1]


def camera(layer, x=0, y=0, zoom=1, width=16, height=16):
    return op(50, struct.pack('<2fI3f', x, y, layer, width, height, zoom))


def clip(rectangle):
    return op(60, struct.pack('<4i', *rectangle))


class LayerCameraClipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / ('layer_camera_clip_' + uuid.uuid4().hex[:8])
        cls.assets = cls.folder / 'assets'
        cls.assets.mkdir(parents=True)
        pixels = bytes([0, 0, 255, 255]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(pixels),
                                         x=0, y=0, full_w=4, full_h=4) + pixels
        (cls.assets / 'sprite_test.NPK').write_bytes(npk(image))

    def render(self, scenes, profile):
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        header += b'\x0c' + struct.pack('<H', 6) + bytes(126)
        commands = dict(sorted(enumerate(raw for raw, aux in scenes), key=lambda item: len(item[1])))
        timeline = [(n * 10, (n,), aux) for n, (raw, aux) in enumerate(scenes)]
        path = self.folder / (self._testMethodName + '_' + profile + '.rep')
        path.write_bytes(pack_replay(1.8, commands, timeline, ['sprite/test/frame.img'], header))
        report, rgba = path.with_suffix('.json'), path.with_suffix('.rgba')
        result = subprocess.run([str(ROOT / 'build/rep_gpu.exe'), '--profile', profile,
                                 '--consume', str(path), str(report), str(rgba), str(self.assets)],
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(report.read_bytes())['exact_eof'])
        pixels = rgba.read_bytes()
        self.assertEqual(len(pixels), 16 * 16 * 4)
        return lambda x, y: tuple(pixels[(y * 16 + x) * 4:(y * 16 + x) * 4 + 3])

    def assert_rectangles(self, pixel, rectangles):
        for y in range(16):
            for x in range(16):
                inside = any(l <= x < r and t <= y < b for l, t, r, b in rectangles)
                self.assertEqual(pixel(x, y), (255, 0, 0) if inside else (0, 0, 0),
                                 (x, y, rectangles))

    def test_world_clip_moves_with_each_draw_layer_camera(self):
        # SeaDragonTooth records a world clip near the floor, while its layer2
        # camera has negative Y. A separate layer uses the same raw rectangle.
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                shifted, first = draw43(2, -1, 2)
                fixed, second = draw43(8, 1, 3)
                raw = camera(2, y=-4) + camera(3) + clip((-16, -16, 16, 2)) + shifted + fixed
                pixel = self.render([(raw, first + second)], profile)
                self.assert_rectangles(pixel, [(2, 3, 6, 6), (8, 1, 12, 2)])

    def test_world_clip_uses_persistent_camera_in_later_scene(self):
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                draw, aux = draw43(2, -1, 2)
                raw = clip((-16, -16, 16, 2)) + draw
                pixel = self.render([(camera(2, y=-4) + raw, aux), (raw, aux)], profile)
                self.assert_rectangles(pixel, [(2, 3, 6, 6)])

    def test_world_clip_zoom_and_bypass_follow_native_camera(self):
        # CN 148C2D900: zoom around integer camera half-size after subtracting
        # integer camera position; drawable virtual112 bypasses zoom.
        for profile in ('dfo', 'dnf-july'):
            for bypass in (False, True):
                with self.subTest(profile=profile, bypass=bypass):
                    draw, aux = draw43(6, -1, 2)
                    if bypass:
                        draw = bytearray(draw)
                        draw[8 + 56] = 1
                        draw = bytes(draw)
                    pixel = self.render([(camera(2, y=-4, zoom=2) +
                                          clip((-16, -16, 16, 2)) + draw, aux)], profile)
                    self.assert_rectangles(pixel, [(6, 3, 10, 6)] if bypass else [(4, 0, 12, 4)])

    def test_legacy_pixel_clip_keeps_screen_coordinates(self):
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                draw, aux = legacy(2, 1)
                pixel = self.render([(camera(0, y=-4) + clip((-16, -16, 16, 2)) + draw, aux)], profile)
                self.assert_rectangles(pixel, [(2, 1, 6, 2)])

    def test_zero_origin_keeps_finite_screen_bounds_when_viewport_differs(self):
        # Native 148C2D9D1..DA06 only replaces sentinel edges at zero integer
        # origin. It neither applies zoom nor bounds left/top to camera size;
        # scissor backend 148C65F70 submits these pixel coordinates directly.
        for profile in ('dfo', 'dnf-july'):
            for zoom in (1, 2):
                with self.subTest(profile=profile, zoom=zoom):
                    draw, aux = draw43(4, 1, 2)
                    raw = camera(2, width=8, zoom=zoom) + clip((10, 0, 12, 16)) + draw
                    pixel = self.render([(raw, aux)], profile)
                    self.assert_rectangles(pixel, [(10, 1, 12, 5)] if zoom == 1 else [(10, 0, 12, 2)])

    def test_partial_sentinels_and_full_reset_keep_unbounded_edges(self):
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                first, a = draw43(2, -1, 2)
                second, b = draw43(8, -1, 2)
                raw = camera(2, y=-4) + clip((-1000000, -1000000, 1000000, 2)) + first
                raw += clip((-1000000, -1000000, 1000000, 1000000)) + second
                pixel = self.render([(raw, a + b)], profile)
                self.assert_rectangles(pixel, [(2, 3, 6, 6), (8, 3, 12, 7)])


if __name__ == '__main__':
    unittest.main()
