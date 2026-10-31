"""Generated OGG selections reach the real MOV/MP4 mixer without changing pixels."""
import array
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid
import zlib

from ogg_fixtures import make_fixture, OGG_A, OGG_B

ROOT = Path(__file__).resolve().parents[1]


class OggExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = ROOT / 'validation' / 'ogg_controls_20261031' / 'export'
        cls.folder = base / ('run_' + uuid.uuid4().hex[:10])
        cls.folder.mkdir(parents=True)
        cls.ffmpeg = cls.folder / 'ffmpeg.exe'
        shutil.copy2(ROOT / 'build' / 'ffmpeg.exe', cls.ffmpeg)
        cls.fixture = make_fixture(cls.folder / 'fixture', cls.ffmpeg)
        cls.output = cls.folder / 'exports'
        override = os.environ.get('REP_OGG_EXPORT_EXE')
        cls.exe = Path(override) if override else cls.folder / 'rep_export.exe'
        if override:
            return
        shaders = base / 'assets'
        if not shaders.exists():
            command = "New-Item -ItemType Junction -Path '" + str(shaders).replace("'", "''")
            command += "' -Target '" + str(ROOT / 'assets').replace("'", "''") + "' | Out-Null"
            subprocess.run(['powershell', '-NoProfile', '-Command', command],
                           capture_output=True, check=True, timeout=15)
        compiler = ROOT / 'toolchain' / 'llvm-mingw-20260616-ucrt-x86_64' / 'bin' / 'clang++.exe'
        flags = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
                 '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor' / 'zlib'),
                 '-I', str(ROOT / 'vendor' / 'freetype' / 'include')]
        objects = [ROOT / 'build' / (name + '.o') for name in
                   ('resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine')]
        environment = dict(os.environ, TMP=str(cls.folder), TEMP=str(cls.folder))
        logs = []
        for name in ('protocol', 'audio_resources', 'audio', 'export', 'export_validate'):
            obj = cls.folder / (name + '.o')
            result = subprocess.run([str(compiler), *flags, '-c', str(ROOT / 'src' / (name + '.cpp')),
                                     '-o', str(obj)], capture_output=True, text=True,
                                    timeout=120, env=environment)
            logs.append(result.stdout + result.stderr)
            (cls.folder / 'compile.log').write_text('\n'.join(logs), encoding='utf8')
            if result.returncode:
                raise AssertionError(result.stderr)
            objects.append(obj)
        result = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects),
                                 str(ROOT / 'vendor' / 'freetype' / 'libfreetype.a'),
                                 str(ROOT / 'vendor' / 'zlib' / 'libz.a'), '-ld3d11', '-ldxgi',
                                 '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lole32', '-o', str(cls.exe)],
                                capture_output=True, text=True, timeout=120, env=environment)
        logs.append(result.stdout + result.stderr)
        (cls.folder / 'compile.log').write_text('\n'.join(logs), encoding='utf8')
        if result.returncode:
            raise AssertionError(result.stderr)

    def export(self, format='mov', hidden=(), audio=True):
        name = self._testMethodName + '_' + uuid.uuid4().hex[:8]
        command = [str(self.exe), '--client', str(self.fixture['client']),
                   '--replay', str(self.fixture['replay']), '--format', format, '--fps', '30',
                   '--alpha', '0', '--audio', '1' if audio else '0',
                   '--output', str(self.output), '--name', name]
        for path in hidden:
            command += ['--hide-ogg', path]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf8', timeout=60)
        (self.folder / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['missing_sounds'], 0)
        return report

    def decode(self, path, audio=True):
        args = ['-map', '0:a:0', '-f', 'f32le', '-ac', '2', '-ar', '48000'] if audio else [
            '-map', '0:v:0', '-f', 'rawvideo', '-pix_fmt', 'rgba']
        return subprocess.run([str(self.ffmpeg), '-v', 'error', '-i', path, *args, 'pipe:1'],
                              capture_output=True, timeout=30)

    def pcm(self, path):
        result = self.decode(path)
        self.assertEqual(result.returncode, 0, result.stderr)
        samples = array.array('f')
        samples.frombytes(result.stdout)
        return samples

    def video_crc(self, path):
        result = self.decode(path, audio=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        return zlib.crc32(result.stdout)

    def assert_all_muted(self, format):
        original = self.export(format)
        muted = self.export(format, (OGG_A, OGG_B))
        original_pcm, muted_pcm = self.pcm(original['output']), self.pcm(muted['output'])
        self.assertGreater(max(map(abs, original_pcm)), .05)
        self.assertLessEqual(max(map(abs, muted_pcm)), .000001)
        self.assertEqual(self.video_crc(original['output']), self.video_crc(muted['output']))
        self.assertEqual(original['frames'], muted['frames'])
        self.assertTrue(muted['audio'])

    def test_mov_all_ogg_disabled_is_silent_and_keeps_pixels(self):
        self.assert_all_muted('mov')

    def test_mp4_all_ogg_disabled_is_silent_and_keeps_pixels(self):
        self.assert_all_muted('mp4')

    def test_one_file_disabled_keeps_the_other_and_mutes_shared_tag(self):
        # Uppercase/backslash input must address the same file used by two TAGs.
        report = self.export(hidden=(OGG_A.upper().replace('/', '\\'),))
        samples = self.pcm(report['output'])
        self.assertGreater(max(map(abs, samples[:24000])), .05, 'B should remain audible')
        # B has ended and the 1000ms alias of disabled A has begun.
        segment = samples[1100 * 96:1200 * 96]
        self.assertEqual(max(map(abs, segment)), 0, 'A_ALIAS must share A file selection')

    def test_disable_audio_ignores_selection_and_keeps_video(self):
        original = self.export(audio=False)
        muted = self.export(hidden=(OGG_A, OGG_B), audio=False)
        self.assertFalse(muted['audio'])
        self.assertNotEqual(self.decode(muted['output']).returncode, 0)
        self.assertEqual(self.video_crc(original['output']), self.video_crc(muted['output']))


if __name__ == '__main__':
    unittest.main()
