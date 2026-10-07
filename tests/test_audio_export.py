"""Recorded REP sound is mixed into native MOV/MP4 exports, never PNG."""
import array
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid
import wave

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay
from test_npk_reader import npk, img_header, rec
from rep_protocol import _default_draw_params

ROOT = Path(__file__).resolve().parents[1]
FFPROBE = Path(r'D:\ffmpeg\ffprobe.exe')


class NativeAudioExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'audio_export' / uuid.uuid4().hex[:10]
        cls.client = cls.folder / 'client'
        assets = cls.client / 'ImagePacks2'
        assets.mkdir(parents=True)
        raw = bytes([0, 0, 255, 128]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (assets / 'sprite_test.NPK').write_bytes(npk(image, 'sprite/test/frame.img'))
        music = cls.client / 'music'
        music.mkdir()
        with wave.open(str(music / 'tone.wav'), 'wb') as output:
            output.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
            output.writeframes(b''.join(struct.pack('<h', int(16000 * math.sin(2 * math.pi * 440 * n / 48000))) for n in range(4800)))
        (cls.client / 'audio.xml').write_text('<AUDIO><MUSIC ID="TEST_TONE" FILE="music/tone.wav" /></AUDIO>', encoding='utf8')
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<I', params, 0, 1)
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        sound = struct.pack('<II9i', 6, 36, 0, 1, 0, 1, 1, 0, 0, -1, -1)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        cls.replay = cls.folder / 'tone.rep'
        cls.replay.write_bytes(pack_replay(1.7, {0: sound, 1: draw},
            [(0, (0, 1), struct.pack('<2h', 2, 3)), (500, (1,), struct.pack('<2h', 4, 3))],
            ['TEST_TONE', 'sprite/test/frame.img'], header=header))

    def export(self, format='mov', audio=None, cancel=None, client=None):
        name = self._testMethodName + '_' + format
        command = [str(ROOT / 'build' / 'rep_export.exe'), '--client', str(client or self.client),
                   '--replay', str(self.replay), '--format', format, '--fps', '30', '--alpha', '1',
                   '--output', str(self.folder), '--name', name]
        if audio is not None:
            command += ['--audio', '1' if audio else '0']
        if cancel:
            command += ['--cancel-after', str(cancel)]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf8', timeout=40)
        if cancel:
            return result, name
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def streams(self, path):
        result = subprocess.run([str(FFPROBE), '-v', 'error', '-show_streams', '-of', 'json', str(path)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)['streams']

    def audio(self, path):
        result = subprocess.run([str(ROOT / 'build' / 'ffmpeg.exe'), '-v', 'error', '-i', str(path),
                                 '-map', '0:a:0', '-f', 'f32le', '-ac', '2', '-ar', '48000', 'pipe:1'],
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        samples = array.array('f')
        samples.frombytes(result.stdout)
        return samples

    def test_mov_contains_pcm_and_pads_to_the_video_duration(self):
        result = self.export()
        streams = self.streams(result['output'])
        audio = next((s for s in streams if s['codec_type'] == 'audio'), None)
        self.assertIsNotNone(audio, 'MOV export drops recorded REP audio')
        self.assertEqual((audio['codec_name'], audio['sample_rate'], audio['channels']), ('pcm_s16le', '48000', 2))
        samples = self.audio(result['output'])
        self.assertEqual(len(samples), result['frames'] * 48000 // result['fps'] * 2)
        self.assertGreater(max(abs(x) for x in samples[:4800]), .1)
        self.assertEqual(max(abs(x) for x in samples[24000:]), 0)
        self.assertTrue(result['audio'])
        self.assertEqual(result['missing_sounds'], 0)
        self.assertFalse(list(self.folder.glob('*.audio-*.wav')))

    def test_mp4_contains_aac_with_audible_recorded_sound(self):
        result = self.export('mp4')
        audio = next((s for s in self.streams(result['output']) if s['codec_type'] == 'audio'), None)
        self.assertIsNotNone(audio, 'MP4 export drops recorded REP audio')
        self.assertEqual((audio['codec_name'], audio['sample_rate'], audio['channels']), ('aac', '48000', 2))
        self.assertAlmostEqual(float(audio['duration']), result['frames'] / result['fps'], delta=.002)
        self.assertGreater(max(abs(x) for x in self.audio(result['output'])[:4800]), .1)

    def test_audio_can_be_disabled_without_changing_video_frames(self):
        result = self.export('mov', audio=False)
        self.assertEqual([s['codec_type'] for s in self.streams(result['output'])], ['video'])
        self.assertFalse(result['audio'])
        self.assertEqual(result['frames'], 16)

    def test_png_audio_option_never_creates_a_sound_artifact(self):
        result = self.export('png', audio=True)
        self.assertFalse(result['audio'])
        self.assertEqual(len(list(Path(result['output']).glob('frame_*.png'))), 16)
        self.assertFalse(list(Path(result['output']).glob('*.wav')))
        self.assertFalse(list(self.folder.glob('*.audio-*.wav')))

    def test_missing_sound_is_reported_without_failing_export(self):
        client = self.folder / 'missing-client'
        (client / 'ImagePacks2').mkdir(parents=True)
        (client / 'ImagePacks2' / 'sprite_test.NPK').write_bytes((self.client / 'ImagePacks2' / 'sprite_test.NPK').read_bytes())
        (client / 'audio.xml').write_bytes((self.client / 'audio.xml').read_bytes())
        result = self.export('mp4', client=client)
        self.assertIn('missing_sounds', result, 'export does not report missing sound resources')
        self.assertGreater(result['missing_sounds'], 0)
        self.assertTrue(result['audio'])
        self.assertFalse(any(abs(x) > .001 for x in self.audio(result['output'])))

    def test_cancel_removes_only_its_owned_temporary_audio_file(self):
        preserved = self.folder / 'preserve.audio-existing.wav'
        preserved.write_bytes(b'existing audio stays unchanged')
        result, name = self.export('mp4', cancel=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('cancelled', result.stderr.lower())
        self.assertFalse((self.folder / (name + '.mp4')).exists())
        self.assertEqual(preserved.read_bytes(), b'existing audio stays unchanged')
        self.assertEqual(list(self.folder.glob('*.audio-*.wav')), [preserved])


if __name__ == '__main__':
    unittest.main()
