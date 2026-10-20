"""Broken patch IMG entries fall back to the original, per logical path."""
import json
from pathlib import Path
import struct
import subprocess
import unittest
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
PRISM = (ROOT / 'native/rep').is_dir()
PLAYER = ROOT.parent / 'rep_player' if PRISM else ROOT
SOURCE = ROOT / 'native/rep' if PRISM else ROOT / 'src'
LOGICAL = 'sprite/test/frame.img'


def image(color=(255, 0, 0, 255), broken_frame=False):
    raw = bytes((color[2], color[1], color[0], color[3])) * 4
    header = b'Neople Img File\0' + struct.pack('<4I', 72, 0, 2, 2)
    good = struct.pack('<9i', 16, 5, 2, 2, 16, 0, 0, 2, 2)
    bad = struct.pack('<9i', 16, 6, 2, 2, 3, 0, 0, 2, 2)
    return header + good + (bad if broken_frame else good) + raw + (b'bad' if broken_frame else raw)


def package(path, entries):
    seed = (b'puchikon@neople dungeon and fighter ' + b'DNF' * 100)[:255] + b'\0'
    offset = 20 + len(entries) * 264
    table, payloads, locations = [], [], {}
    for name, data in entries.items():
        encoded = bytes(a ^ b for a, b in zip(name.encode().ljust(256, b'\0'), seed))
        table.append(struct.pack('<II', offset, len(data)) + encoded)
        payloads.append(data)
        locations[name] = (offset, len(data))
        offset += len(data)
    path.write_bytes(b'NeoplePack_Bill\0' + struct.pack('<I', len(entries)) + b''.join(table + payloads))
    return locations


class PatchFallbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / ('patch_fallback_' + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        probe = cls.folder / 'probe.cpp'
        probe.write_text('''#include "bindings.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    rep::Assets assets(argv[1]);std::string path=rep::utf8(argv[2]);
#ifdef PRISM
    if(argc>3&&std::wstring_view(argv[3])==L"raw"){
        auto raw=assets.rawImage(path);rep::Img img(raw);std::cout<<rep::crc(img.frame(1)->cropped());return 0;
    }
#endif
    if(argc>3&&std::wstring_view(argv[3])==L"shared"){
        rep::Assets consumer(assets.index());consumer.frame(path,0);assets.frame(path,1,0,false);
#ifdef PRISM
        auto raw=consumer.rawImage(path);rep::Img img(raw);std::cout<<rep::crc(img.frame(0)->cropped());
#else
        std::cout<<rep::crc(consumer.frame(path,0,0,false)->cropped());
#endif
        return 0;
    }
    if(argc>3&&(std::wstring_view(argv[3])==L"binder"||std::wstring_view(argv[3])==L"binder-internal")){
        rep::Gpu gpu({});rep::Binder binder(gpu,assets);rep::Effect effect;effect.present=true;effect.type=17;effect.floats.resize(25);effect.floats[10]=0;
        auto pixels=std::make_shared<rep::Pixels>();pixels->width=pixels->height=2;pixels->rgba.resize(16,255);auto texture=gpu.texture(pixels);
        rep::ShaderContext context;context.width=context.height=2;
        if(std::wstring_view(argv[3])==L"binder-internal"){effect.floats[10]=6;auto material=binder.bind(&effect,texture,context);std::cout<<"["<<int(material.textures[1]->source->rgba[0])<<","<<int(material.textures[2]->source->rgba[0])<<"]";return 0;}
        auto before=binder.bind(&effect,texture,context);assets.frame(path,6,0,false);auto after=binder.bind(&effect,texture,context);
        std::cout<<"["<<int(before.textures[1]->source->rgba[0])<<","<<int(after.textures[1]->source->rgba[0])<<"]";return 0;
    }
    auto first=assets.frame(path,0);auto frame=assets.frame(path,1,0,false);auto again=assets.frame(path,0,0,false);
    auto other=assets.frame("sprite/test/other.img",0,0,false);
    std::cout<<"["<<rep::crc(frame->cropped())<<","<<rep::crc(again->cropped())<<","<<rep::crc(other->cropped())<<","<<rep::crc(first->cropped())<<"]";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
''', encoding='utf-8')
        cls.exe = cls.folder / 'probe.exe'
        flags = ['-std=c++20', '-O2', '-DNOMINMAX', '-municode', '-static', '-I', str(SOURCE), '-I', str(PLAYER / 'vendor/zlib')]
        if PRISM:
            flags += ['-DPRISM']
        subprocess.run([str(PLAYER / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'), *flags,
                        str(SOURCE / 'protocol.cpp'), str(SOURCE / 'resources.cpp'), str(SOURCE / 'gpu.cpp'), str(SOURCE / 'bindings.cpp'), str(probe),
                        str(PLAYER / 'vendor/zlib/libz.a'), '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-o', str(cls.exe)], check=True, capture_output=True, timeout=120)

    def packs(self, bad, native_index=False):
        packs = self.folder / self._testMethodName
        packs.mkdir()
        original_name = 'unrelated.NPK' if native_index else 'sprite_test.NPK'
        locations = package(packs / original_name, {LOGICAL: image(), 'sprite/test/other.img': image()})
        if native_index:
            package(packs / 'sprite.NPK', {})
            name = original_name.encode()
            raw = struct.pack('<II', len(locations), len(name)) + name
            for path, (offset, length) in locations.items():
                encoded = path.encode()
                raw += struct.pack('<I', len(encoded)) + encoded + struct.pack('<II', offset, length)
            zipped = zlib.compress(raw)
            encoded = bytes((((value ^ 0xaa) + n * 7) & 255) ^ 0xaa for n, value in enumerate(zipped))
            (packs / 'NpkIndex.etc').write_bytes(struct.pack('<II', len(raw), len(encoded)) + encoded)
        package(packs / '!first.NPK', {LOGICAL: bad, 'sprite/test/other.img': image((0, 255, 0, 255))})
        package(packs / '!second.NPK', {LOGICAL: image((0, 0, 255, 255))})
        return packs

    def verify(self, packs):
        result = subprocess.run([str(self.exe), str(packs), LOGICAL], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        red = zlib.crc32(bytes((255, 0, 0, 255)) * 4)
        green = zlib.crc32(bytes((0, 255, 0, 255)) * 4)
        self.assertEqual(json.loads(result.stdout), [red, red, green, red])

    def test_invalid_img_header_uses_original_and_keeps_other_patch_images(self):
        self.verify(self.packs(b'broken IMG'))

    def test_later_decode_failure_replaces_the_whole_patch_img(self):
        self.verify(self.packs(image((0, 0, 255, 255), broken_frame=True)))

    def test_native_index_preserves_original_entry_beneath_overlay(self):
        self.verify(self.packs(image(broken_frame=True), native_index=True))

    def test_short_patch_frame_table_uses_original(self):
        self.verify(self.packs(image()[:-1]))

    def test_source_rejection_is_shared_with_other_readers_and_exports(self):
        raw = image((0, 0, 255, 255))
        one = raw[:16] + struct.pack('<4I', 36, 0, 2, 1) + raw[32:68] + raw[104:120]
        packs = self.packs(one)
        result = subprocess.run([str(self.exe), str(packs), LOGICAL, 'shared'], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(int(result.stdout), zlib.crc32(bytes((255, 0, 0, 255)) * 4))

    def test_shader_builtin_and_material_caches_drop_rejected_patch_pixels(self):
        packs = self.packs(image())
        shader = 'sprite/shader/mask.img'
        def repeated(color, count):
            raw = image(color)
            return raw[:16] + struct.pack('<4I', 36 * count, 0, 2, count) + raw[32:68] * count + raw[104:120] * count
        package(packs / 'sprite_shader.NPK', {shader: repeated((255, 0, 0, 255), 7)})
        package(packs / '!first.NPK', {shader: repeated((0, 0, 255, 255), 5)})
        result = subprocess.run([str(self.exe), str(packs), shader, 'binder'], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), [0, 255])
        internal = subprocess.run([str(self.exe), str(packs), shader, 'binder-internal'], capture_output=True, text=True, timeout=15)
        self.assertEqual(internal.returncode, 0, internal.stderr)
        self.assertEqual(json.loads(internal.stdout), [255, 255], 'one material cannot mix the rejected patch and original')

    @unittest.skipUnless(PRISM, 'raw IMG export belongs to Skill Prism')
    def test_raw_export_uses_original_after_patch_decode_failure(self):
        packs = self.packs(image(broken_frame=True))
        result = subprocess.run([str(self.exe), str(packs), LOGICAL, 'raw'], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(int(result.stdout), zlib.crc32(bytes((255, 0, 0, 255)) * 4))


if __name__ == '__main__':
    unittest.main()
