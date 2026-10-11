"""Shared category choices and per REP manual exceptions, without a UI or GPU."""
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


class ImageVisibilityPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'image_visibility_20261011' / ('policy_' + uuid.uuid4().hex[:10])
        cls.folder.mkdir(parents=True)
        cls.exe = cls.folder / 'policy_probe.exe'
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        cls.compiled = subprocess.run([str(compiler), '-std=c++20', '-O2', '-Wall', '-Wextra', '-static',
            '-I', str(ROOT / 'src'), str(ROOT / 'tests/image_visibility_policy_native.cpp'), '-o', str(cls.exe)],
            capture_output=True, text=True, timeout=30)
        (cls.folder / 'compile.log').write_text(cls.compiled.stdout + cls.compiled.stderr, encoding='utf8')
        print('IMAGE_VISIBILITY_POLICY_VALIDATION=' + str(cls.folder), flush=True)

    def case(self, name):
        self.assertEqual(self.compiled.returncode, 0, 'Shared image visibility policy is missing or does not compile: ' + self.compiled.stderr)
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True, timeout=10)
        (self.folder / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_defaults_and_shared_normalization(self): self.case('defaults')
    def test_categories_keep_effect_paths_visible(self): self.case('categories')
    def test_manual_exceptions_survive_category_round_trip(self): self.case('manual')
    def test_show_all_resets_choices_and_exceptions(self): self.case('show_all')
    def test_only_category_choices_persist(self): self.case('settings')
    def test_invalid_settings_default_every_category_off(self): self.case('invalid')


if __name__ == '__main__':
    unittest.main()
