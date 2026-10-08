"""Public render-thread behavior for explicit NPK refresh and fixed index snapshots."""
from concurrent.futures import ThreadPoolExecutor
import json
import os
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
from test_scene_branches import img_header, legacy, npk, op, pack_replay, rec

ROOT = Path(__file__).resolve().parents[1]


def image(color):
    raw = bytes(color) * 16
    return img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw


class NpkPriorityControllerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'npk_priority_20261008' / ('controller_' + uuid.uuid4().hex[:10])
        cls.folder.mkdir(parents=True)
        cls.binary = cls.folder / 'controller_probe.exe'
        source = cls.folder / 'src'
        source.mkdir()
        revision = os.environ.get('NPK_PRIORITY_SOURCE_REV')
        names = ('protocol', 'resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine',
                 'ui_playback', 'audio', 'audio_resources')
        pending = [name + '.cpp' for name in names]
        copied = set()
        while pending:
            name = pending.pop()
            if name in copied:
                continue
            copied.add(name)
            if revision:
                content = subprocess.run(['git', 'show', revision + ':src/' + name], cwd=ROOT,
                                         capture_output=True, check=True).stdout
            else:
                content = (ROOT / 'src' / name).read_bytes()
            (source / name).write_bytes(content)
            for header in re.findall(rb'^\s*#include\s+"([^"]+)"', content, re.MULTILINE):
                pending.append(header.decode())
        (cls.folder / 'source_revision.txt').write_text(revision or 'working tree source snapshot', encoding='utf8')
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        flags = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
                 '-I', str(source), '-I', str(ROOT / 'vendor/zlib'), '-I', str(ROOT / 'vendor/freetype/include')]

        def compile_one(name):
            target = cls.folder / (name + '.o')
            src = ROOT / 'tests/npk_priority_controller_native.cpp' if name == 'harness' else source / (name + '.cpp')
            result = subprocess.run([str(compiler), *flags, '-c', str(src), '-o', str(target)],
                                    capture_output=True, text=True, timeout=120)
            (cls.folder / (name + '_compile.log')).write_text(result.stdout + result.stderr, encoding='utf8')
            if result.returncode:
                raise RuntimeError(name + ': ' + result.stderr)
            return target

        with ThreadPoolExecutor(max_workers=3) as pool:
            objects = list(pool.map(compile_one, (*names, 'harness')))
        result = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects),
            str(ROOT / 'build/app.res.o'), str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
            '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lole32', '-o', str(cls.binary)],
            capture_output=True, text=True, timeout=120)
        (cls.folder / 'link.log').write_text(result.stdout + result.stderr, encoding='utf8')
        if result.returncode:
            raise RuntimeError(result.stderr)

    def prepare(self, mode):
        folder = self.folder / mode
        folder.mkdir()
        client = folder / 'client'
        packs = client / 'ImagePacks2'
        packs.mkdir(parents=True)
        replay_dir = client / 'Replay'
        replay_dir.mkdir()
        reference_packs = folder / 'reference_client' / 'ImagePacks2'
        reference_packs.mkdir(parents=True)
        (folder / 'reference_client' / 'Replay').mkdir()
        original = npk(image((0, 0, 255, 255)))
        patched = npk(image((0, 255, 0, 255)))
        (packs / 'sprite_test.NPK').write_bytes(original)
        (reference_packs / 'sprite_test.NPK').write_bytes(patched)
        (folder / 'patch_green.NPK').write_bytes(patched)
        (folder / 'patch_red.NPK').write_bytes(original)
        draw, aux = legacy(2, 3)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        payload = pack_replay(1.7, {0: draw}, [(0, (0,), aux), (10, (0,), aux)],
                              ['sprite/test/frame.img'], header=header)
        for name in ('first', 'second'):
            (replay_dir / (name + '.rep')).write_bytes(payload)
        if mode == 'shader':
            # Type 12 binds a cached builtin dissolvemask frame. Replacing this
            # isolated original package lets a fresh controller supply an
            # independent expected GPU result even on the pre-overlay baseline.
            (packs / 'sprite_shader.NPK').write_bytes(npk(image((0, 0, 0, 255)), 'sprite/shader/dissolvemask.img'))
            (folder / 'shader_changed.NPK').write_bytes(npk(image((255, 255, 255, 255)), 'sprite/shader/dissolvemask.img'))
            values = [0.0] * 29
            values[9] = .5
            values[10] = .1
            values[17:21] = [1, 1, 1, 1]
            values[23:25] = [1, 1]
            effect = op(19, b'\1' + struct.pack('<IIBII', 12, len(values), 0, 0, 0) + struct.pack('<29f', *values))
            shader = effect + draw + op(20)
            (replay_dir / 'shader.rep').write_bytes(pack_replay(1.7, {0: shader},
                [(0, (0,), aux), (10, (0,), aux)], ['sprite/test/frame.img'], header=header))
        return folder, client

    def run_mode(self, mode):
        folder, client = self.prepare(mode)
        result = subprocess.run([str(self.binary), mode, str(folder), str(client)],
                                capture_output=True, text=True, timeout=90)
        (folder / 'probe.log').write_text(result.stdout + result.stderr, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        (folder / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
        return report

    def test_refresh_rebuilds_images_and_preserves_visual_settings(self):
        report = self.run_mode('priority')
        self.assertTrue(report['fixture_colors_differ'], report)
        self.assertTrue(report['ordinary_replay_keeps_snapshot'], report)
        self.assertTrue(report['switch_replay_keeps_snapshot'], report)
        self.assertTrue(report['refresh_applies_added_patch'], report)
        self.assertTrue(report['refresh_rename_changes_priority'], report)
        self.assertTrue(report['refresh_reopens_renamed_patch'], report)
        self.assertTrue(report['refresh_removal_restores_original'], report)
        self.assertTrue(report['refresh_preserves_settings'], report)
        self.assertTrue(report['refresh_clears_published_snapshot'], report)
        self.assertTrue(report['refresh_publishes_new_snapshot'], report)

    def test_selecting_same_client_root_forces_resource_reload(self):
        report = self.run_mode('same_root')
        self.assertTrue(report['fixture_colors_differ'], report)
        self.assertTrue(report['same_root_rebuilds_images'], report)
        self.assertTrue(report['configure_clears_published_snapshot'], report)
        self.assertTrue(report['same_root_new_snapshot'], report)

    def test_refresh_without_replay_publishes_index_and_stays_empty(self):
        report = self.run_mode('empty')
        self.assertTrue(report['empty_stays_empty'], report)
        self.assertTrue(report['empty_snapshot_available'], report)

    def test_refresh_releases_cached_builtin_shader_img(self):
        report = self.run_mode('shader')
        self.assertTrue(report['shader_fixture_changes_output'], report)
        self.assertTrue(report['refresh_rebuilds_shader_builtin'], report)


if __name__ == '__main__':
    unittest.main()
