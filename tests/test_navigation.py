"""Exact, cancellable navigation on independent streams and an owned muted device."""
import json
import os
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


class NavigationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/navigation_20261011' / uuid.uuid4().hex[:10]
        cls.client = cls.folder / 'client'
        packs = cls.client / 'ImagePacks2'
        packs.mkdir(parents=True)
        raw = bytes((0, 0, 255, 255)) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (packs / 'sprite_test.NPK').write_bytes(npk(image, 'sprite/test/frame.img'))
        music = cls.client / 'Music'
        music.mkdir()
        with wave.open(str(music / 'tone.wav'), 'wb') as out:
            out.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            out.writeframes(struct.pack('<2h', 4096, 4096) * 48000)
        (cls.client / 'audio.xml').write_text('<AUDIO><EFFECT ID="TONE" FILE="Music/tone.wav" LOOP_DELAY="0"/></AUDIO>', encoding='utf8')
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<I', params, 0, 1)
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        sound = struct.pack('<II9i', 6, 36, 0, 0, 0, 7, 1, 0, 0, -1, -1)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        cls.short = cls.folder / 'short.rep'
        cls.short.write_bytes(pack_replay(1.7, {0: sound, 1: draw},
            [(t, (0, 1) if n == 0 else (1,), struct.pack('<2h', n, 3)) for n, t in enumerate((0, 10, 10, 30, 50, 50))],
            ['TONE', 'sprite/test/frame.img'], header=header))
        cls.heavy = cls.folder / 'heavy.rep'
        cls.heavy.write_bytes(pack_replay(1.7, {0: sound, 1: draw},
            [(t, ((0,) if t == 0 else ()) + (1,) * 4000, struct.pack('<2h', 2, 3) * 4000) for t in range(0, 1001, 10)],
            ['TONE', 'sprite/test/frame.img'], header=header))
        shutil.copy2(ROOT / 'build/ffmpeg.exe', cls.folder / 'ffmpeg.exe')
        subprocess.run(['powershell', '-NoProfile', '-Command',
            f"New-Item -ItemType Junction -Path '{cls.folder / 'assets'}' -Target '{ROOT / 'assets'}' | Out-Null"], capture_output=True, check=True)
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        flags = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
            '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor/zlib'), '-I', str(ROOT / 'vendor/freetype/include')]
        environment = dict(os.environ, TEMP=str(cls.folder), TMP=str(cls.folder))
        objects = []
        for name in ('protocol', 'resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine', 'audio', 'audio_resources', 'ui_playback', 'harness'):
            source = ROOT / ('tests/navigation_native.cpp' if name == 'harness' else 'src/' + name + '.cpp')
            obj = cls.folder / (name + '.o')
            result = subprocess.run([str(compiler), *flags, '-c', str(source), '-o', str(obj)], capture_output=True, text=True, timeout=120, env=environment)
            if result.returncode:
                raise RuntimeError(result.stderr)
            objects.append(obj)
        cls.exe = cls.folder / 'navigation_probe.exe'
        result = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects),
            str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
            '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lole32', '-o', str(cls.exe)],
            capture_output=True, text=True, timeout=120, env=environment)
        if result.returncode:
            raise RuntimeError(result.stderr)
        print('navigation validation: ' + str(cls.folder), flush=True)

    def result(self, mode, heavy=False):
        result = subprocess.run([str(self.exe), mode, str(self.folder), str(self.client), str(self.heavy if heavy else self.short)],
            capture_output=True, text=True, encoding='utf8', timeout=35)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        (self.folder / (mode + '.json')).write_text(json.dumps(report, indent=2), encoding='utf8')
        self.assertTrue(report['implemented'], 'exact cancellable navigation is absent')
        return report

    def test_engine_reuses_exact_forward_state_and_invalidates_cancelled_history(self):
        report = self.result('engine')
        self.assertEqual((report['first'], report['timestamp']), (3, 30))
        for name in ('reused', 'endpoint', 'cancelled', 'rebuilt_after_cancel', 'accurate'):
            self.assertTrue(report[name], str(report))

    def test_scrub_freezes_clock_restores_playing_and_waits_for_audio(self):
        report = self.result('scrub')
        self.assertEqual(report['paused_ordinal'], 3)
        self.assertEqual(report['endpoint_ordinal'], 5)
        for name in ('frozen', 'sync', 'repeat_bounded', 'audio_confirmed'):
            self.assertTrue(report[name], str(report))

    def test_latest_seek_and_stop_interrupt_a_real_rebuild(self):
        report = self.result('cancel', True)
        self.assertTrue(report['latest'], str(report))
        self.assertTrue(report['stop_wins'], str(report))


if __name__ == '__main__':
    unittest.main()
