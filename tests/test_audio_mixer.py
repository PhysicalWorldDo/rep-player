"""Sample-level REP audio timing, concurrent voices and control contracts."""
import json
import os
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import unittest
import uuid
import wave

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay

ROOT = Path(__file__).resolve().parents[1]


def play(resource, key=1, sub=1, delay=0, slot=-1, source=0, offset=0):
    return struct.pack('<II9i', 6, 36, resource, 0, delay, key, sub, offset, source, slot, -1)


def control(kind, key=1, sub=0, flag=0, threshold=0):
    return struct.pack('<IIIIB3xI', 7, kind, key, sub, flag, threshold)


class AudioMixerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'audio_20261007' / ('mixer_' + uuid.uuid4().hex[:8])
        cls.client = cls.folder / 'client'
        music = cls.client / 'Music'
        music.mkdir(parents=True)
        shutil.copy2(ROOT / 'build/ffmpeg.exe', cls.folder / 'ffmpeg.exe')
        for name, value in [('a', 8192), ('b', 16384), ('quiet', 4096)]:
            with wave.open(str(music / (name + '.wav')), 'wb') as out:
                out.setnchannels(2); out.setsampwidth(2); out.setframerate(48000)
                out.writeframes(struct.pack('<2h', value, value) * 48000)
        with wave.open(str(music / 'short.wav'), 'wb') as out:
            out.setnchannels(2); out.setsampwidth(2); out.setframerate(48000)
            out.writeframes(struct.pack('<2h', 8192, 8192) * 480)
        with wave.open(str(music / 'ramp.wav'), 'wb') as out:
            out.setnchannels(2); out.setsampwidth(2); out.setframerate(48000)
            out.writeframes(b''.join(struct.pack('<2h', int(n / 4800 * 16000), int(n / 4800 * 16000)) for n in range(4800)))
        (cls.client / 'audio.xml').write_text(
            '<AUDIO><EFFECT ID="A" FILE="Music/a.wav" LOOP_DELAY="0"/><EFFECT ID="B" FILE="Music/b.wav" LOOP_DELAY="0"/>'
            '<MUSIC ID="M" FILE="Music/quiet.wav" LOOP_DELAY="0"/>'
            '<RANDOM ID="R"><ITEM TAG="A" PROB="100"/></RANDOM>'
            '<EFFECT ID="L" FILE="Music/short.wav" LOOP_DELAY=".005" LOOP_TIMES="1"/>'
            '<EFFECT ID="S" FILE="Music/a.wav" DUPLICATE_LIMIT="1" DUPLICATE_POLICY="SWITCH"/>'
            '<EFFECT ID="P" FILE="Music/a.wav" DUPLICATE_LIMIT="1"/>'
            '<GROUP ID="G"><ITEM TAG="A" DELAY=".02"/></GROUP>'
            '<MUSIC ID="OFFSET" FILE="Music/ramp.wav"/>'
            '<AMBIENT ID="AM" FILE="Music/quiet.wav" LOOP_DELAY="0"/></AUDIO>', encoding='utf-8')
        text = (cls.client / 'audio.xml').read_text(encoding='utf-8')
        entries = ''
        for n in range(24):
            shutil.copy2(music / 'a.wav', music / ('cold' + str(n) + '.wav'))
            entries += '<EFFECT ID="C' + str(n) + '" FILE="Music/cold' + str(n) + '.wav"/>'
        (cls.client / 'audio.xml').write_text(text.replace('</AUDIO>', entries + '</AUDIO>'), encoding='utf-8')
        cls.exe = cls.folder / 'mixer_probe.exe'
        command = [str(ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'),
                   '-std=c++20', '-O2', '-municode', '-static', '-DNOMINMAX',
                   '-I' + str(ROOT / 'vendor/zlib'), str(ROOT / 'tests/audio_mixer_probe.cpp'),
                   str(ROOT / 'src/protocol.cpp')]
        for source in ('audio_resources.cpp', 'audio.cpp'):
            if (ROOT / 'src' / source).exists(): command.append(str(ROOT / 'src' / source))
        command += [str(ROOT / 'vendor/zlib/libz.a'), '-lxaudio2_9', '-lole32', '-o', str(cls.exe)]
        environment = dict(os.environ, TEMP=str(cls.folder), TMP=str(cls.folder))
        built = subprocess.run(command, capture_output=True, text=True, env=environment)
        if built.returncode: raise RuntimeError(built.stdout + built.stderr)

    def mix(self, commands, scenes, seek=0, resources=('A', 'B', 'M', 'R', 'L', 'S', 'P', 'G', 'OFFSET', 'AM'), player=False, profile='dfo'):
        path = self.folder / (self._testMethodName + '.rep')
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8))
        path.write_bytes(pack_replay(1.7, commands, [(time, ids, b'') for time, ids in scenes], resources, header=header))
        args = [str(self.exe), str(self.client), str(path), str(seek)]
        if player: args.append(player if isinstance(player, str) else 'player')
        if profile != 'dfo': args.append(profile)
        result = subprocess.run(args, capture_output=True, text=True)
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
        data = self.mix({0: play(0, 10, 1, source=1), 1: play(1, 10, 2, source=3),
                         2: control(2, 10, flag=1, threshold=2), 3: control(0, 10, 2)},
                        [(0, (0, 1)), (20, (2,)), (40, (3,)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], .75, places=4)
        self.assertAlmostEqual(data['samples'][20], .5, places=4)
        self.assertAlmostEqual(data['samples'][40], 0, places=4)

    def test_delayed_request_can_be_cancelled_before_start(self):
        data = self.mix({0: play(0, 10, delay=80), 1: control(0, 10, 1)},
                        [(0, (0,)), (40, (1,)), (100, ())])
        self.assertTrue(all(abs(value) < 1e-5 for value in data['samples']))

    def test_condition_uses_latest_shared_tag_rank_for_older_voices(self):
        data = self.mix({0: play(0, 10, 1, source=1), 1: play(0, 10, 2, source=3),
                         2: control(2, 10, flag=1, threshold=2)}, [(0, (0, 1)), (20, (2,)), (100, ())])
        self.assertAlmostEqual(data['samples'][20], .5, places=4)

    def test_positive_delay_loop_times_one_means_one_total_play(self):
        data = self.mix({0: play(4)}, [(0, (0,)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], .25, places=4)
        self.assertAlmostEqual(data['samples'][9], .25, places=4)
        self.assertTrue(all(abs(v) < 1e-5 for v in data['samples'][10:]))

    def test_repeat_limit_counts_pending_requests(self):
        data = self.mix({0: play(6, delay=80), 1: play(6, sub=2)}, [(0, (0,)), (10, (1,)), (100, ())])
        self.assertAlmostEqual(data['samples'][10], 0, places=4)
        self.assertAlmostEqual(data['samples'][80], .25, places=4)

    def test_switch_policy_keeps_new_sound_and_expires_old_within100ms(self):
        commands = {0: play(5, sub=1), 1: play(5, sub=2)}
        scenes = [(0, (0,)), (20, (1,)), (300, ())]
        data = self.mix(commands, scenes, seek=50)
        self.assertAlmostEqual(data['samples'][0], .5, places=4)
        self.assertAlmostEqual(data['samples'][69], .5, places=4)
        self.assertAlmostEqual(data['samples'][70], .25, places=4)

    def test_group_uses_recorded_child_request_without_duplicate_expansion(self):
        data = self.mix({0: play(7), 1: play(0)}, [(0, (0,)), (20, (1,)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], 0, places=4)
        self.assertAlmostEqual(data['samples'][20], .25, places=4)

    def test_type1_keeps_audible_loop_and_owner_key(self):
        data = self.mix({0: play(0, 10), 1: control(1, 10), 2: control(0, 10, 1)},
                        [(0, (0,)), (20, (1,)), (40, (2,)), (100, ())])
        self.assertAlmostEqual(data['samples'][20], .25, places=4)
        self.assertAlmostEqual(data['samples'][40], 0, places=4)

    def test_short_legal_request_prefix_uses_protocol_defaults(self):
        data = self.mix({0: struct.pack('<III', 6, 4, 0)}, [(0, (0,)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], .25, places=4)

    def test_music_start_position_is_source_offset_in_milliseconds(self):
        data = self.mix({0: play(8, offset=20)}, [(0, (0,)), (100, ())])
        self.assertAlmostEqual(data['samples'][0], 3200 / 32768, places=4)
        self.assertAlmostEqual(data['samples'][20], 6400 / 32768, places=4)

    def test_device_pause_seek_and_stop_keep_sample_clock_in_bounds(self):
        data = self.mix({0: play(0)}, [(0, (0,)), (1000, ())], player=True)
        self.assertTrue(data['available'], 'local audio output device was unavailable')
        self.assertGreaterEqual(data['first'], 20)
        self.assertEqual(data['paused'], data['frozen'])
        self.assertGreaterEqual(data['resumed'], 20)
        self.assertLess(data['resumed'], 90)
        self.assertEqual(data['sought'], 10)
        self.assertEqual(data['stopped'], 0)

    def test_cold_lookahead_decoding_does_not_block_device_sample_clock(self):
        names = ('A',) + tuple('C' + str(n) for n in range(24))
        commands = {n: play(n) for n in range(len(names))}
        data = self.mix(commands, [(0, (0,)), (2010, tuple(range(1, len(names)))), (4000, ())],
                        resources=names, player='player-cold')
        self.assertTrue(data['available'])
        self.assertGreaterEqual(data['first'], 300, 'cold future resource decoding stalled the 30ms device queue')

    def test_music_owner_stop_is_available_only_in_dnf_profile(self):
        commands = {0: play(2, 10), 1: control(0, 10, 1)}
        scenes = [(0, (0,)), (20, (1,)), (100, ())]
        dfo = self.mix(commands, scenes)
        dnf = self.mix(commands, scenes, profile='dnf-july')
        self.assertAlmostEqual(dfo['samples'][20], .125, places=4)
        self.assertAlmostEqual(dnf['samples'][20], 0, places=4)

    def test_music_does_not_belong_to_type2_ordinary_voice_map(self):
        data = self.mix({0: play(2, 10), 1: play(0, 10), 2: control(2, 10)},
                        [(0, (0, 1)), (20, (2,)), (100, ())], profile='dnf-july')
        self.assertAlmostEqual(data['samples'][20], .125, places=4)

    def test_ambient_uses_persistent_special_slot_even_if_recorded_slot_differs(self):
        data = self.mix({0: play(9, 10, slot=42), 1: control(3), 2: control(0, 10, 1)},
                        [(0, (0,)), (20, (1,)), (40, (2,)), (100, ())], profile='dnf-july')
        self.assertAlmostEqual(data['samples'][20], .125, places=4)
        self.assertAlmostEqual(data['samples'][40], .125, places=4)


if __name__ == '__main__': unittest.main()
