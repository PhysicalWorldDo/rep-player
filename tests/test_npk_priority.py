"""Synthetic NPK priority tests; all artifacts stay in the authorized validation tree."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
VALIDATION = ROOT / 'validation' / 'npk_priority_20261008'
LOGICAL = 'sprite/character/test/frame.img'
ORIGINAL = 'sprite_character_test.NPK'
RED = (255, 0, 0, 255)
GREEN = (0, 255, 0, 255)
BLUE = (0, 0, 255, 255)
YELLOW = (255, 255, 0, 255)


def image(*colors):
    header = b'Neople Img File\0' + struct.pack('<4I', len(colors) * 36, 0, 2, len(colors))
    records = b''.join(struct.pack('<9i', 16, 5, 2, 2, 16, 0, 0, 2, 2) for _ in colors)
    pixels = b''.join(bytes((color[2], color[1], color[0], color[3])) * 4 for color in colors)
    return header + records + pixels


def write_package(path, entries):
    seed = (b'puchikon@neople dungeon and fighter ' + b'DNF' * 100)[:255] + b'\0'
    offset = 20 + 264 * len(entries)
    records, payloads, locations = [], [], {}
    for logical, payload in entries.items():
        name = logical.encode('utf-8')
        if len(name) >= 256:
            raise ValueError('fixture logical path is too long')
        encoded = bytes(a ^ b for a, b in zip(name.ljust(256, b'\0'), seed))
        records.append(struct.pack('<II', offset, len(payload)) + encoded)
        payloads.append(payload)
        locations[logical] = (offset, len(payload))
        offset += len(payload)
    path.write_bytes(b'NeoplePack_Bill\0' + struct.pack('<I', len(entries))
                     + b''.join(records) + b''.join(payloads))
    return locations


def write_native_index(path, package_name, locations):
    package = package_name.encode('utf-8')
    raw = struct.pack('<II', len(locations), len(package)) + package
    for logical, (offset, length) in locations.items():
        name = logical.encode('utf-8')
        raw += struct.pack('<I', len(name)) + name + struct.pack('<II', offset, length)
    compressed = zlib.compress(raw)
    encoded = bytes((((byte ^ 0xaa) + index * 7) & 255) ^ 0xaa
                    for index, byte in enumerate(compressed))
    path.write_bytes(struct.pack('<II', len(raw), len(encoded)) + encoded)


class NpkPriorityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = VALIDATION / ('resources_tests_' + uuid.uuid4().hex[:10])
        cls.folder.mkdir(parents=True)
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        cls.exe = cls.folder / 'npk_priority_resources_native.exe'
        command = [str(compiler), '-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE',
                   '-municode', '-static', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'vendor/zlib'),
                   str(ROOT / 'src/protocol.cpp'), str(ROOT / 'src/resources.cpp'),
                   str(ROOT / 'tests/npk_priority_resources_native.cpp'),
                   str(ROOT / 'vendor/zlib/libz.a'), '-o', str(cls.exe)]
        completed = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=120)
        (cls.folder / 'compile.stdout.txt').write_text(completed.stdout, encoding='utf-8')
        (cls.folder / 'compile.stderr.txt').write_text(completed.stderr, encoding='utf-8')
        if completed.returncode:
            raise RuntimeError(completed.stderr)
        cls.reports = {}
        print('NPK_PRIORITY_VALIDATION=' + str(cls.folder), flush=True)

    @classmethod
    def tearDownClass(cls):
        (cls.folder / 'observations.json').write_text(json.dumps(cls.reports, ensure_ascii=False, indent=2),
                                                     encoding='utf-8')

    def packs(self):
        path = self.folder / self._testMethodName / 'ImagePacks2'
        path.mkdir(parents=True)
        return path

    def original(self, packs, colors=(RED,), extra=None):
        entries = {LOGICAL: image(*colors)}
        if extra:
            entries.update(extra)
        return write_package(packs / ORIGINAL, entries)

    def run_frame(self, packs, logical=LOGICAL, frame=0, *, success=True):
        result = subprocess.run([str(self.exe), 'frame', str(packs), logical, str(frame)],
                                capture_output=True, text=True, encoding='utf-8', timeout=20)
        record = {'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
        self.reports.setdefault(self._testMethodName, []).append(record)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)
        return result

    def test_arbitrary_chinese_early_patch_overrides_guessed_original(self):
        packs = self.packs(); self.original(packs)
        write_package(packs / '%%中文补丁.npk', {LOGICAL: image(GREEN)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(GREEN))

    def test_early_patch_overrides_native_etc_entry(self):
        packs = self.packs(); locations = self.original(packs)
        write_native_index(packs / 'NpkIndex.etc', ORIGINAL, locations)
        write_package(packs / '!补丁.NPK', {LOGICAL: image(GREEN)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(GREEN))

    def test_number_sort_is_character_order(self):
        packs = self.packs(); self.original(packs)
        write_package(packs / '!2.npk', {LOGICAL: image(BLUE)})
        write_package(packs / '!10.npk', {LOGICAL: image(GREEN)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(GREEN))

    def test_filename_sort_preserves_case(self):
        packs = self.packs(); self.original(packs)
        write_package(packs / '!a_lower.npk', {LOGICAL: image(GREEN)})
        write_package(packs / '!Z_upper.NPK', {LOGICAL: image(BLUE)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(BLUE))

    def test_two_chinese_patches_use_unsigned_utf16_order(self):
        packs = self.packs(); self.original(packs)
        write_package(packs / '!甲.npk', {LOGICAL: image(GREEN)})
        write_package(packs / '!乙.npk', {LOGICAL: image(BLUE)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(BLUE))

    def test_sprite_dot_boundary_does_not_read_bad_later_packages(self):
        packs = self.packs(); self.original(packs)
        (packs / 'sprite.NPK').write_bytes(b'official boundary is intentionally not read')
        (packs / 'sprite_bad_unrelated.NPK').write_bytes(b'broken unrelated fixture')
        write_package(packs / '!early.npk', {LOGICAL: image(GREEN)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(GREEN))

    def test_official_boundary_prefix_ignores_ascii_case(self):
        packs = self.packs(); self.original(packs)
        (packs / 'SPRITE.NPK').write_bytes(b'boundary is intentionally not read')
        (packs / 'Z_later_unrelated.npk').write_bytes(b'broken fixture after uppercase boundary')
        write_package(packs / '!early.npk', {LOGICAL: image(GREEN)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(GREEN))

    def test_invalid_early_package_reports_filename(self):
        packs = self.packs(); self.original(packs)
        (packs / '!broken.npk').write_bytes(b'bad NPK fixture')
        result = self.run_frame(packs, success=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('!broken.npk', result.stderr)

    def test_new_index_after_rename_or_removal_falls_back_to_original(self):
        packs = self.packs(); self.original(packs)
        early = packs / '!patch.npk'
        write_package(early, {LOGICAL: image(GREEN)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(GREEN))
        later = packs / 'zz_patch.npk'; early.rename(later)
        self.assertEqual(self.run_frame(packs)['pixel'], list(RED))
        later.rename(early)
        self.assertEqual(self.run_frame(packs)['pixel'], list(GREEN))
        early.unlink()
        self.assertEqual(self.run_frame(packs)['pixel'], list(RED))

    def test_unpatched_paths_keep_guessed_original(self):
        packs = self.packs(); other = 'sprite/character/test/other.img'
        self.original(packs, extra={other: image(BLUE)})
        write_package(packs / '!partial.npk', {LOGICAL: image(GREEN)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(GREEN))
        self.assertEqual(self.run_frame(packs, other)['pixel'], list(BLUE))

    def test_missing_patch_frame_uses_placeholder_without_original_frame(self):
        packs = self.packs(); self.original(packs, colors=(RED, BLUE))
        write_package(packs / 'sprite_interface.NPK', {'sprite/interface/base.img': image(*([YELLOW] * 92))})
        write_package(packs / '!one_frame.npk', {LOGICAL: image(GREEN)})
        result = self.run_frame(packs, frame=1)
        self.assertEqual(result['pixel'], list(YELLOW))
        self.assertEqual(result['actual_path'], 'sprite/interface/base.img')
        self.assertEqual(result['actual_frame'], 91)
        self.assertEqual(result['fallbacks'], 1)

    def test_no_patch_keeps_original_behavior(self):
        packs = self.packs(); self.original(packs)
        self.assertEqual(self.run_frame(packs)['pixel'], list(RED))

    def test_no_sprite_boundary_keeps_existing_native_index_only(self):
        packs = self.packs()
        locations = write_package(packs / 'original.NPK', {LOGICAL: image(RED)})
        write_native_index(packs / 'NpkIndex.etc', 'original.NPK', locations)
        write_package(packs / '!patch.npk', {LOGICAL: image(GREEN)})
        self.assertEqual(self.run_frame(packs)['pixel'], list(RED))

    def test_shared_index_reuses_catalog_but_not_decoded_images(self):
        packs = self.packs(); self.original(packs)
        write_package(packs / '!patch.npk', {LOGICAL: image(GREEN)})
        result = subprocess.run([str(self.exe), 'snapshot', str(packs), LOGICAL],
                                capture_output=True, text=True, encoding='utf-8', timeout=20)
        self.reports[self._testMethodName] = [{'exit_code': result.returncode,
                                             'stdout': result.stdout, 'stderr': result.stderr}]
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report['supported'], 'Assets cannot share an immutable resource index')
        for name in ['shared', 'private_decoded', 'same_root', 'same_pixels', 'new_index_rejects_bad_package']:
            self.assertTrue(report[name], name)


if __name__ == '__main__':
    unittest.main()
