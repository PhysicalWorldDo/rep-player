"""Exercise locale font availability and Windows Unicode paths with real fonts."""
import json
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL_FONTS = Path(r'D:\115us\client\Fonts')
CHINESE_CLIENT = Path('E:/WeGameApps/地下城与勇士：创新世纪')


class NativeClientFontTests(unittest.TestCase):
    def font(self, root=None):
        command = [str(ROOT / 'build' / 'rep_gpu.exe'), '--font',
                   '2', '18', '400', '0', '2', '1', '1', 'Skill 01']
        if root is not None:
            command.append(str(root))
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.strip(), 'native font check returned no result')
        return json.loads(result.stdout)

    def fixture(self, name):
        folder = ROOT / 'validation' / 'font_fix' / (name + '_' + uuid.uuid4().hex[:8])
        folder.mkdir(parents=True)
        return folder

    def test_existing_font_loads_from_unicode_client_directory(self):
        folder = self.fixture('中文字体')
        shutil.copyfile(ORIGINAL_FONTS / 'DNFForgedBlade-Medium.ttf',
                        folder / 'DNFForgedBlade-Medium.ttf')
        self.assertEqual(self.font(folder), self.font())

    def test_chinese_family_works_without_korean_client_font_names(self):
        folder = self.fixture('chinese_family')
        # Identical font bytes isolate filename resolution from glyph rendering.
        shutil.copyfile(ORIGINAL_FONTS / 'DNFForgedBlade-Medium.ttf',
                        folder / 'NotoSansSC-Regular.otf')
        self.assertEqual(self.font(folder), self.font())

    @unittest.skipUnless(CHINESE_CLIENT.is_dir(), 'selected Chinese client is unavailable')
    def test_dark_flame_vp2_consumes_with_selected_chinese_client_fonts(self):
        folder = self.fixture('darkflame')
        report = folder / 'consume.json'
        result = subprocess.run(
            [str(ROOT / 'build' / 'rep_gpu.exe'), '--consume',
             str(CHINESE_CLIENT / 'Replay' / 'SkillReplay' / 'Swordman' / 'DarkFlameSlash_VP2.rep'),
             str(report), str(folder / 'last.rgba'), str(CHINESE_CLIENT / 'ImagePacks2')],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report.is_file(), 'consume did not produce a fresh report')
        data = json.loads(report.read_text(encoding='utf8'))
        self.assertIs(data['exact_eof'], True)
        self.assertGreater(data['scenes'], 0)
        self.assertGreater(data['opcodes'][48] + data['opcodes'][5], 0)


if __name__ == '__main__':
    unittest.main()
