"""Real PNG exports share category rules, manual exceptions and legacy hides."""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scene_branches import img_header, npk, pack_replay, rec, _default_draw_params
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PATHS = ('sprite/map/room/tile.img', 'sprite/monster/test/body.img',
         'sprite/monster/test/effect/flash.img', 'sprite/character/test/body.img',
         'sprite/npc/test/body.img', 'sprite/custom/test/frame.img')
COLORS = ((255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255),
          (255, 255, 255, 255), (255, 255, 0, 255), (255, 0, 255, 255))


class ImageVisibilityExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'image_visibility_20261011' / ('export_' + uuid.uuid4().hex[:10])
        cls.folder.mkdir(parents=True)
        source = cls.folder / 'src'
        source.mkdir()
        names = ('protocol', 'resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine',
                 'audio_resources', 'audio', 'export')
        pending = [name + '.cpp' for name in names]
        copied = set()
        while pending:
            name = pending.pop()
            if name in copied:
                continue
            copied.add(name)
            content = (ROOT / 'src' / name).read_bytes()
            (source / name).write_bytes(content)
            pending.extend(header.decode() for header in re.findall(rb'^\s*#include\s+"([^"]+)"', content, re.MULTILINE))
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-static',
                 '-I', str(source), '-I', str(ROOT / 'vendor/zlib'), '-I', str(ROOT / 'vendor/freetype/include')]

        def compile_one(name):
            path = ROOT / 'tests/image_visibility_export_native.cpp' if name == 'harness' else source / (name + '.cpp')
            target = cls.folder / (name + '.o')
            result = subprocess.run([str(compiler), *flags, '-c', str(path), '-o', str(target)],
                                    capture_output=True, text=True, timeout=120)
            (cls.folder / (name + '_compile.log')).write_text(result.stdout + result.stderr, encoding='utf8')
            if result.returncode:
                raise RuntimeError(name + ': ' + result.stderr)
            return target

        with ThreadPoolExecutor(max_workers=3) as pool:
            objects = list(pool.map(compile_one, (*names, 'harness')))
        cls.exe = cls.folder / 'visibility_export.exe'
        linked = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects),
            str(ROOT / 'build/app.res.o'), str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
            '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lole32', '-o', str(cls.exe)],
            capture_output=True, text=True, timeout=120)
        (cls.folder / 'link.log').write_text(linked.stdout + linked.stderr, encoding='utf8')
        if linked.returncode:
            raise RuntimeError(linked.stderr)
        resources = cls.folder / 'resources'
        resources.mkdir()
        shutil.copy2(ROOT / 'build/ffmpeg.exe', resources / 'ffmpeg.exe')
        shutil.copytree(ROOT / 'assets/shaders', cls.folder / 'assets/shaders')
        cls.client = cls.folder / 'client'
        packs = cls.client / 'ImagePacks2'
        packs.mkdir(parents=True)
        commands, auxiliaries = {}, []
        for index, (path, color) in enumerate(zip(PATHS, COLORS)):
            r, g, b, a = color
            raw = bytes((b, g, r, a)) * 16
            image = img_header(2, 36, 1) + rec(16, 5, 4, 4, 64, x=0, y=0, full_w=4, full_h=4) + raw
            package = path.rsplit('/', 1)[0].replace('/', '_') + '.NPK'
            (packs / package).write_bytes(npk(image, path))
            params = bytearray(_default_draw_params(b''))
            struct.pack_into('<I', params, 0, index)
            struct.pack_into('<2f', params, 28, 0, 0)
            commands[index] = struct.pack('<II', 3, 64) + params
            auxiliaries.append(struct.pack('<2h', 2 + index * 6, 3))
        first = (0, 2, 5)
        scenes = [(0, first, b''.join(auxiliaries[i] for i in first)),
                  (100, tuple(range(6)), b''.join(auxiliaries))]
        header = b'\x0b\0' + struct.pack('<8h', *([40, 12] * 4))
        cls.replay = cls.folder / 'categories.rep'
        cls.replay.write_bytes(pack_replay(1.7, commands, scenes, list(PATHS), header=header))
        print('IMAGE_VISIBILITY_EXPORT_VALIDATION=' + str(cls.folder), flush=True)

    def export(self, mode, expected, hidden_count):
        result = subprocess.run([str(self.exe), str(self.folder), str(self.client), str(self.replay), mode, mode],
                                capture_output=True, text=True, timeout=30)
        (self.folder / (mode + '.log')).write_text(result.stdout + result.stderr, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual((report['frames'], report['scenes'], report['hidden']), (4, 2, hidden_count))
        frames = sorted((self.folder / 'exports' / mode).glob('frame_*.png'))
        self.assertEqual(len(frames), 4)
        for frame_index, path in enumerate(frames):
            with Image.open(path) as image:
                rgba = image.convert('RGBA')
            self.assertEqual(rgba.size, (40, 12))
            for index, visible in enumerate(expected):
                if frame_index == 0 and index not in (0, 2, 5):
                    visible = False
                self.assertEqual(rgba.getpixel((3 + index * 6, 4)), COLORS[index] if visible else (0, 0, 0, 0),
                                 (mode, frame_index, PATHS[index]))

    def test_category_rules_cover_paths_first_called_later(self):
        self.export('categories', (False, False, True, False, False, True), 4)

    def test_manual_show_and_hide_override_category_and_effect_exception(self):
        self.export('manual', (False, True, False, False, False, True), 4)

    def test_legacy_hidden_images_still_work_without_category_choices(self):
        self.export('legacy', (True, True, True, True, True, False), 1)

    def test_policy_manual_show_can_override_legacy_hidden_path(self):
        self.export('legacy_override', (True, True, True, True, True, True), 0)

    def test_export_policy_copy_keeps_its_start_choice(self):
        self.export('snapshot', (False, False, True, False, False, True), 4)


if __name__ == '__main__':
    unittest.main()
