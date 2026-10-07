"""Sample-level REP audio timing, concurrent voices and control contracts."""
import json
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

ROOT = Path(__file__).resolve().parents[1]


def play(resource, key=1, sub=1, delay=0, slot=-1, source=0, loops=1):
    return struct.pack('<II9i', 6, 36, resource, loops, delay, key, sub, 0, source, slot, -1)


def control(kind, key=1, sub=0, flag=0, threshold=0):
    return struct.pack('<IIIIB3xI', 7, kind, key, sub, flag, threshold)


class AudioMixerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'audio_20261007' / ('mixer_' + uuid.uuid4().hex[:8])
        cls.client = cls.folder / 'client'
        music = cls.client / 'Music'
        music.mkdir(parents=True)
        for name, value in [('a', 8192), ('b', 16384), ('quiet', 4096)]:
            with wave.open(str(music / (name + '.wav')), 'wb') as out:
                out.setnchannels(1); out.setsampwidth(2); out.setframerate(48000)
                out.writeframes(struct.pack('<h', value) * 48000)
        (cls.client / 'audio.xml').write_text(
            '<AUDIO><EFFECT TAG="A" FILE="music/a.wav"/><EFFECT TAG="B" FILE="music/b.wav"/>'
            '<MUSIC TAG="M" FILE="music/quiet.wav" LOOP_DELAY="0"/>'
            '<RANDOM TAG="R"><ITEM TAG="A" PROB="100"/></RANDOM></AUDIO>', encoding='utf-8')
        cls.exe = cls.folder / 'mixer_probe.exe'
        command = [str(ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'),
                   '-std=c++20', '-O2', '-municode', '-static', '-DNOMINMAX',
                   '-I' + str(ROOT / 'vendor/zlib'), str(ROOT / 'tests/audio_mixer_probe.cpp'),
                   str(ROOT / 'src/protocol.cpp')]
        for source in ('audio_resources.cpp', 'audio.cpp'):
            if (ROOT / 'src' / source).exists(): command.append(str(ROOT / 'src' / source))
        command += [str(ROOT / 'vendor/zlib/libz.a'), '-lxaudio2_9', '-lole32', '-o', str(cls.exe)]
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode: raise RuntimeError(built.stdout + built.stderr)

    def mix(self, commands, scenes, seek=0, resources=('A', 'B', 'M', 'R')):
        path = self.folder / (self._testMethodName + '.rep')
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8))
        path.write_bytes(pack_replay(1.7, commands, [(time, ids, b'') for time, ids in scenes], resources, header=header))
        result = subprocess.run([str(self.exe), str(self.client), str(path), str(seek)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertTrue(data['implemented'], 'REP audio timeline and PCM mixer are not implemented')
        return data

    def test_concurrent_sounds_start_and_stop_at_recorded_times(self):
        data = self.mix({0: play(0, 10), 1: play(1, 20), 2: control(0, 10, 1), 3: control(0, 20, 1)},
                        [(0, (0,)), (20, (1,)), (40, (2,)), (60, (3,)), (100, ())])
        samples = data['samples']
        for ms, value in [(0, .25), (19, .25), (20, .75), (39, .75), (40, .5), (59, .5), (60, 0), (99, 0)]:
            self.assertAlmostEqual(samples[ms], value, places=4)

    def test_repeated_command_references_are_distinct_requests(self):
        data = self.mix({0: play(0)}, [(0, (0, 0)), (20, (0,)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], .5, places=4)
        self.assertAlmostEqual(data['samples'][20], .75, places=4)
        self.assertEqual(data['events'], 3)

    def test_random_parent_is_not_played_again_with_recorded_leaf(self):
        data = self.mix({0: play(3), 1: play(0)}, [(0, (0, 1)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], .25, places=4)
        self.assertEqual(data['missing'], 0)

    def test_seek_reconstructs_active_voices_without_replaying_history(self):
        commands = {0: play(0, 10), 1: play(1, 20), 2: control(0, 10, 1)}
        scenes = [(0, (0,)), (20, (1,)), (40, (2,)), (200, ())]
        full = self.mix(commands, scenes)
        sought = self.mix(commands, scenes, seek=50)
        self.assertEqual(full['samples'][50:100], sought['samples'][:50])

    def test_stop_all_preserves_music_until_opcode41(self):
        data = self.mix({0: play(0), 1: play(2, 30, slot=25), 2: control(3), 3: struct.pack('<I', 41)},
                        [(0, (0, 1)), (20, (2,)), (40, (3,)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], .375, places=4)
        self.assertAlmostEqual(data['samples'][20], .125, places=4)
        self.assertAlmostEqual(data['samples'][40], 0, places=4)

    def test_conditional_stop_matches_subkeys_and_source_threshold(self):
        data = self.mix({0: play(0, 10, 1, source=1), 1: play(0, 10, 2, source=3),
                         2: control(2, 10, flag=1, threshold=2), 3: control(0, 10, 2)},
                        [(0, (0, 1)), (20, (2,)), (40, (3,)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], .5, places=4)
        self.assertAlmostEqual(data['samples'][20], .25, places=4)
        self.assertAlmostEqual(data['samples'][40], 0, places=4)

    def test_delayed_request_can_be_cancelled_before_start(self):
        data = self.mix({0: play(0, 10, delay=80), 1: control(0, 10, 1)},
                        [(0, (0,)), (40, (1,)), (100, ())])
        self.assertTrue(all(abs(value) < 1e-5 for value in data['samples']))


if __name__ == '__main__': unittest.main()
