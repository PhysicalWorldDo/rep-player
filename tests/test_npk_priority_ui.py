"""Actual Refresh NPK UI and export snapshots, isolated to this task's outputs."""
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import uuid
import zipfile

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay
from test_npk_reader import npk, img_header, rec
from rep_protocol import _default_draw_params
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BASELINE = '0d1f26c7c1e6f7ff7a9dacd45a61b2d60a640f82'


class NpkPriorityUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'npk_priority_20261008' / ('ui_' + uuid.uuid4().hex[:10])
        cls.folder.mkdir(parents=True)
        compiler = ROOT / 'toolchain' / 'llvm-mingw-20260616-ucrt-x86_64' / 'bin' / 'clang++.exe'
        options = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
                   '-I', str(ROOT / 'vendor/zlib'), '-I', str(ROOT / 'vendor/freetype/include')]
        if os.environ.get('REP_NPK_UI_BASELINE') == '1':
            archive = subprocess.check_output(['git', 'archive', '--format=zip', BASELINE, 'src'], cwd=ROOT)
            source = cls.folder / 'baseline'
            with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
                zipped.extractall(source)
            options += ['-DREP_NPK_UI_APP_SOURCE="' + (source / 'src/app.cpp').as_posix() + '"']
        names = ['protocol', 'resources', 'audio_resources', 'audio', 'gpu', 'bindings', 'fonts', 'movies',
                 'engine', 'catalog', 'export', 'ui_playback', 'app.res']
        objects = []
        for name in names:
            path = cls.folder / (name + '.o')
            shutil.copy2(ROOT / 'build' / (name + '.o'), path)
            objects.append(path)
        harness = cls.folder / 'harness.o'
        cls.compile([str(compiler), *options, '-c', str(ROOT / 'tests/npk_priority_ui_native.cpp'), '-o', str(harness)], 'compile')
        cls.exe = cls.folder / 'npk_priority_ui.exe'
        cls.compile([str(compiler), '-municode', '-static', *map(str, objects), str(harness),
                     str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
                     '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lcomctl32',
                     '-lshell32', '-lole32', '-luuid', '-luxtheme', '-lgdi32', '-o', str(cls.exe)], 'link')
        resources = cls.folder / 'resources'
        resources.mkdir()
        shutil.copy2(ROOT / 'build/ffmpeg.exe', resources / 'ffmpeg.exe')

    @classmethod
    def compile(cls, command, label):
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf8', timeout=120)
        (cls.folder / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf8')
        if result.returncode:
            raise RuntimeError(label + ' failed: ' + result.stderr)

    def setUp(self):
        self.run_folder = self.folder / self._testMethodName
        self.run_folder.mkdir()
        self.client = self.run_folder / '中文客户端'
        packs = self.client / 'ImagePacks2'
        packs.mkdir(parents=True)
        replay = self.client / 'Replay'
        replay.mkdir()
        self.patch = self.run_folder / 'owned_patch.NPK'
        def image(bgra):
            return img_header(2, 36, 1) + rec(16, 5, 4, 4, 64, x=0, y=0, full_w=4, full_h=4) + bytes(bgra) * 16
        (packs / 'sprite_test.NPK').write_bytes(npk(image((0, 0, 255, 255))))
        self.patch.write_bytes(npk(image((0, 255, 0, 255))))
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        (replay / 'refresh.rep').write_bytes(pack_replay(1.7, {0: draw},
            [(0, (0,), struct.pack('<2h', 2, 3)), (100, (0,), struct.pack('<2h', 2, 3))],
            ['sprite/test/frame.img'], header=header))

    def run_mode(self, mode):
        result = subprocess.run([str(self.exe), mode, str(self.run_folder), str(self.client), str(self.patch)],
                                capture_output=True, text=True, encoding='utf8', cwd=self.run_folder, timeout=60)
        (self.run_folder / 'stdout.log').write_text(result.stdout, encoding='utf8')
        (self.run_folder / 'stderr.log').write_text(result.stderr, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        (self.run_folder / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
        for bitmap in self.run_folder.glob('toolbar_*.bmp'):
            with Image.open(bitmap) as image:
                image.convert('RGB').save(bitmap.with_suffix('.png'))
        return report

    def test_bilingual_minimum_toolbar_and_empty_client_disabled(self):
        report = self.run_mode('empty')
        self.assertTrue(report['toolbar'])
        self.assertTrue(report['empty_client_disabled'])

    def test_refresh_command_preview_step_and_export_share_published_snapshot(self):
        report = self.run_mode('refresh')
        for name in ['refresh_command', 'pending_export_disabled', 'settings_preserved', 'img_panel', 'frame_step']:
            self.assertTrue(report[name], name)
        self.assertNotEqual(report['base_crc'], report['patch_crc'])
        # 1.5x canvas leaves a four-pixel margin around the original 16x16 picture.
        for name, color in [('before_refresh', (255, 0, 0, 255)), ('after_refresh', (0, 255, 0, 255))]:
            frames = sorted((self.run_folder / 'exports' / name).glob('frame_*.png'))
            self.assertEqual(len(frames), 4)
            for path in frames:
                with Image.open(path) as image:
                    self.assertEqual(image.size, (24, 24))
                    self.assertEqual(image.convert('RGBA').getpixel((7, 8)), color, name)


if __name__ == '__main__':
    unittest.main()
