"""Expanded exports preserve original pixels and show all four outside edges."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay
from test_npk_reader import npk, img_header, rec
from rep_protocol import _default_draw_params
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
FFPROBE = Path(r'D:\ffmpeg\ffprobe.exe')


class CanvasExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'canvas_expansion' / ('export_' + uuid.uuid4().hex[:8])
        cls.client = cls.folder / 'client'
        assets = cls.client / 'ImagePacks2'
        assets.mkdir(parents=True)
        raw = bytes([0, 0, 255, 128]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw),
                                         x=0, y=0, full_w=4, full_h=4) + raw
        (assets / 'sprite_test.NPK').write_bytes(npk(image, 'sprite/test/frame.img'))
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        cls.positions = [(-2, 3), (14, 3), (4, -2), (4, 14)]
        aux = b''.join(struct.pack('<2h', x, y) for x, y in cls.positions)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        cls.replay = cls.folder / 'outside.rep'
        cls.replay.write_bytes(pack_replay(1.7, {0: draw * len(cls.positions)},
                                          [(0, (0,), aux), (100, (0,), aux)],
                                          ['sprite/test/frame.img'], header=header))

    def invoke(self, *canvas, format='png', label='', extra=()):
        command = [str(ROOT / 'build' / 'rep_export.exe'), '--client', str(self.client),
                   '--replay', str(self.replay), '--format', format, '--fps', '30',
                   '--alpha', '1', '--output', str(self.folder),
                   '--name', self._testMethodName + label, *canvas, *extra]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8',
                                cwd=ROOT, timeout=30)
        return result

    def export(self, *canvas, **options):
        result = self.invoke(*canvas, **options)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def assert_expanded_png(self, option, value, size, origin):
        original = self.export(label='_original')
        expanded = self.export(option, value, label='_expanded')
        self.assertEqual((expanded['width'], expanded['height']), size)
        self.assertEqual((expanded['frames'], expanded['executed_scenes']), (4, 2))
        original_frames = sorted(Path(original['output']).glob('frame_*.png'))
        expanded_frames = sorted(Path(expanded['output']).glob('frame_*.png'))
        self.assertEqual(len(expanded_frames), 4)
        left, top = origin
        for original_path, expanded_path in zip(original_frames, expanded_frames):
            old = Image.open(original_path).convert('RGBA')
            new = Image.open(expanded_path).convert('RGBA')
            self.assertEqual(new.size, size)
            self.assertEqual(new.crop((left, top, left + 16, top + 16)).tobytes(), old.tobytes())
            for y in range(size[1]):
                for x in range(size[0]):
                    drawn = any(px + left <= x < px + left + 4 and
                                py + top <= y < py + top + 4 for px, py in self.positions)
                    expected = (255, 0, 0, 128) if drawn else (0, 0, 0, 0)
                    self.assertEqual(new.getpixel((x, y)), expected, (x, y, expanded_path.name))

    def test_scale_expands_canvas_without_enlarging_sprite_pixels(self):
        self.assert_expanded_png('--canvas-scale', '2', (32, 32), (8, 8))

    def test_size_centers_original_canvas(self):
        self.assert_expanded_png('--canvas-size', '30x24', (30, 24), (7, 4))

    def test_padding_keeps_requested_asymmetric_origin(self):
        self.assert_expanded_png('--canvas-padding', '5,7,9,11', (30, 34), (5, 7))

    def test_saved_size_smaller_than_rep_keeps_original_bounds(self):
        original = self.export(label='_original')
        resized = self.export('--canvas-size', '15x16', label='_smaller')
        self.assertEqual((resized['width'], resized['height']), (16, 16))
        original_frames = sorted(Path(original['output']).glob('frame_*.png'))
        resized_frames = sorted(Path(resized['output']).glob('frame_*.png'))
        for original_path, resized_path in zip(original_frames, resized_frames):
            self.assertEqual(Image.open(original_path).convert('RGBA').tobytes(),
                             Image.open(resized_path).convert('RGBA').tobytes())

    def test_expanded_hidden_export_still_consumes_scenes(self):
        data = self.export('--canvas-padding', '5,7,9,11',
                           extra=('--hide', 'sprite/test/frame.img'))
        self.assertEqual((data['width'], data['height'], data['executed_scenes']), (30, 34, 2))
        self.assertEqual(data['hidden_images'], 1)
        for frame in Path(data['output']).glob('frame_*.png'):
            self.assertEqual(Image.open(frame).convert('RGBA').getbbox(), None)

    def probe(self, path):
        result = subprocess.run([str(FFPROBE), '-v', 'error', '-show_streams', '-of', 'json', str(path)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)['streams']

    def decode(self, path):
        result = subprocess.run([str(ROOT / 'build' / 'ffmpeg.exe'), '-v', 'error', '-i', str(path),
                                 '-f', 'rawvideo', '-pix_fmt', 'rgba', 'pipe:1'],
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_mov_encoder_uses_expanded_size_and_retains_alpha(self):
        data = self.export('--canvas-scale', '2', format='mov')
        stream, = self.probe(data['output'])
        self.assertEqual((stream['width'], stream['height'], int(stream['nb_frames'])), (32, 32, 4))
        self.assertTrue(stream['pix_fmt'].startswith('yuva'))
        decoded = self.decode(data['output'])
        self.assertEqual(len(decoded), 32 * 32 * 4 * 4)
        self.assertEqual(decoded[3], 0)
        self.assertIn(decoded[((11 * 32 + 6) * 4) + 3], range(126, 131))

    def test_mp4_encoder_uses_odd_expanded_size_with_opaque_frames(self):
        data = self.export('--canvas-padding', '3,4,6,7', format='mp4')
        self.assertFalse(data['alpha'])
        stream, = self.probe(data['output'])
        self.assertEqual((stream['width'], stream['height'], int(stream['nb_frames'])), (25, 27, 4))
        decoded = self.decode(data['output'])
        self.assertEqual(len(decoded), 25 * 27 * 4 * 4)
        self.assertEqual(set(decoded[3::4]), {255})

    def test_invalid_canvas_values_are_rejected(self):
        for option, value in (('--canvas-scale', '0'), ('--canvas-scale', '2junk'),
                              ('--canvas-size', '0x16'), ('--canvas-size', '30x24junk'),
                              ('--canvas-padding', '-1,0,0,0'), ('--canvas-padding', '1,2,3')):
            with self.subTest(option=option, value=value):
                result = self.invoke(option, value)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('canvas', result.stderr.lower())


if __name__ == '__main__':
    unittest.main()
