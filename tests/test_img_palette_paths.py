"""Native missing-name palette fallback (CN July 148BEBD10 / 148BECF00)."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid
import zlib

sys.dont_write_bytecode = True
from test_npk_priority import write_package, image, RED, GREEN, BLUE

ROOT = Path(__file__).resolve().parents[1]
BASE = 'sprite/character/swordman/equipment/weapon/katana/'
PACKAGE = 'sprite_character_swordman_equipment_weapon_katana.NPK'


def paletted():
    colors = [RED, GREEN] + [BLUE] * 10
    palettes = struct.pack('<I', len(colors))
    for color in colors:
        palettes += struct.pack('<I', 2) + bytes(4) + bytes(color)
    pixels = zlib.compress(bytes([1]) * 4)
    header = b'Neople Img File\0' + struct.pack('<4I', 36, 0, 6, 1)
    return header + palettes + struct.pack('<9i', 14, 6, 2, 2, len(pixels), 0, 0, 2, 2) + pixels


class PalettePathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/img_missing_20261021' / ('palette_' + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        cls.exe = cls.folder / 'palette_paths.exe'
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        command = [str(compiler), '-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE',
                   '-municode', '-static', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor/zlib'),
                   str(ROOT / 'src/protocol.cpp'), str(ROOT / 'src/resources.cpp'),
                   str(ROOT / 'tests/palette_paths_native.cpp'),
                   str(ROOT / 'vendor/zlib/libz.a'), '-o', str(cls.exe)]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=120)
        (cls.folder / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(result.stderr)
        print('PALETTE_VALIDATION=' + str(cls.folder), flush=True)

    def packs(self, entries, package=PACKAGE):
        folder = self.folder / self._testMethodName
        folder.mkdir()
        write_package(folder / package, entries)
        return folder

    def frame(self, packs, name, frame=0, patches=True):
        logical = name if name.startswith('sprite/') else BASE + name
        result = subprocess.run([str(self.exe), str(packs), str(int(patches)), str(frame),
                                 'single', logical], capture_output=True, text=True, encoding='utf-8')
        (packs / ('result_' + name.replace('/', '_') + '.json')).write_text(json.dumps({
            'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}), encoding='utf-8')
        return result

    def test_missing_0111_uses_0100_palette_11(self):
        packs = self.packs({BASE + 'katana0100b.img': paletted()})
        result = self.frame(packs, 'katana0111b.img')
        self.assertEqual(result.returncode, 0, result.stderr)
        got = json.loads(result.stdout)
        self.assertEqual(got['pixel'], list(BLUE))
        self.assertEqual(got['fallbacks'], 0)
        self.assertEqual(got['actual_path'], BASE + 'katana0111b.img')

    def test_distinct_alias_palettes_do_not_share_cached_frame(self):
        packs = self.packs({BASE + 'katana0100c.img': paletted()})
        names = ['katana0101c.img', 'katana0111c.img', 'katana0100c.img', 'katana0101c.img', 'katana0111c.img']
        result = subprocess.run([str(self.exe), str(packs), '1', '0', 'single',
                                 *[BASE + name for name in names]], capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stderr)
        samples = json.loads(result.stdout)['samples']
        self.assertEqual([x['pixel'] for x in samples], [list(x) for x in [GREEN, BLUE, RED, GREEN, BLUE]])
        self.assertEqual([x['actual_path'] for x in samples], [BASE + name for name in names])

    def test_alias_patch_rejection_is_shared_with_another_reader(self):
        packs = self.packs({BASE + 'katana0100b.img': paletted()})
        write_package(packs / '!patch.npk', {BASE + 'katana0100b.img': b'broken image'})
        result = subprocess.run([str(self.exe), str(packs), '1', '0', 'shared',
                                 BASE + 'katana0111b.img', BASE + 'katana0101b.img'],
                                capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stderr)
        samples = json.loads(result.stdout)['samples']
        self.assertEqual([x['pixel'] for x in samples], [list(BLUE), list(GREEN)])
        self.assertEqual([x['revision'] for x in samples], [1, 1])

    def test_exact_image_has_priority(self):
        packs = self.packs({BASE + 'katana0100b.img': paletted(), BASE + 'katana0111b.img': image(GREEN)})
        result = self.frame(packs, 'katana0111b.img')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['pixel'], list(GREEN))

    def test_existing_image_bad_frame_does_not_switch_names(self):
        packs = self.packs({BASE + 'katana0100b.img': image(RED, RED), BASE + 'katana0111b.img': image(GREEN)})
        result = self.frame(packs, 'katana0111b.img', 1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('native missing-resource fallback absent', result.stderr)

    def test_alias_uses_applied_patch_selection(self):
        packs = self.packs({BASE + 'katana0100b.img': paletted()})
        write_package(packs / '!patch.npk', {BASE + 'katana0100b.img': image(GREEN)})
        for enabled, color in [(True, GREEN), (False, BLUE)]:
            result = self.frame(packs, 'katana0111b.img', patches=enabled)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)['pixel'], list(color))

    def test_missing_non_numeric_name_still_reports_missing(self):
        packs = self.packs({BASE + 'katana0100b.img': paletted()})
        result = self.frame(packs, 'absent.img')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('native missing-resource fallback absent', result.stderr)

    def test_missing_ui_name_uses_2x_same_frame_and_original_metadata(self):
        logical = 'sprite/interface/commoncomponent.img'
        hd = 'sprite/interface/(2x)commoncomponent.img'
        packs = self.packs({hd: image(RED, GREEN, BLUE)}, 'sprite_interface.NPK')
        result = self.frame(packs, logical, 2)
        self.assertEqual(result.returncode, 0, result.stderr)
        got = json.loads(result.stdout)
        self.assertEqual(got['pixel'], list(BLUE))
        self.assertEqual(got['metadata'], [0, 0, 2, 2, 2, 2])
        self.assertEqual(got['geometry_scale'], .5)
        self.assertEqual(got['actual_path'], logical)
        self.assertEqual(got['fallbacks'], 0)
        direct = self.frame(packs, hd, 2)
        self.assertEqual(json.loads(direct.stdout)['pixel'], got['pixel'])
        self.assertEqual(json.loads(direct.stdout)['geometry_scale'], 1)

    def test_2x_alias_does_not_change_explicit_hd_cached_frame_geometry(self):
        logical = 'sprite/interface/commoncomponent.img'
        hd = 'sprite/interface/(2x)commoncomponent.img'
        packs = self.packs({hd: image(RED)}, 'sprite_interface.NPK')
        result = subprocess.run([str(self.exe), str(packs), '1', '0', 'single', logical, hd, logical],
                                capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([x['geometry_scale'] for x in json.loads(result.stdout)['samples']], [.5, 1, .5])

    def test_2x_does_not_override_an_existing_original_ui_image(self):
        logical = 'sprite/interface/commoncomponent.img'
        packs = self.packs({logical: image(RED), 'sprite/interface/(2x)commoncomponent.img': image(BLUE)},
                           'sprite_interface.NPK')
        result = self.frame(packs, logical)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['pixel'], list(RED))

    def test_2x_alias_obeys_applied_patch_selection(self):
        logical = 'sprite/interface/newstyle/windows/chatting/chatting_new.img'
        hd = 'sprite/interface/newstyle/windows/chatting/(2x)chatting_new.img'
        packs = self.packs({hd: image(RED)}, 'sprite_interface_newstyle_windows_chatting.NPK')
        write_package(packs / '!patch.npk', {hd: image(GREEN)})
        for enabled, color in [(True, GREEN), (False, RED)]:
            result = self.frame(packs, logical, patches=enabled)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)['pixel'], list(color))

    def test_absent_numeric_palette_candidate_still_tries_requested_2x_name(self):
        logical = 'sprite/interface/panel0111.img'
        packs = self.packs({'sprite/interface/(2x)panel0111.img': image(GREEN)}, 'sprite_interface.NPK')
        result = self.frame(packs, logical)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['pixel'], list(GREEN))

    def test_existing_ui_bad_frame_does_not_switch_to_2x_name(self):
        logical = 'sprite/interface/panel0111.img'
        packs = self.packs({logical: image(RED), 'sprite/interface/(2x)panel0111.img': image(GREEN, GREEN)},
                           'sprite_interface.NPK')
        result = self.frame(packs, logical, 1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('native missing-resource fallback absent', result.stderr)


if __name__ == '__main__':
    unittest.main()
