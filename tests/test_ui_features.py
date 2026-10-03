import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeUiFeatureTests(unittest.TestCase):
    def test_pause_search_language_client_and_image_visibility(self):
        report = ROOT / 'validation' / 'ui_features.json'
        result = subprocess.Popen([str(ROOT / 'build' / 'rep_player.exe'),
                                   '--ui-feature-test', str(report), '--client', r'D:\115us\client'],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            result.communicate(timeout=45)
        except subprocess.TimeoutExpired:
            result.terminate()
            result.communicate(timeout=5)
            self.fail('native window did not complete the requested UI feature checks')
        self.assertEqual(result.returncode, 0)
        out = json.loads(report.read_text(encoding='utf8'))
        for key in ('pause_freezes', 'resume_advances', 'search_zh', 'search_en',
                    'search_vp', 'language_preserves_search_selection',
                    'client_roots', 'image_hide_changes_frame',
                    'image_restore_matches_frame', 'hidden_remains_listed',
                    'image_state_shared', 'pass'):
            self.assertTrue(out[key], key)


if __name__ == '__main__':
    unittest.main()
