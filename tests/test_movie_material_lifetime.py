"""Dynamic movie/font shader sources must leave the stable material cache."""
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = Path(os.environ.get('REP_MOVIE_SEEK_OUTPUT', ROOT / 'validation/seek_native_20261029/movies'))


class MovieMaterialLifetimeTests(unittest.TestCase):
    def test_dynamic_sources_release_gpu_textures_but_static_sources_still_cache(self):
        folder = OUTPUT / ('material_' + uuid.uuid4().hex[:8])
        assets = folder / 'assets'
        assets.mkdir(parents=True)
        # Native mask frame 4 is the only external resource used by type 17.
        image = (b'Neople Img File\0' + struct.pack('<4I', 36 * 5, 0, 2, 5) +
                 struct.pack('<9i', 16, 5, 2, 2, 16, 0, 0, 2, 2) * 5 + b'\xff' * 16 * 5)
        name = b'sprite/shader/mask.img'.ljust(256, b'\0')
        seed = (b'puchikon@neople dungeon and fighter ' + b'DNF' * 100)[:255] + b'\0'
        entry = struct.pack('<II', 284, len(image)) + bytes(a ^ b for a, b in zip(name, seed))
        (assets / 'sprite_shader.NPK').write_bytes(b'NeoplePack_Bill\0' + struct.pack('<I', 1) + entry + image)
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        executable = folder / 'probe.exe'
        command = [compiler, '-std=c++20', '-O2', '-DNOMINMAX', '-municode', '-static',
                   '-I', ROOT / 'src', '-I', ROOT / 'vendor/zlib',
                   ROOT / 'src/protocol.cpp', ROOT / 'src/resources.cpp', ROOT / 'src/gpu.cpp',
                   ROOT / 'src/bindings.cpp', ROOT / 'tests/movie_material_lifetime_probe.cpp',
                   ROOT / 'vendor/zlib/libz.a', '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-o', executable]
        built = subprocess.run(list(map(str, command)), capture_output=True, timeout=120)
        (folder / 'build.log').write_bytes(built.stdout + built.stderr)
        self.assertEqual(built.returncode, 0, built.stderr.decode(errors='replace'))
        result = subprocess.run([str(executable), str(assets)], capture_output=True, timeout=20)
        (folder / 'result.log').write_bytes(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        got = json.loads(result.stdout)
        self.assertEqual(got['dynamic_retained'], 0, 'material cache pinned previous movie/font frames')
        self.assertTrue(got['static_cached'])
        self.assertTrue(got['reset_releases'])


if __name__ == '__main__':
    unittest.main()
