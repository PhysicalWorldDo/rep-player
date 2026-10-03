"""Exercise bulk IMG removal through the real native controls and render worker."""
import json
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


class NativeImgRemovalTests(unittest.TestCase):
    def test_bulk_removal_preserves_effects_and_can_restore_everything(self):
        report = ROOT / 'validation' / ('ui_img_filters_' + uuid.uuid4().hex[:8] + '.json')
        process = subprocess.Popen(
            [str(ROOT / 'build' / 'rep_player.exe'), '--ui-img-filter-test', str(report), '--client', r'D:\115us\client'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=ROOT)
        try:
            stdout, stderr = process.communicate(timeout=40)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.communicate(timeout=5)
            self.fail('native bulk IMG removal checks did not finish')
        self.assertEqual(process.returncode, 0, (stderr or stdout).decode('utf8', errors='replace'))
        self.assertTrue(report.is_file(), 'native bulk IMG removal checks did not write a fresh report')
        result = json.loads(report.read_text(encoding='utf8'))
        for key in ('bulk_buttons_present', 'bulk_buttons_fit_minimum_sidebar',
                    'bulk_bilingual_labels', 'bulk_path_heuristics',
                    'bulk_background_hidden', 'bulk_monsters_hidden', 'bulk_characters_hidden',
                    'bulk_whole_replay_paths', 'bulk_effects_preserved', 'bulk_preview_changes',
                    'bulk_shared_visibility', 'bulk_manual_checkbox_restore',
                    'bulk_show_all_restores', 'bulk_new_replay_resets', 'pass'):
            with self.subTest(check=key):
                self.assertIs(result.get(key), True, key)


if __name__ == '__main__':
    unittest.main()
