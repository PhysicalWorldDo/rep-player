"""Real ownership and native worker regressions for cache lifetime across REP files."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import uuid
import zlib

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay, npk, img_header, rec, legacy, op

ROOT = Path(__file__).resolve().parents[1]


class PlaybackCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'release_103' / 'memory' / uuid.uuid4().hex[:8]
        cls.client = cls.folder / 'client'
        packs = cls.client / 'ImagePacks2'
        replay = cls.client / 'Replay'
        packs.mkdir(parents=True)
        replay.mkdir()
        size = 1024
        header = b'\x0b\0' + struct.pack('<8h', *([size, size] * 4))
        header += b'\x0c' + struct.pack('<H', 7) + bytes(126)
        for n in range(16):
            path = f'sprite/cache{n}/frame.img'
            raw = bytes([20 + n * 7, 150 - n * 4, 100 + n * 3, 255]) * (size * size)
            zipped = zlib.compress(raw)
            image = img_header(2, 36, 1) + rec(16, 6, size, size, len(zipped),
                                                       x=0, y=0, full_w=size, full_h=size) + zipped
            (packs / f'sprite_cache{n}.NPK').write_bytes(npk(image, path))
            draw, aux = legacy()
            (replay / f'cache{n}.rep').write_bytes(pack_replay(
                1.7, {0: draw}, [(0, (0,), aux), (1, (0,), aux)], [path], header=header))
        (replay / 'bad.rep').write_bytes(b'broken REP fixture')
        compiler = ROOT / 'toolchain' / 'llvm-mingw-20260616-ucrt-x86_64' / 'bin' / 'clang++.exe'
        options = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
                   '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor/zlib'), '-I', str(ROOT / 'vendor/freetype/include')]
        # Only the worker and harness are compiled here. Other engine objects are
        # copied once so the root task can rebuild without changing this test run.
        names = ['protocol', 'resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine']
        objects = []
        for name in names:
            target = cls.folder / (name + '.o')
            shutil.copy2(ROOT / 'build' / (name + '.o'), target)
            objects.append(target)
        for name, source in [('worker', ROOT / 'src/ui_playback.cpp'),
                             ('harness', ROOT / 'tests/playback_cache_native.cpp')]:
            target = cls.folder / (name + '.o')
            result = subprocess.run([str(compiler), *options, '-c', str(source), '-o', str(target)],
                                    capture_output=True, text=True, timeout=120)
            if result.returncode:
                raise RuntimeError(result.stderr)
            objects.append(target)
        cls.exe = cls.folder / 'playback_cache.exe'
        result = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects),
                                 str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
                                 '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lpsapi',
                                 '-o', str(cls.exe)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stderr)
        # GPU shader discovery stays inside the source workspace; the runtime
        # and all new fixture/cache files remain isolated in this run folder.
        assets = cls.folder / 'assets'
        result = subprocess.run(['powershell', '-NoProfile', '-Command',
                                 f"New-Item -ItemType Junction -Path '{assets}' -Target '{ROOT / 'assets'}' | Out-Null"],
                                capture_output=True, text=True, timeout=20)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def run_mode(self, mode):
        result = subprocess.run([str(self.exe), mode, str(self.folder), str(self.client)],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        (self.folder / (mode + '.json')).write_text(json.dumps(report, indent=2), encoding='utf8')
        return report

    def test_clearing_decoded_images_releases_owned_pixels_and_allows_reload(self):
        report = self.run_mode('assets')
        self.assertTrue(report['released'], 'Decoded IMG, Frame and Pixels remain owned after cache release')
        self.assertTrue(report['pixel_crc_preserved'])

    def test_switching_replays_keeps_committed_memory_bounded_and_playback_correct(self):
        report = self.run_mode('playback')
        self.assertLess(report['growth_bytes'], 64 * 1024 * 1024,
                        'Sixteen different REP files accumulate old decoded and GPU textures')
        self.assertTrue(report['switched_pixels'])
        self.assertTrue(report['same_replay_pixels'])
        self.assertTrue(report['error_recovered'])


if __name__ == '__main__':
    unittest.main()
