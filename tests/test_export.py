"""Real GPU and native FFmpeg export, with owned synthetic resources."""
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


class NativeExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'new_ui_export' / ('fixtures_' + uuid.uuid4().hex[:8])
        cls.client = cls.folder / 'client'
        assets = cls.client / 'ImagePacks2'
        assets.mkdir(parents=True)
        raw = bytes([0, 0, 255, 128]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (assets / 'sprite_test.NPK').write_bytes(npk(image, 'sprite/test/frame.img'))
        commands, scenes = {}, []
        for index, (time, x) in enumerate(((0, 2), (10, 4), (20, 6), (100, 8))):
            params = bytearray(_default_draw_params(b''))
            struct.pack_into('<2f', params, 28, 0, 0)
            commands[index] = struct.pack('<II', 3, 64) + params
            scenes.append((time, (index,), struct.pack('<2h', x, 3)))
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        cls.replay = cls.folder / 'fixed.rep'
        cls.replay.write_bytes(pack_replay(1.7, commands, scenes, ['sprite/test/frame.img'], header=header))

    def export(self, format='png', fps=30, hidden=False, cancel=None):
        exe = ROOT / 'build' / 'rep_export.exe'
        self.assertTrue(exe.is_file(), 'native fixed-timeline material export has not been implemented')
        name = self._testMethodName + '_' + format + '_' + str(fps)
        command = [str(exe), '--client', str(self.client), '--replay', str(self.replay),
                   '--format', format, '--fps', str(fps), '--alpha', '1',
                   '--output', str(self.folder), '--name', name]
        if hidden:
            command += ['--hide', 'sprite/test/frame.img']
        if cancel is not None:
            command += ['--cancel-after', str(cancel)]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', cwd=ROOT)
        if cancel is not None:
            return result, self.folder / name
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def probe(self, path):
        result = subprocess.run([str(FFPROBE), '-v', 'error', '-show_streams', '-of', 'json', str(path)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)['streams']

    def test_png_fixed_timeline_preserves_straight_alpha_and_last_frame(self):
        data = self.export()
        self.assertEqual((data['width'], data['height'], data['fps'], data['frames']), (16, 16, 30, 4))
        self.assertEqual(data['duration_ms'], 100)
        folder = Path(data['output'])
        frames = sorted(folder.glob('frame_*.png'))
        self.assertEqual(len(frames), 4)
        first = Image.open(frames[0]).convert('RGBA')
        last = Image.open(frames[-1]).convert('RGBA')
        self.assertEqual(first.getpixel((0, 0)), (0, 0, 0, 0))
        r, g, b, a = first.getpixel((3, 4))
        self.assertGreaterEqual(r, 250, 'RGBA must be straight alpha, not dark premultiplied RGB')
        self.assertEqual((g, b, a), (0, 0, 128))
        self.assertEqual(last.getpixel((3, 4))[3], 0)
        self.assertEqual(last.getpixel((9, 4))[3], 128)

    def test_hidden_path_is_shared_with_export_and_consumes_all_scenes(self):
        data = self.export(hidden=True)
        self.assertEqual(data['executed_scenes'], 4)
        self.assertEqual(data['hidden_images'], 1)
        for frame in Path(data['output']).glob('frame_*.png'):
            self.assertEqual(Image.open(frame).convert('RGBA').getbbox(), None)

    def test_mov_is_decodable_prores_4444_with_real_alpha(self):
        data = self.export('mov', 60)
        streams = self.probe(data['output'])
        self.assertEqual(len(streams), 1)
        stream = streams[0]
        self.assertEqual((stream['codec_name'], stream['profile']), ('prores', '4444'))
        self.assertEqual((stream['width'], stream['height'], stream['r_frame_rate'], int(stream['nb_frames'])),
                         (16, 16, '60/1', 7))
        self.assertTrue(stream['pix_fmt'].startswith('yuva'))
        raw = subprocess.run([str(ROOT/'build'/'ffmpeg.exe'), '-v', 'error', '-i', data['output'],
                              '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgba', 'pipe:1'], capture_output=True)
        self.assertEqual(raw.returncode, 0, raw.stderr)
        self.assertEqual(raw.stdout[3], 0)
        self.assertIn(raw.stdout[(4*16+3)*4+3], range(126, 131))

    def test_mp4_disables_alpha_and_has_no_audio(self):
        data = self.export('mp4', 30)
        self.assertFalse(data['alpha'])
        streams = self.probe(data['output'])
        self.assertEqual(len(streams), 1)
        stream = streams[0]
        self.assertEqual((stream['codec_name'], stream['width'], stream['height'], stream['r_frame_rate']),
                         ('h264', 16, 16, '30/1'))
        raw = subprocess.run([str(ROOT/'build'/'ffmpeg.exe'), '-v', 'error', '-i', data['output'],
                              '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgba', 'pipe:1'], capture_output=True)
        self.assertEqual(raw.returncode, 0, raw.stderr)
        self.assertEqual(set(raw.stdout[3::4]), {255})

    def test_cancel_reports_failure_without_a_completed_artifact(self):
        result, path = self.export(cancel=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('cancelled', result.stderr.lower())
        self.assertFalse(path.exists())


if __name__ == '__main__':
    unittest.main()
