"""Real render-thread regression for simultaneous Canvas Apply and Stop."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay
from test_npk_reader import npk, img_header, rec
from rep_protocol import _default_draw_params

ROOT = Path(__file__).resolve().parents[1]


class CanvasControllerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'canvas_ui' / ('controller_' + uuid.uuid4().hex[:10])
        cls.folder.mkdir(parents=True)
        cls.client = cls.folder / 'client'
        assets = cls.client / 'ImagePacks2'
        assets.mkdir(parents=True)
        raw = bytes((0, 0, 255, 255)) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (assets / 'sprite_test.NPK').write_bytes(npk(image, 'sprite/test/frame.img'))
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        cls.replay = cls.folder / 'controller.rep'
        cls.replay.write_bytes(pack_replay(1.7, {0: draw},
            [(n, (0,), struct.pack('<2h', 2 + n % 4, 3)) for n in range(64)],
            ['sprite/test/frame.img'], header=b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))))
        cls.binary = cls.folder / 'controller_probe.exe'
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        objects = [ROOT / 'build' / (name + '.o') for name in
                   ('protocol', 'resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine', 'ui_playback')]
        result = subprocess.run([str(compiler), '-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE',
            '-municode', '-static', '-I', str(ROOT / 'vendor/zlib'), '-I', str(ROOT / 'vendor/freetype/include'),
            str(ROOT / 'tests/canvas_controller_native.cpp'), *map(str, objects), str(ROOT / 'build/app.res.o'),
            str(ROOT / 'vendor/freetype/libfreetype.a'), str(ROOT / 'vendor/zlib/libz.a'),
            '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-o', str(cls.binary)],
            capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def test_pending_canvas_apply_survives_stop_generation(self):
        result = subprocess.run([str(self.binary), str(self.folder), str(self.client), str(self.replay)],
            capture_output=True, text=True, timeout=30)
        self.assertTrue(result.stdout.strip(), result.stderr)
        report = json.loads(result.stdout)
        (self.folder / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
        self.assertTrue(report['pass'], 'queued Canvas Apply was lost when Stop advanced the controller generation: ' + str(report))
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
