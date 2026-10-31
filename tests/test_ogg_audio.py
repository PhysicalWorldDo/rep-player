"""OGG call inventory, resource muting and one-shot audition contracts."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import wave

sys.dont_write_bytecode = True
from ogg_fixtures import pack_replay

ROOT = Path(__file__).resolve().parents[1]


def play(resource, delay=0, slot=-1, priority=-1):
    return struct.pack('<II9i', 6, 36, resource, 0, delay, 1, 1, 0, 0, slot, priority)


class OggAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/ogg_controls_20261031/audio'
        cls.client = cls.folder / 'client'
        music = cls.client / 'Music'
        music.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / 'build/ffmpeg.exe', cls.folder / 'ffmpeg.exe')
        for name, value in [('first', 8192), ('second', 16384)]:
            wav = music / (name + '.wav')
            with wave.open(str(wav), 'wb') as out:
                out.setparams((2, 2, 48000, 0, 'NONE', 'PCM'))
                out.writeframes(struct.pack('<2h', value, value) * 960)
            subprocess.run([str(cls.folder / 'ffmpeg.exe'), '-v', 'error', '-y', '-i', str(wav), '-c:a', 'libvorbis', '-q:a', '5', str(music / (name + '.ogg'))], check=True, capture_output=True)
        (cls.client / 'audio.xml').write_text('<AUDIO>'
            '<EFFECT ID="FIRST" FILE="Music/first.ogg" LOOP_DELAY="0"/>'
            '<VOICE ID="ALIAS" FILE="Music/FIRST.OGG"/>'
            '<EFFECT ID="SECOND" FILE="Music/second.ogg" LOOP_DELAY="0"/>'
            '<EFFECT ID="LOOP" FILE="Music/first.ogg" LOOP_DELAY=".005" LOOP_TIMES="2"/>'
            '<EFFECT ID="WAV" FILE="Music/first.wav"/>'
            '<EFFECT ID="MISSING" FILE="Music/missing.ogg"/>'
            '<EFFECT ID="UNUSED" FILE="Music/unused.ogg"/>'
            '<GROUP ID="GROUP"><ITEM TAG="UNUSED"/></GROUP>'
            '</AUDIO>', encoding='utf-8')
        cls.exe = cls.folder / 'ogg_audio_probe.exe'
        command = [str(ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'), '-std=c++20', '-O2', '-municode', '-static', '-DNOMINMAX', '-I' + str(ROOT / 'vendor/zlib'), str(ROOT / 'tests/ogg_audio_native.cpp'), str(ROOT / 'src/protocol.cpp'), str(ROOT / 'src/audio_resources.cpp'), str(ROOT / 'src/audio.cpp'), str(ROOT / 'vendor/zlib/libz.a'), '-lxaudio2_9', '-lole32', '-o', str(cls.exe)]
        built = subprocess.run(command, capture_output=True, text=True, env=dict(os.environ, TEMP=str(cls.folder), TMP=str(cls.folder)))
        (cls.folder / 'build.log').write_text(built.stdout + built.stderr, encoding='utf8')
        if built.returncode: raise RuntimeError(built.stdout + built.stderr)

    def probe(self, commands, scenes, mode='filter'):
        path = self.folder / (self._testMethodName + '.rep')
        path.write_bytes(pack_replay(commands, scenes, ['FIRST', 'ALIAS', 'SECOND', 'LOOP', 'WAV', 'MISSING', 'UNUSED', 'GROUP']))
        result = subprocess.run([str(self.exe), str(self.client), str(path), mode], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertTrue(data['implemented'], 'OGG inventory/filter/audition API is missing')
        return data

    def test_actual_calls_deduplicate_paths_and_distinguish_equal_timestamp_records(self):
        data = self.probe({0: play(0), 1: play(1), 2: play(2), 3: play(4), 4: play(5), 5: play(6), 6: play(7)}, [(0, (0, 1, 6)), (0, (2,)), (20, (3, 4)), (100, ())], 'paths')
        self.assertEqual(data['all'], ['music/first.ogg', 'music/missing.ogg', 'music/second.ogg'])
        self.assertEqual(data['frame0'], ['music/first.ogg'])
        self.assertEqual(data['frame1'], ['music/second.ogg'])
        self.assertEqual(data['frame2'], ['music/missing.ogg'])
        self.assertEqual(data['frame3'], [])
        self.assertEqual(data['decoders'], 0)

    def test_muting_shared_file_removes_all_aliases_and_keeps_other_audio(self):
        data = self.probe({0: play(0), 1: play(1), 2: play(2)}, [(0, (0, 1, 2)), (100, ())])
        self.assertGreater(data['before'][0], .9)
        self.assertAlmostEqual(data['after'][0], .5, delta=.03)
        self.assertEqual(data['events'], 3)

    def test_muted_voice_still_reserves_priority_slot(self):
        data = self.probe({0: play(0, slot=8, priority=9), 1: play(2, slot=8, priority=1)}, [(0, (0,)), (10, (1,)), (100, ())])
        self.assertGreater(data['before'][15], .2)
        self.assertLess(max(abs(value) for value in data['after']), 1e-6)
        self.assertEqual(data['resumedError'], 0)

    def test_muting_preserves_delays_and_loop_progress_when_reenabled(self):
        data = self.probe({0: play(3, delay=10)}, [(0, (0,)), (100, ())])
        self.assertLess(abs(data['before'][0]), 1e-6)
        self.assertGreater(data['before'][12], .2)
        self.assertGreater(data['before'][40], .2)
        self.assertLess(abs(data['before'][70]), 1e-6)
        self.assertLess(max(abs(value) for value in data['after']), 1e-6)
        self.assertEqual(data['resumedError'], 0)

    def test_audition_ignores_hidden_state_and_xml_loop_and_rejects_uncalled_paths(self):
        data = self.probe({0: play(0), 1: play(5)}, [(0, (0, 1)), (100, ())], 'solo')
        self.assertEqual(data['error'], 0)
        self.assertEqual(data['tail'], 0)
        self.assertTrue(data['cancelled'])
        self.assertTrue(data['unknown'])
        self.assertTrue(data['missing'])
        self.assertGreater(data['duration'], 0)
        self.assertLess(data['duration'], 30)

    def test_player_worker_applies_filter_on_seek_and_exposes_shared_inventory(self):
        data = self.probe({0: play(0)}, [(0, (0,)), (100, ())], 'player')
        self.assertTrue(data['ready'])
        self.assertTrue(data['clip'])
        self.assertEqual(data['all'], ['music/first.ogg'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
