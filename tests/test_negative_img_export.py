"""Real GPU/encoder regression for the IMG offset switch, in an isolated module."""
import json
from pathlib import Path
import shutil
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
SIZE = (16, 16)
OFFSETS = {'x': (-2, 0), 'y': (0, -2), 'both': (-2, -2),
           'zero': (0, 0), 'positive': (2, 3)}


class NegativeImgExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = (ROOT / 'validation' / 'negative_img_offsets_20261010' /
                      ('export_' + uuid.uuid4().hex[:8]))
        cls.module = cls.folder / 'module'
        cls.build = cls.module / 'build'
        cls.build.mkdir(parents=True)
        for name in ('rep_export.exe', 'ffmpeg.exe'):
            shutil.copy2(ROOT / 'build' / name, cls.build / name)
        # Match the source-build module-root detection used by runtime_paths.hpp.
        shutil.copy2(ROOT / 'build.ps1', cls.module / 'build.ps1')
        shutil.copytree(ROOT / 'assets' / 'shaders', cls.module / 'assets' / 'shaders')
        cls.client = cls.module / 'client'
        assets = cls.client / 'ImagePacks2'
        assets.mkdir(parents=True)
        cls.exports = cls.module / 'exports'
        cls.replays = {}
        cls.results = []
        raw = bytes([0, 0, 255, 128]) * 16
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        for name, (x, y) in OFFSETS.items():
            logical = 'sprite/negativeexport/' + name + '/frame.img'
            image = (img_header(2, 36, 1) +
                     rec(16, 5, 4, 4, len(raw), x=x, y=y, full_w=4, full_h=4) + raw)
            (assets / ('sprite_negativeexport_' + name + '.NPK')).write_bytes(npk(image, logical))
            replay = cls.module / (name + '.rep')
            aux = struct.pack('<2h', 6, 6)
            replay.write_bytes(pack_replay(1.7, {0: draw}, [(0, (0,), aux), (100, (0,), aux)],
                                          [logical], header=header))
            cls.replays[name] = replay

    @classmethod
    def tearDownClass(cls):
        (cls.folder / 'export_results.json').write_text(
            json.dumps(cls.results, indent=2), encoding='utf-8')

    def export(self, resource, *, enabled=None, format='png'):
        state = 'default' if enabled is None else 'on' if enabled else 'off'
        name = self._testMethodName + '_' + resource + '_' + state + '_' + format
        command = [str(self.build / 'rep_export.exe'), '--client', str(self.client),
                   '--replay', str(self.replays[resource]), '--format', format,
                   '--fps', '30', '--alpha', '1', '--audio', '0',
                   '--output', str(self.exports), '--name', name]
        if enabled is not None:
            command += ['--negative-img-offsets', '1' if enabled else '0']
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding='utf-8', cwd=self.module, timeout=30)
        evidence = {'test': self._testMethodName, 'resource': resource, 'enabled': enabled,
                    'format': format, 'returncode': result.returncode,
                    'stdout': result.stdout, 'stderr': result.stderr}
        self.results.append(evidence)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual((data['width'], data['height'], data['frames'], data['executed_scenes']),
                         (16, 16, 4, 2), 'IMG offsets must not expand the export canvas')
        self.assertFalse(data['audio'])
        self.assertTrue(Path(data['output']).is_relative_to(self.exports))
        return data

    def assert_png(self, data, resource, *, visible):
        frames = sorted(Path(data['output']).glob('frame_*.png'))
        self.assertEqual(len(frames), 4)
        x0, y0 = OFFSETS[resource]
        x0 += 6
        y0 += 6
        for path in frames:
            with Image.open(path) as image:
                rgba = image.convert('RGBA')
            self.assertEqual(rgba.size, SIZE)
            for y in range(16):
                for x in range(16):
                    drawn = visible and x0 <= x < x0 + 4 and y0 <= y < y0 + 4
                    expected = (255, 0, 0, 128) if drawn else (0, 0, 0, 0)
                    self.assertEqual(rgba.getpixel((x, y)), expected,
                                     (resource, visible, x, y, path.name))

    def test_negative_axes_are_transparent_by_default(self):
        # No switch argument: this is the pixel RED against the old executable.
        for resource in ('x', 'y', 'both'):
            with self.subTest(resource=resource):
                self.assert_png(self.export(resource), resource, visible=False)

    def test_explicit_off_keeps_negative_img_transparent(self):
        self.assert_png(self.export('x', enabled=False), 'x', visible=False)

    def test_on_restores_signed_img_pixels_for_all_negative_axes(self):
        for resource in ('x', 'y', 'both'):
            with self.subTest(resource=resource):
                self.assert_png(self.export(resource, enabled=True), resource, visible=True)

    def test_zero_and_positive_img_offsets_still_draw(self):
        for resource in ('zero', 'positive'):
            for enabled in (None, True):
                with self.subTest(resource=resource, enabled=enabled):
                    self.assert_png(self.export(resource, enabled=enabled), resource, visible=True)

    def decode(self, data):
        probe = subprocess.run([str(FFPROBE), '-v', 'error', '-show_streams', '-of', 'json',
                                data['output']], capture_output=True, text=True, timeout=20)
        self.assertEqual(probe.returncode, 0, probe.stderr)
        stream, = json.loads(probe.stdout)['streams']
        self.assertEqual((stream['width'], stream['height'], int(stream['nb_frames'])), (16, 16, 4))
        decoded = subprocess.run([str(self.build / 'ffmpeg.exe'), '-v', 'error',
                                  '-i', data['output'], '-f', 'rawvideo', '-pix_fmt', 'rgba',
                                  'pipe:1'], capture_output=True, cwd=self.module, timeout=20)
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertEqual(len(decoded.stdout), 16 * 16 * 4 * 4)
        return stream, decoded.stdout

    def test_mov_switch_controls_pixels_and_preserves_alpha(self):
        for enabled in (None, True):
            with self.subTest(enabled=enabled):
                data = self.export('x', enabled=enabled, format='mov')
                self.assertTrue(data['alpha'])
                stream, rgba = self.decode(data)
                self.assertEqual((stream['codec_name'], stream['profile']), ('prores', '4444'))
                self.assertTrue(stream['pix_fmt'].startswith('yuva'))
                if enabled:
                    for frame in range(4):
                        offset = frame * 16 * 16 * 4 + (7 * 16 + 5) * 4
                        self.assertGreaterEqual(rgba[offset], 245)
                        self.assertIn(rgba[offset + 3], range(126, 131))
                else:
                    self.assertEqual(set(rgba[3::4]), {0})

    def test_mp4_switch_controls_pixels_without_alpha_or_canvas_growth(self):
        for enabled in (None, True):
            with self.subTest(enabled=enabled):
                data = self.export('x', enabled=enabled, format='mp4')
                self.assertFalse(data['alpha'])
                stream, rgba = self.decode(data)
                self.assertEqual(stream['codec_name'], 'h264')
                self.assertEqual(set(rgba[3::4]), {255})
                if enabled:
                    for frame in range(4):
                        offset = frame * 16 * 16 * 4 + (7 * 16 + 5) * 4
                        self.assertGreater(rgba[offset], 80)
                else:
                    self.assertLessEqual(max(rgba[0::4]), 3)
                    self.assertLessEqual(max(rgba[1::4]), 3)
                    self.assertLessEqual(max(rgba[2::4]), 3)


if __name__ == '__main__':
    unittest.main()
