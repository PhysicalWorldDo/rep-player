"""SoundPacks overlays follow the applied image patch selection and refresh."""
import hashlib
import io
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
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_npk_priority import image, RED
from test_scene_branches import op, pack_replay

ROOT = Path(__file__).resolve().parents[1]
LOGICAL = 'sounds/char/fixture/tone.wav'
OTHER = 'sounds/effect/unusual/other.wav'
KEY = (b'puchikon@neople dungeon and fighter ' + b'DNF' * 100)[:255] + b'\0'


def wav(level):
    buffer = io.BytesIO()
    with wave.open(buffer, 'wb') as out:
        out.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        out.writeframes(struct.pack('<h', level) * 2 * 4800)
    return buffer.getvalue()


def npk(path, entries):
    offset = 52 + 264 * len(entries)
    table, data = bytearray(), bytearray()
    for name, payload in entries.items():
        encrypted = bytes(a ^ b for a, b in zip(name.encode().ljust(256, b'\0'), KEY))
        table += struct.pack('<II', offset, len(payload)) + encrypted
        data += payload
        offset += len(payload)
    header = b'NeoplePack_Bill\0' + struct.pack('<I', len(entries)) + table
    path.write_bytes(header + hashlib.sha256(header[:len(header) // 17 * 17]).digest() + data)


class AudioPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'audio_patches_20261023' / uuid.uuid4().hex[:10]
        cls.folder.mkdir(parents=True)
        cls.exe = cls.folder / 'audio_patches.exe'
        shutil.copy2(ROOT / 'build/ffmpeg.exe', cls.folder / 'ffmpeg.exe')
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        names = ('protocol', 'resources', 'audio_resources', 'audio', 'gpu', 'bindings', 'fonts',
                 'movies', 'engine', 'ui_playback', 'export', 'app.res')
        command = [compiler, '-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode',
                   '-static', '-I', ROOT / 'src', '-I', ROOT / 'vendor/freetype/include',
                   ROOT / 'tests/audio_patches_native.cpp',
                   *(ROOT / 'build' / (name + '.o') for name in names),
                   ROOT / 'vendor/freetype/libfreetype.a', ROOT / 'vendor/zlib/libz.a',
                   '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lole32', '-o', cls.exe]
        compiled = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=120)
        (cls.folder / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf8')
        if compiled.returncode:
            raise AssertionError(compiled.stderr)
        print('AUDIO_PATCH_VALIDATION=' + str(cls.folder), flush=True)

    def client(self, official=True):
        root = self.folder / self._testMethodName
        packs = root / 'client/SoundPacks'
        packs.mkdir(parents=True)
        (packs.parent / 'audio.xml').write_text(
            f'<AUDIO><EFFECT ID="TONE" FILE="{LOGICAL}"/><VOICE ID="OTHER" FILE="{OTHER}"/></AUDIO>', encoding='utf8')
        if official:
            npk(packs / 'sounds_char_fixture.NPK', {LOGICAL: wav(4096)})
            npk(packs / 'sounds_misc.NPK', {OTHER: wav(8192)})
        return root, packs

    def run_probe(self, root, mode='resources', enabled=True):
        result = subprocess.run([str(self.exe), mode, str(root / 'client'), str(root), str(int(enabled))],
                                capture_output=True, text=True, encoding='utf8', timeout=60)
        (root / (mode + '_' + str(enabled) + '.json')).write_text(
            json.dumps({'code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}), encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_early_patch_overrides_guessed_official_and_preserves_other_paths(self):
        root, packs = self.client()
        npk(packs / '%%中文补丁.npk', {LOGICAL: wav(16384)})
        got = self.run_probe(root)
        self.assertEqual(got['tone'], .5)
        self.assertEqual(got['other'], .25)

    def test_first_filename_wins_using_case_sensitive_utf16_order(self):
        root, packs = self.client()
        for name, value in [('2.npk', 8192), ('10.npk', 16384), ('A_upper.npk', 24576), ('a_lower.npk', 4096)]:
            npk(packs / name, {LOGICAL: wav(value)})
        self.assertEqual(self.run_probe(root)['tone'], .5)
        (packs / '10.npk').unlink()
        (packs / '2.npk').unlink()
        self.assertEqual(self.run_probe(root)['tone'], .75)

    def test_disabled_patches_skip_broken_early_packages(self):
        root, packs = self.client()
        (packs / '!broken.npk').write_bytes(b'bad npk')
        self.assertEqual(self.run_probe(root, enabled=False)['tone'], .125)

    def test_disabled_patches_do_not_supply_missing_official_paths(self):
        root, packs = self.client()
        npk(packs / '!patch.npk', {OTHER: wav(16384)})
        (packs / 'sounds_misc.NPK').unlink()
        self.assertEqual(self.run_probe(root, enabled=False)['other'], -9)

    def test_packages_after_official_boundary_never_supply_overlay_paths(self):
        root, packs = self.client()
        (packs / 'sounds_misc.NPK').unlink()
        npk(packs / 'zz_patch.npk', {OTHER: wav(16384)})
        self.assertEqual(self.run_probe(root)['other'], -9)

    def test_missing_official_boundary_ignores_custom_packages(self):
        root, packs = self.client(official=False)
        npk(packs / '!patch.npk', {LOGICAL: wav(16384)})
        self.assertEqual(self.run_probe(root)['tone'], -9)

    def test_official_prefix_is_case_insensitive_but_sort_preserves_case(self):
        root, packs = self.client()
        npk(packs / 'SOUNDS_boundary.NPK', {'sounds/unrelated.wav': wav(1)})
        npk(packs / 'Z_after.npk', {OTHER: wav(24576)})
        npk(packs / '!early.NPK', {LOGICAL: wav(16384)})
        self.assertEqual(self.run_probe(root)['tone'], .5)
        self.assertEqual(self.run_probe(root)['other'], .25)

    def test_invalid_patch_audio_falls_back_per_path(self):
        root, packs = self.client()
        npk(packs / '!patch.npk', {LOGICAL: b'not audio', OTHER: wav(16384)})
        got = self.run_probe(root)
        self.assertEqual(got['tone'], .125)
        self.assertEqual(got['other'], .5)
        self.assertEqual(got['missing'], 0)

    def test_nonfinite_patch_pcm_falls_back_to_official(self):
        root, packs = self.client()
        data = struct.pack('<f', float('nan')) * 9600
        floating = (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
                    struct.pack('<IHHIIHH', 16, 3, 2, 48000, 384000, 8, 32) +
                    b'data' + struct.pack('<I', len(data)) + data)
        npk(packs / '!patch.npk', {LOGICAL: floating})
        got = self.run_probe(root)
        self.assertEqual(got['tone'], .125)
        self.assertEqual(got['missing'], 0)

    def test_applied_snapshot_is_shared_by_replay_step_and_video_export_until_refresh(self):
        self.controller_case()

    def test_bad_patch_rejection_stays_shared_until_refresh(self):
        self.controller_case(repaired=True)

    def controller_case(self, repaired=False):
        root, packs = self.client()
        payload = wav(16384)
        npk(packs / '%patch.npk', {LOGICAL: b'JUNK' + payload[4:] if repaired else payload})
        npk(root / 'repaired.npk', {LOGICAL: payload})
        npk(root / 'added.npk', {LOGICAL: wav(24576)})
        images = packs.parent / 'ImagePacks2'
        images.mkdir()
        npk(images / 'sprite_test.NPK', {'sprite/test/frame.img': image(RED)})
        replay_dir = packs.parent / 'Replay'
        replay_dir.mkdir()
        sound = op(6, struct.pack('<I9i', 36, 0, 0, 0, 1, 1, 0, 0, -1, -1))
        replay_dir.joinpath('tone.rep').write_bytes(pack_replay(1.7, {0: sound}, [(0, (0,), b''), (100, (), b'')],
            ['TONE'], header=b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))))
        replay_dir.joinpath('missing.rep').write_bytes(pack_replay(1.7, {0: sound}, [(0, (0,), b''), (100, (), b'')],
            ['ABSENT_TAG'], header=b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))))
        got = self.run_probe(root, 'fallback' if repaired else 'controller')
        self.assertTrue(got['supported'], 'controller does not expose applied sound resources')
        self.assertEqual((got['enabled'], got['disabled'], got['refreshed']),
                         (.125, .125, .5) if repaired else (.5, .125, .75))
        self.assertTrue(got['same_snapshot'])
        self.assertTrue(got['new_snapshot'])
        self.assertTrue(got['export_audio'])
        self.assertTrue(got['clean_diagnostics'], 'a previous REP leaked its missing sound into playback/export')
        for name, expected in [('applied', .125 if repaired else .5), ('disabled', .125)]:
            pcm = subprocess.run([str(self.folder / 'ffmpeg.exe'), '-v', 'error', '-i', str(root / (name + '.mov')),
                                  '-map', '0:a:0', '-f', 'f32le', 'pipe:1'], capture_output=True, timeout=20)
            self.assertEqual(pcm.returncode, 0, pcm.stderr)
            self.assertAlmostEqual(struct.unpack_from('<f', pcm.stdout)[0], expected, places=4)


if __name__ == '__main__':
    unittest.main()
