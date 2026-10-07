"""Muted real-device/render-thread audio control contracts in an owned window."""
import json
from pathlib import Path
import shutil
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


class AudioControllerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/audio_20261007' / ('controller_' + uuid.uuid4().hex[:8])
        cls.client = cls.folder / 'client'
        assets = cls.client / 'ImagePacks2'
        assets.mkdir(parents=True)
        raw = bytes((0, 0, 255, 255)) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (assets / 'sprite_test.NPK').write_bytes(npk(image, 'sprite/test/frame.img'))
        music = cls.client / 'Music'
        music.mkdir()
        with wave.open(str(music / 'tone.wav'), 'wb') as output:
            output.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            output.writeframes(struct.pack('<2h', 4096, 4096) * 48000)
        (cls.client / 'audio.xml').write_text('<AUDIO><EFFECT ID="TONE" FILE="Music/tone.wav" LOOP_DELAY="0"/></AUDIO>', encoding='utf8')
        cls.silent_client = cls.folder / 'silent-client'
        shutil.copytree(assets, cls.silent_client / 'ImagePacks2')
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<I', params, 0, 1)
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        sound = struct.pack('<II9i', 6, 36, 0, 0, 0, 7, 1, 0, 0, -1, -1)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        scenes = [(n, (0, 1) if n in (0, 500) else (1,), struct.pack('<2h', 2, 3)) for n in range(0, 1501, 100)]
        cls.replay = cls.folder / 'tone.rep'
        (cls.folder / 'broken.rep').write_bytes(b'broken REP fixture')
        cls.replay.write_bytes(pack_replay(1.7, {0: sound, 1: draw}, scenes,
            ['TONE', 'sprite/test/frame.img'], header=header))
        cls.silent = cls.folder / 'silent.rep'
        cls.silent.write_bytes(pack_replay(1.7, {1: draw}, [(n, (1,), struct.pack('<2h', 2, 3)) for n in range(0, 1501, 100)],
            ['TONE', 'sprite/test/frame.img'], header=header))
        cls.heavy = cls.folder / 'heavy.rep'
        heavy_params = params.copy()
        struct.pack_into('<2f', heavy_params, 20, 256, 256)
        heavy_draw = struct.pack('<II', 3, 64) + heavy_params
        heavy_scenes = [(n, ((0,) if n == 0 else ()) + (1,) * 6000,
                         struct.pack('<2h', 2, 3) * 6000) for n in range(0, 1501, 5)]
        cls.heavy.write_bytes(pack_replay(1.7, {0: sound, 1: heavy_draw}, heavy_scenes,
            ['TONE', 'sprite/test/frame.img'], header=header))
        cls.exe = cls.folder / 'audio_controller_probe.exe'
        shutil.copy2(ROOT / 'build/ffmpeg.exe', cls.folder / 'ffmpeg.exe')
        subprocess.run(['powershell', '-NoProfile', '-Command',
                        f"New-Item -ItemType Junction -Path '{cls.folder / 'assets'}' -Target '{ROOT / 'assets'}' | Out-Null"],
                       capture_output=True, check=True)
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        flags = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
                 '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor/zlib'), '-I', str(ROOT / 'vendor/freetype/include')]
        objects = []
        for name in ('resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine'):
            destination = cls.folder / (name + '.o')
            shutil.copy2(ROOT / 'build' / (name + '.o'), destination)
            objects.append(destination)
        for name, source in [('protocol', ROOT / 'src/protocol.cpp'), ('worker', ROOT / 'src/ui_playback.cpp'),
                             ('audio', ROOT / 'src/audio.cpp'), ('audio_resources', ROOT / 'src/audio_resources.cpp'),
                             ('harness', ROOT / 'tests/audio_controller_native.cpp')]:
            destination = cls.folder / (name + '.o')
            result = subprocess.run([str(compiler), *flags, '-c', str(source), '-o', str(destination)],
                                    capture_output=True, text=True, timeout=120)
            if result.returncode:
                raise RuntimeError(result.stderr)
            objects.append(destination)
        result = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects),
            str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
            '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lole32', '-o', str(cls.exe)],
            capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def result(self, mode, replay=None):
        result = subprocess.run([str(self.exe), mode, str(self.folder), str(self.client),
                                 str(replay or self.replay), str(self.silent_client), str(self.silent)],
                                capture_output=True, text=True, encoding='utf8', timeout=35)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        (self.folder / (mode + '.json')).write_text(json.dumps(report, indent=2), encoding='utf8')
        self.assertTrue(report['implemented'], 'PlaybackController does not connect REP audio and controls')
        return report

    def test_pause_step_resume_refresh_replay_stop_and_client_switch(self):
        report = self.result('transitions')
        for field in ('pause_stable', 'refresh_stable', 'sync', 'settings_survive'):
            self.assertTrue(report[field], str(report))
        self.assertFalse(report['switched_available'])
        self.assertEqual((report['switched_events'], report['switched_missing']), (0, 0))

    def test_end_keeps_final_picture_and_stops_the_audio_clock(self):
        report = self.result('end')
        self.assertTrue(report['ended'])
        self.assertTrue(report['stable'])
        self.assertAlmostEqual(report['position'], report['duration'], delta=12)

    def test_no_audio_recording_needs_no_registry_or_output_device(self):
        report = self.result('silent', self.silent)
        self.assertFalse(report['available'])
        self.assertTrue(report['message_empty'])
        self.assertTrue(report['playing'])
        self.assertEqual((report['events'], report['missing']), (0, 0))

    def test_missing_recorded_registry_is_nonfatal_and_visible(self):
        report = self.result('missing')
        self.assertTrue(report['playing'])
        self.assertGreater(report['missing'], 0)
        self.assertFalse(report['message_empty'])
        self.assertEqual(report['events'], 2)

    def test_failed_replay_switch_releases_audio_and_clears_audio_status(self):
        report = self.result('error')
        self.assertTrue(report['error'])
        self.assertFalse(report['available'])
        self.assertEqual((report['events'], report['position']), (0, 0))

    def test_playing_canvas_rebuild_preserves_audio_visual_clock_alignment(self):
        report = self.result('canvas-active', self.heavy)
        self.assertTrue(report['playing'], str(report))
        self.assertGreater(report['rebuild_ms'], 100, 'fixture did not exercise an expensive real GPU rebuild')
        self.assertLessEqual(report['difference'], 35, 'audio kept advancing while canvas rebuild preserved the REP time: ' + str(report))


if __name__ == '__main__':
    unittest.main()
