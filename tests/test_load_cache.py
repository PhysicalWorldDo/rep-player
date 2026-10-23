"""Real CPU/GPU ownership, byte-budget, and shared patch validation regressions."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import unittest
import uuid
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]

def image(value, corrupt_last=False):
    size = 128
    raw = bytes([5, 10, value, 255]) * (size * size)
    payloads = [zlib.compress(raw), b'not zlib' if corrupt_last else zlib.compress(raw)]
    header = b'Neople Img File\0' + struct.pack('<4I', 72, 0, 2, 2)
    records = b''.join(struct.pack('<9i', 16, 6, size, size, len(p), 0, 0, size, size) for p in payloads)
    return header + records + b''.join(payloads)

def package(data, logical):
    seed = b'puchikon@neople dungeon and fighter '
    while len(seed) < 255:
        seed += b'DNF'
    seed = seed[:255] + b'\0'
    name = logical.encode() + b'\0'
    name = name.ljust(256, b'\0')
    encoded = bytes(a ^ b for a, b in zip(name, seed))
    return b'NeoplePack_Bill\0' + struct.pack('<I', 1) + struct.pack('<2I', 284, len(data)) + encoded + data

def palette_image():
    size = 128
    raw = zlib.compress(bytes(size*size))
    header = b'Neople Img File\0' + struct.pack('<5I',36,0,6,1,12)
    palettes = b''.join(struct.pack('<I',1)+bytes([30+n,10,5,255]) for n in range(12))
    return header+palettes+struct.pack('<9i',14,6,size,size,len(raw),0,0,size,size)+raw

class LoadCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/load_stage1_20261023/cache' / uuid.uuid4().hex[:8]
        cls.folder.mkdir(parents=True)
        cls.packs = cls.folder / 'packs'
        cls.packs.mkdir()
        for n in range(3):
            (cls.packs / f'sprite_cache{n}.NPK').write_bytes(package(image(30+n), f'sprite/cache{n}/frame.img'))
        for filename, logical, value, corrupt in [('000_valid.NPK','sprite/valid/frame.img',90,False),('001_bad.NPK','sprite/bad/frame.img',80,True),('sprite_valid.NPK','sprite/valid/frame.img',10,False),('sprite_bad.NPK','sprite/bad/frame.img',20,False)]:
            (cls.packs / filename).write_bytes(package(image(value,corrupt),logical))
        (cls.packs/'sprite_weapon.NPK').write_bytes(package(palette_image(),'sprite/weapon/katana0100b.img'))
        (cls.packs/'sprite_interface.NPK').write_bytes(package(image(70),'sprite/interface/(2x)handle.img'))
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        opts = ['-std=c++20','-O2','-DNOMINMAX','-DUNICODE','-D_UNICODE','-I',str(ROOT/'src'),'-I',str(ROOT/'vendor/zlib')]
        instrumented = cls.folder/'resources_instrumented.cpp'
        source = (ROOT/'src/resources.cpp').read_text(encoding='utf8')
        target = 'void Img::validateFrames(){'
        if source.count(target) != 1:
            raise RuntimeError('Cannot instrument the real whole-IMG validation entry')
        instrumented.write_text(source.replace(target, target+'\n    extern void loadCacheValidationBarrier();loadCacheValidationBarrier();',1),encoding='utf8')
        objects = []
        for name, source in [('protocol',ROOT/'src/protocol.cpp'),('resources',instrumented),('gpu',ROOT/'src/gpu.cpp'),('probe',ROOT/'tests/load_cache_native.cpp')]:
            obj = cls.folder / (name+'.o')
            result = subprocess.run([str(compiler),*opts,'-c',str(source),'-o',str(obj)],capture_output=True,text=True,timeout=180)
            if result.returncode:
                raise RuntimeError(result.stderr)
            objects.append(obj)
        cls.exe = cls.folder/'load_cache.exe'
        result = subprocess.run([str(compiler),'-municode','-static',*map(str,objects),str(ROOT/'vendor/zlib/libz.a'),'-ld3d11','-ldxgi','-ld3dcompiler','-ldxguid','-o',str(cls.exe)],capture_output=True,text=True,timeout=90)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def run_mode(self, mode):
        result = subprocess.run([str(self.exe),mode,str(self.packs)],capture_output=True,text=True,timeout=90)
        (self.folder/(mode+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
        self.assertEqual(result.returncode,0,result.stderr)

    def test_cpu_lru_byte_budget_and_full_clear(self):
        self.run_mode('assets')

    def test_gpu_lru_byte_budget_cpu_shadow_and_pinned_lifetime(self):
        self.run_mode('gpu')

    def test_patch_validation_reuse_revision_and_unused_bad_frame(self):
        self.run_mode('patch')

    def test_whole_img_validation_does_not_block_hot_frames(self):
        self.run_mode('patch-nonblocking')

    def test_validation_completion_respects_concurrent_path_rejection(self):
        self.run_mode('patch-rejected-during-validation')

    def test_palette_alias_refreshes_real_source_lru(self):
        self.run_mode('alias-palette')

    def test_hd_alias_refreshes_real_source_lru(self):
        self.run_mode('alias-hd')

if __name__ == '__main__':
    unittest.main()
