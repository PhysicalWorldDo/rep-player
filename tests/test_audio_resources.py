"""TAG registration, read-only package acquisition and shared native PCM decode."""
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import unittest
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
CN = Path(r'E:\WeGameApps\地下城与勇士：创新世纪')
KEY = (b'puchikon@neople dungeon and fighter ' + b'DNF' * 100)[:255] + b'\0'


def package(entries):
    position = 20 + len(entries) * 264
    table, data = bytearray(), bytearray()
    for name, payload in entries:
        name = name.encode().ljust(256, b'\0')
        table += struct.pack('<II', position, len(payload)) + bytes(a ^ b for a, b in zip(name, KEY))
        data += payload
        position += len(payload)
    return b'NeoplePack_Bill\0' + struct.pack('<I', len(entries)) + table + data


class AudioResourcesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.probe = None
        if not (ROOT / 'src' / 'audio_resources.cpp').is_file():
            return
        cls.folder = ROOT / 'validation' / 'audio_resources' / uuid.uuid4().hex[:10]
        cls.folder.mkdir(parents=True)
        cls.client = cls.folder / '声音客户端'
        cls.client.mkdir()
        cls.cache = cls.folder / 'cache'
        cls.cache.mkdir()
        (cls.cache / 'preserve.txt').write_text('retain prior cache', encoding='utf8')
        cls.ffmpeg = cls.folder / 'ffmpeg.exe'
        shutil.copy2(ROOT / 'build' / 'ffmpeg.exe', cls.ffmpeg)
        compiler = ROOT / 'toolchain' / 'llvm-mingw-20260616-ucrt-x86_64' / 'bin' / 'clang++.exe'
        cls.probe = cls.folder / 'audio_resources_probe.exe'
        command = [compiler, '-std=c++20', '-O2', '-DNOMINMAX', '-municode', '-static', '-I', ROOT / 'src',
                   ROOT / 'tests' / 'audio_resources_probe.cpp', ROOT / 'src' / 'audio_resources.cpp',
                   ROOT / 'build' / 'protocol.o', ROOT / 'vendor' / 'zlib' / 'libz.a', '-o', cls.probe]
        compile_result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
        (cls.folder / 'compile.log').write_text(compile_result.stdout + compile_result.stderr, encoding='utf8')
        if compile_result.returncode:
            raise AssertionError(compile_result.stderr)
        (cls.client / 'Music').mkdir()
        (cls.client / 'SoundPacks').mkdir()
        wav = cls.client / 'Music' / 'tone & music.wav'
        with wave.open(str(wav), 'wb') as file:
            file.setparams((1, 2, 44100, 0, 'NONE', 'not compressed'))
            file.writeframes(b''.join(struct.pack('<h', round(math.sin(n * math.tau * 440 / 44100) * 16000))
                                     for n in range(4410)))
        ogg = cls.folder / 'fixture.ogg'
        subprocess.run([str(cls.ffmpeg), '-v', 'error', '-i', str(wav), '-c:a', 'libvorbis', str(ogg)],
                       check=True, capture_output=True)
        cls.payload = ogg.read_bytes()
        (cls.client / 'SoundPacks' / 'sounds_char_fixture.npk').write_bytes(package([
            ('sounds/char/fixture/tone.ogg', cls.payload)]))
        (cls.client / 'SoundPacks' / 'sounds_effect_misc.npk').write_bytes(package([
            ('sounds/effect/unusual/fallback.ogg', cls.payload), ('sounds/effect/bad.ogg', b'not audio')]))
        (cls.client / 'audio.xml').write_text('''<?xml version="1.0"?>
<AudioTagDatabase>
<!-- <EFFECT ID="COMMENTED" FILE="sounds/missing.ogg"/> -->
<EFFECT ID="TONE" FILE="sounds\\char\\fixture\\tone.ogg" LOOP_DELAY="0.1" DUPLICATE_LIMIT="2" DUPLICATE_POLICY="SWITCH"/>
<VOICE ID='ALIAS' FILE='sounds/char/fixture/tone.ogg'/>
<MUSIC ID="MUSIC&amp;TAG" FILE="music\\tone &amp; music.wav" LOOP_DELAY="0"/>
<AMBIENT ID="AMBIENT" FILE="sounds/effect/unusual/fallback.ogg"/>
<UNINTERRUPTED_EFFECT ID="UNINTERRUPTED" FILE="sounds/effect/unusual/fallback.ogg" LOOP_DELAY="1.5"/>
<RANDOM ID="R_PARENT"><ITEM TAG="TONE" PROB="100"/></RANDOM>
<GROUP ID="G_PARENT"><ITEM TAG="TONE" DELAY="0.5"/></GROUP>
<RANDOMGROUP><GROUP><ITEM ID="A" PROB="100"/></GROUP><PLAYTAG ID="RG_PARENT"><ITEM TAG="TONE" GROUP="A"/></PLAYTAG></RANDOMGROUP>
<EFFECT ID="MISSING" FILE="sounds/effect/absent.ogg"/>
<EFFECT ID="BAD" FILE="sounds/effect/bad.ogg"/>
<EFFECT ID="NUMERIC&#x26;TAG" FILE="sounds/char/fixture/tone.ogg"/>
</AudioTagDatabase>''', encoding='utf8')

    def run_probe(self, mode='summary', tags=(), client=None):
        self.assertIsNotNone(self.probe, 'native sound-resource module is not implemented')
        result = subprocess.run([str(self.probe), mode, str(client or self.client), str(self.cache), *tags],
                                capture_output=True, text=True, encoding='utf8', timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_package_audio_decodes_to_stereo_48k(self):
        got = self.run_probe(tags=['TONE'])['items'][0]
        self.assertTrue(got['clip'])
        self.assertEqual((got['rate'], got['channels']), (48000, 2))
        self.assertAlmostEqual(got['frames'] / got['rate'], 0.1, delta=0.01)
        self.assertGreater(got['mean_abs'], 0.1)
        self.assertTrue(got['finite'])
        self.assertEqual((got['loop_delay'], got['duplicate_limit'], got['duplicate_policy']), (0.1, 2, 'SWITCH'))

    def test_direct_music_and_xml_entities_preserve_chinese_root(self):
        got = self.run_probe(tags=['MUSIC&TAG', 'NUMERIC&TAG'])['items']
        self.assertTrue(all(item['clip'] for item in got))
        self.assertEqual(got[0]['frames'], 4800)
        self.assertEqual(got[0]['file'], r'music\tone & music.wav')

    def test_soundpack_directory_fallback_resolves_exact_internal_path(self):
        got = self.run_probe(tags=['AMBIENT', 'UNINTERRUPTED'])
        self.assertTrue(all(item['clip'] for item in got['items']))
        self.assertEqual(got['decoded'], 1)
        self.assertEqual(got['items'][1]['loop_delay'], 1.5)

    def test_composites_are_registered_without_random_replay_or_decode(self):
        got = self.run_probe(tags=['R_PARENT', 'G_PARENT', 'RG_PARENT'])
        self.assertTrue(all(item['definition'] and not item['playable'] and not item['clip'] for item in got['items']))
        self.assertEqual(got['decoded'], 0)

    def test_comments_do_not_register_fake_audio(self):
        item = self.run_probe(tags=['COMMENTED'])['items'][0]
        self.assertFalse(item['definition'])
        self.assertFalse(item['clip'])

    def test_missing_and_invalid_files_report_diagnostics_without_throwing(self):
        got = self.run_probe(tags=['MISSING', 'BAD', 'ABSENT_TAG'])
        self.assertTrue(all(not item['clip'] for item in got['items']))
        self.assertGreaterEqual(len(got['diagnostics']), 3)
        self.assertTrue(any('BAD' in message for message in got['diagnostics']))

    def test_client_without_audio_registry_remains_usable(self):
        self.assertIsNotNone(self.probe, 'native sound-resource module is not implemented')
        empty = self.folder / 'empty_client'
        empty.mkdir(exist_ok=True)
        got = self.run_probe(client=empty, tags=['TONE'])
        self.assertFalse(got['items'][0]['definition'])
        self.assertFalse(got['items'][0]['clip'])

    def test_cancellation_does_not_poison_following_decode(self):
        got = self.run_probe(mode='cancel-retry')
        self.assertTrue(got['cancelled_null'])
        self.assertGreater(got['retry_frames'], 0)
        self.assertEqual(got['decoded'], 1)

    def test_concurrent_aliases_share_one_pcm_object(self):
        got = self.run_probe(mode='concurrent')
        self.assertTrue(got['same'])
        self.assertGreater(got['frames'], 0)
        self.assertEqual(got['decoded'], 1)

    def test_input_packages_and_prior_cache_stay_unchanged(self):
        self.run_probe(tags=['TONE', 'MUSIC&TAG'])
        self.assertEqual((self.client / 'SoundPacks' / 'sounds_char_fixture.npk').read_bytes(),
                         package([('sounds/char/fixture/tone.ogg', self.payload)]))
        self.assertEqual((self.cache / 'preserve.txt').read_text(encoding='utf8'), 'retain prior cache')
        self.assertEqual([p.name for p in self.cache.iterdir()], ['preserve.txt'])

    @unittest.skipUnless((CN / 'audio.xml').is_file(), 'read-only CN client is unavailable')
    def test_real_seadragon_recorded_music_and_ambient(self):
        tags = ['BREAKER_F_SEA_DRAGON_TOOTH_DRAGON_LOOP_VP1', 'BREAKER_F_SEA_DRAGON_TOOTH_DRAGON_SHOT_VP1',
                'BREAKER_F_SEA_DRAGON_TOOTH_DRAGON_END_VP1', 'BREAKER_F_VO_SEADRAGON_TOOTH_VP1_02',
                'M_ARADPVP_PUB', 'AMB_DARKSTAGE_01']
        got = self.run_probe(client=CN, tags=tags)
        self.assertTrue(all(item['clip'] and item['finite'] for item in got['items']))
        self.assertTrue(all((item['rate'], item['channels']) == (48000, 2) for item in got['items']))
        for item, expected in zip(got['items'], [2.066667, 1.666667, 1.3, 0.751156, 87.771406, 27.967823]):
            self.assertAlmostEqual(item['frames'] / 48000, expected, delta=0.01)
        self.assertEqual(got['decoded'], 6)


if __name__ == '__main__':
    unittest.main()
