"""Behavior checks produced by the real native UI revision test window."""
import json
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


class NativeUiRevisionTests(unittest.TestCase):
    def test_frame_steps_splitters_hierarchy_scrollbars_and_backgrounds(self):
        report = ROOT / 'validation' / ('ui_revision_' + uuid.uuid4().hex[:8] + '.json')
        process = subprocess.Popen(
            [str(ROOT / 'build' / 'rep_player.exe'), '--ui-revision-test', str(report)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding='utf8', errors='replace', cwd=ROOT)
        try:
            stdout, stderr = process.communicate(timeout=45)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.communicate(timeout=5)
            self.fail('native --ui-revision-test did not complete frame-step and layout checks')
        self.assertEqual(process.returncode, 0, stderr or stdout)
        self.assertTrue(report.is_file(), 'native --ui-revision-test did not write a fresh report')
        data = json.loads(report.read_text(encoding='utf8'))
        for key in ('step_next_adjacent', 'step_previous_restores', 'step_pauses',
                    'step_hidden_retained', 'splitter_vertical_resize',
                    'splitter_horizontal_resize', 'hierarchy_indented',
                    'dark_scrollbars', 'static_backgrounds_match', 'pass'):
            with self.subTest(check=key):
                self.assertIs(data.get(key), True, key)


if __name__ == '__main__':
    unittest.main()
