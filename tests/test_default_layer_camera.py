"""Real GPU regressions for a modern layer without an opcode50 camera."""
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


def camera(layer, x=0, y=0):
    return op(50, struct.pack('<2fI3f', x, y, layer, 16, 16, 1))


def draw43(x, y, layer, scale=(1, 1)):
    # The user's v1.8/minor6 records the complete native60 structure.
    params = bytearray(60)
    struct.pack_into('<I', params, 4, layer)
    struct.pack_into('<2f', params, 16, *scale)
    struct.pack_into('<I', params, 28, 0xffffffff)
    struct.pack_into('<2f', params, 48, 1, 1)
    return op(43, struct.pack('<I', 0) + params), struct.pack('<2h', x, y)


class DefaultLayerCameraTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / ('default_camera_' + uuid.uuid4().hex[:8])
        cls.assets = cls.folder / 'assets'
        cls.assets.mkdir(parents=True)
        raw = bytes([0, 0, 255, 255]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw),
                                         x=0, y=0, full_w=4, full_h=4) + raw
        (cls.assets / 'sprite_test.NPK').write_bytes(npk(image))

    def render(self, scenes, profile):
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        header += b'\x0c' + struct.pack('<H', 6) + bytes(126)
        commands = {n: raw for n, (raw, aux) in enumerate(scenes)}
        # REP dictionary lengths must be nondecreasing.
        commands = dict(sorted(commands.items(), key=lambda item: len(item[1])))
        timeline = [(n * 10, (n,), aux) for n, (raw, aux) in enumerate(scenes)]
        path = self.folder / (self._testMethodName + '_' + profile + '.rep')
        path.write_bytes(pack_replay(1.8, commands, timeline,
                                    ['sprite/test/frame.img'], header=header))
        report, rgba = path.with_suffix('.json'), path.with_suffix('.rgba')
        result = subprocess.run([str(ROOT / 'build' / 'rep_gpu.exe'), '--consume',
                                 str(path), str(report), str(rgba), str(self.assets),
                                 '--profile', profile], capture_output=True, text=True,
                                timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(report.read_text())['exact_eof'])
        pixels = rgba.read_bytes()
        self.assertEqual(len(pixels), 16 * 16 * 4)
        return lambda x, y: tuple(pixels[(y * 16 + x) * 4:(y * 16 + x) * 4 + 3])

    def assert_rectangle(self, pixel, rectangle):
        left, top, right, bottom = rectangle
        for y in range(16):
            for x in range(16):
                expected = (255, 0, 0) if left <= x < right and top <= y < bottom else (0, 0, 0)
                self.assertEqual(pixel(x, y), expected, (x, y, rectangle))

    def test_unconfigured_duplicate_layer_does_not_draw_in_pixel_coordinates(self):
        # ImperialKnight records the same actor on configured layer2 and
        # unconfigured layer20. Identity camera matrices place (4,4) outside
        # clip space; treating it as raw pixels incorrectly adds a second actor.
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                first, a = draw43(4, 4, layer=2)
                duplicate, b = draw43(4, 4, layer=20)
                pixel = self.render([(camera(2, 4, 4) + first + duplicate, a + b)], profile)
                self.assert_rectangle(pixel, (0, 0, 4, 4))

    def test_configured_layer20_keeps_its_camera_and_persists_between_scenes(self):
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                draw, aux = draw43(6, 6, layer=20)
                pixel = self.render([(camera(20, 4, 4) + draw, aux), (draw, aux)], profile)
                self.assert_rectangle(pixel, (2, 2, 6, 6))

    def test_unconfigured_layer_still_draws_inside_identity_clip_space(self):
        # The default camera is a real identity projection, not a disabled
        # layer. Clip [0,1]^2 maps to the viewport's upper right quarter.
        for profile in ('dfo', 'dnf-july'):
            for layer in (20, 35):
                with self.subTest(profile=profile, layer=layer):
                    draw, aux = draw43(0, 0, layer=layer, scale=(.25, .25))
                    pixel = self.render([(draw, aux)], profile)
                    self.assert_rectangle(pixel, (8, 0, 16, 8))

    def test_legacy_draw_keeps_pixel_coordinates_without_a_layer_camera(self):
        for profile in ('dfo', 'dnf-july'):
            with self.subTest(profile=profile):
                draw, aux = legacy(4, 4)
                pixel = self.render([(draw, aux)], profile)
                self.assert_rectangle(pixel, (4, 4, 8, 8))


if __name__ == '__main__':
    unittest.main()
