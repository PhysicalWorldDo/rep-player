"""Active OGG lifetimes follow the interpreter and the submitted device clock."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
from ogg_fixtures import OGG_A, OGG_B, make_fixture, pack_replay

ROOT = Path(__file__).resolve().parents[1]
TAGS = ['A', 'B', 'LOOP', 'GAP', 'MUSIC', 'AMBIENT', 'ALIAS']


def play(resource, owner=1, delay=0, slot=-1, priority=-1):
    return struct.pack('<II9i', 6, 36, resource, 0, delay, owner, 1, 0, 0, slot, priority)


def control(kind, owner=1):
    return struct.pack('<IIIIB3xI', 7, kind, owner, 1, 0, 0)


class ActiveOggTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/ogg_active_20261031/audio'
        cls.folder.mkdir(parents=True, exist_ok=True)
        ffmpeg = cls.folder / 'ffmpeg.exe'
        shutil.copy2(ROOT / 'build/ffmpeg.exe', ffmpeg)
        fixture = make_fixture(cls.folder, ffmpeg)
        cls.client = fixture['client']
        (cls.client / 'audio.xml').write_text('<AUDIO>'
            '<EFFECT ID="A" FILE="' + OGG_A + '"/>'
            '<EFFECT ID="B" FILE="' + OGG_B + '"/>'
            '<EFFECT ID="LOOP" FILE="' + OGG_A + '" LOOP_DELAY="0"/>'
            '<EFFECT ID="GAP" FILE="' + OGG_A + '" LOOP_DELAY=".05" LOOP_TIMES="2"/>'
            '<MUSIC ID="MUSIC" FILE="' + OGG_B + '" LOOP_DELAY="0"/>'
            '<AMBIENT ID="AMBIENT" FILE="' + OGG_B + '" LOOP_DELAY="0"/>'
            '<VOICE ID="ALIAS" FILE="' + OGG_A + '"/>'
            '</AUDIO>', encoding='utf8')
        cls.exe = cls.folder / 'ogg_active_probe.exe'
        command = [str(ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'), '-std=c++20', '-O2', '-municode', '-static', '-DNOMINMAX', '-I' + str(ROOT / 'vendor/zlib'), str(ROOT / 'tests/ogg_active_native.cpp'), str(ROOT / 'src/protocol.cpp'), str(ROOT / 'src/audio_resources.cpp'), str(ROOT / 'src/audio.cpp'), str(ROOT / 'vendor/zlib/libz.a'), '-lxaudio2_9', '-lole32', '-o', str(cls.exe)]
        result = subprocess.run(command, capture_output=True, text=True, env=dict(os.environ, TEMP=str(cls.folder), TMP=str(cls.folder)))
        (cls.folder / 'build.log').write_text(result.stdout + result.stderr, encoding='utf8')
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def probe(self, commands, scenes, times, mode='track'):
        replay = self.folder / (self._testMethodName + '.rep')
        replay.write_bytes(pack_replay(commands, scenes, TAGS))
        result = subprocess.run([str(self.exe), str(self.client), str(replay), mode, ' '.join(map(str, times))], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        (self.folder / (self._testMethodName + '.json')).write_text(json.dumps(data, indent=2), encoding='utf8')
        self.assertTrue(data['implemented'], 'Active OGG lifecycle/device snapshot API is missing')
        return data

    def test_one_shot_stays_active_after_call_record_and_expires_naturally(self):
        data = self.probe({0: play(0)}, [(0, (0,)), (1000, ())], [0, 100, 250, 400])
        self.assertEqual([s['paths'] for s in data['states']], [[OGG_A], [OGG_A], [OGG_A], []])

    def test_delayed_start_and_positive_loop_wait_use_actual_voice_lifetime(self):
        data = self.probe({0: play(3, delay=100)}, [(0, (0,)), (1200, ())], [0, 99, 100, 250, 410, 460, 800])
        self.assertEqual([s['paths'] for s in data['states']], [[], [], [OGG_A], [OGG_A], [], [OGG_A], []])

    def test_continuous_loop_persists_and_owner_stop_removes_it(self):
        data = self.probe({0: play(2), 1: control(0)}, [(0, (0,)), (1000, (1,)), (1200, ())], [100, 500, 999, 1000, 1100])
        self.assertEqual([s['paths'] for s in data['states']], [[OGG_A], [OGG_A], [OGG_A], [], []])

    def test_music_opcode41_and_stop_all_follow_special_slots(self):
        data = self.probe({0: play(4), 1: play(2), 2: control(3), 3: struct.pack('<I', 41)}, [(0, (0, 1)), (100, (2,)), (200, (3,)), (1000, ())], [0, 100, 200])
        self.assertEqual([s['paths'] for s in data['states']], [[OGG_A, OGG_B], [OGG_B], []])

    def test_shared_paths_deduplicate_and_priority_rejection_never_adds_voice(self):
        data = self.probe({0: play(2, owner=1, slot=8, priority=9), 1: play(1, owner=2, slot=8, priority=1), 2: play(6, owner=3), 3: control(0, owner=1)}, [(0, (0, 2)), (100, (1,)), (200, (3,)), (1000, ())], [0, 100, 200, 400])
        self.assertEqual([s['paths'] for s in data['states']], [[OGG_A], [OGG_A], [OGG_A], []])

    def test_hidden_resources_remain_in_active_set(self):
        data = self.probe({0: play(2)}, [(0, (0,)), (1000, ())], [100, 500], 'hidden')
        self.assertEqual([s['paths'] for s in data['states']], [[OGG_A], [OGG_A]])

    def test_paused_snapshot_uses_target_clock_instead_of_queued_future_pcm(self):
        data = self.probe({0: play(0)}, [(200, (0,)), (1000, ())], [190, 210, 490, 510], 'player')
        self.assertEqual([s['paths'] for s in data['states']], [[], [OGG_A], [OGG_A], []])
        for state in data['states']:
            self.assertEqual(state['serial'], state['expectedSerial'])
            self.assertEqual(state['sample'], state['time'] * 48)
            self.assertGreaterEqual(state['buffered'], 30)
        self.assertEqual(data['latestSerial'], data['observedSerial'])
        self.assertEqual(data['latestPaths'], [])

    def test_playing_snapshot_tracks_device_sample_through_start_and_end(self):
        data = self.probe({0: play(0)}, [(200, (0,)), (1000, ())], [], 'live')
        states = data['states']
        self.assertTrue(any(s['time'] < 200 for s in states))
        self.assertTrue(any(220 < s['time'] < 450 and s['paths'] == [OGG_A] for s in states))
        self.assertTrue(any(s['time'] >= 510 and s['paths'] == [] for s in states))
        for state in states:
            if state['time'] < 200 or state['time'] >= 500:
                self.assertEqual(state['paths'], [], state)
            elif state['time'] < 490:
                self.assertEqual(state['paths'], [OGG_A], state)

    def test_raw_audition_never_enters_replay_active_list(self):
        data = self.probe({0: play(0)}, [(0, (0,)), (1000, ())], [], 'raw')
        self.assertEqual(data['direct'], [])
        self.assertEqual(data['paths'], [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
