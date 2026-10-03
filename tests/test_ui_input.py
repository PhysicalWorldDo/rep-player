"""Regression checks through the real native playlist and main input loop."""
import json
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


class NativeUiInputTests(unittest.TestCase):
    def run_native_case(self, case, checks):
        report = ROOT / 'validation' / ('ui_input_' + case + '_' + uuid.uuid4().hex[:8] + '.json')
        process = subprocess.Popen(
            [str(ROOT / 'build' / 'rep_player.exe'), '--ui-input-test', str(report),
             '--input-case', case, '--client', r'D:\115us\client'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding='utf8', errors='replace', cwd=ROOT)
        try:
            stdout, stderr = process.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.communicate(timeout=5)
            self.fail('native input check did not finish: ' + case)
        self.assertTrue(report.is_file(), stderr or stdout or 'native input check did not write a fresh report')
        data = json.loads(report.read_text(encoding='utf8'))
        for key in (*checks, 'pass'):
            with self.subTest(check=key):
                self.assertIs(data.get(key), True, key)
        self.assertEqual(process.returncode, 0, stderr or stdout)

    def test_playlist_clicks_switch_autoplay_and_restart_selected_rep(self):
        self.run_native_case('click', ('click_alias_switches', 'click_row_right_switches',
                                      'click_rapid_latest_plays', 'click_selected_restarts'))

    def test_space_controls_playback_across_panes_but_search_accepts_space(self):
        self.run_native_case('space', ('space_tree_pause_resume', 'space_img_pause_resume',
                                      'space_toolbar_pause_resume', 'space_held_key_single_toggle',
                                      'space_hidden_retained', 'space_search_types_literal'))


if __name__ == '__main__':
    unittest.main()
