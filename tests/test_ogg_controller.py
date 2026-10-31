"""OGG controls on a real render/audio worker with generated local inputs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
from ogg_fixtures import make_fixture

ROOT = Path(__file__).resolve().parents[1]


class OggControllerTests(unittest.TestCase):
    def test_call_scopes_mute_audition_and_replay_lifecycle(self):
        self.exercise(False)

    def test_same_batch_audition_and_mute_stays_paused(self):
        self.exercise(True)

    def exercise(self, gated):
        folder = ROOT / 'validation/ogg_controls_20261031/controller' / ('run_' + uuid.uuid4().hex[:8])
        inputs = make_fixture(folder, ROOT / 'build/ffmpeg.exe')
        shutil.copy2(ROOT / 'build/ffmpeg.exe', folder / 'ffmpeg.exe')
        shutil.copytree(ROOT / 'assets/shaders', folder / 'assets/shaders')
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        objects = [ROOT / 'build' / (name + '.o') for name in
                   ('protocol', 'resources', 'audio_resources', 'audio', 'gpu', 'bindings', 'fonts', 'movies', 'engine', 'ui_playback')]
        exe = folder / 'ogg_controller.exe'
        extra = []
        if gated:
            # This gate changes scheduling only, and delegates every operation
            # to the real controller and audio workers.
            source = (ROOT / 'src/ui_playback.cpp').read_text(encoding='utf8')
            boundary = '        bool toggle,filter,capture,canvasChanged'
            self.assertEqual(source.count(boundary), 1)
            source = source.replace('#include "ui_playback.hpp"', '#include "ui_playback.hpp"\nvoid oggSnapshotBoundary();', 1)
            source = source.replace(boundary, '        ::oggSnapshotBoundary();\n' + boundary, 1)
            controlled = folder / 'gated_controller.cpp'
            controlled.write_text(source, encoding='utf8')
            objects = [o for o in objects if o.name != 'ui_playback.o']
            extra = ['-DOGG_CONTROLLER_GATE', controlled]
        command = [compiler, '-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
                   '-I', ROOT / 'src', '-I', ROOT / 'vendor/freetype/include',
                   ROOT / 'tests/ogg_controller_native.cpp', *extra, *objects,
                   ROOT / 'vendor/freetype/libfreetype.a', ROOT / 'vendor/zlib/libz.a',
                   '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lole32', '-o', exe]
        built = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=90,
                               env=dict(os.environ, TEMP=str(folder), TMP=str(folder)))
        (folder / 'build.log').write_text(built.stdout + built.stderr, encoding='utf8')
        self.assertEqual(built.returncode, 0, built.stderr)
        result = subprocess.run(list(map(str, [exe, folder, inputs['client'], inputs['replay'], inputs['otherReplay']])),
                                capture_output=True, text=True, encoding='utf8', timeout=60)
        (folder / 'stdout.log').write_text(result.stdout, encoding='utf8')
        (folder / 'stderr.log').write_text(result.stderr, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        (folder / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf8')
        self.assertTrue(report['implemented'], 'OGG controller capability is missing')
        self.assertTrue(all(report.values()), str(report))


if __name__ == '__main__':
    unittest.main(verbosity=2)
