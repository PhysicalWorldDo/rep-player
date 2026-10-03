import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay
from test_npk_reader import npk, img_header, rec

ROOT = Path(__file__).resolve().parents[1]


class RecordedFrameStepTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'frame_step'
        cls.folder.mkdir(exist_ok=True)
        cls.assets = cls.folder / 'client' / 'ImagePacks2'
        cls.assets.mkdir(parents=True, exist_ok=True)
        raw = bytes((0, 0, 255, 255)) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (cls.assets / 'sprite_test.NPK').write_bytes(npk(image))
        native = bytearray(40)
        native[4] = 1
        struct.pack_into('<2f', native, 16, 1, 1)
        struct.pack_into('<I', native, 28, 0xffffffff)
        draw = struct.pack('<3I', 42, 40, 0) + native
        # The second recorded frame updates a persistent per-layer camera.
        # Realtime selection can skip it; backward stepping must reconstruct it.
        camera = struct.pack('<I2fI3f', 50, 2, 0, 0, 16, 16, 1)
        initial_camera = struct.pack('<I2fI3f', 50, 0, 0, 0, 16, 16, 1)
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        cls.replay = cls.folder / 'duplicates_and_camera.rep'
        cls.replay.write_bytes(pack_replay(1.7, {0: draw, 1: camera + draw, 2: initial_camera + draw},
            [(10, (2,), struct.pack('<2h', 2, 2)), (10, (1,), struct.pack('<2h', 4, 2)),
             (30, (0,), struct.pack('<2h', 6, 2)), (40, (0,), struct.pack('<2h', 8, 2))],
            ['sprite/test/frame.img'], header=header))
        cls.empty = cls.folder / 'empty.rep'
        cls.empty.write_bytes(pack_replay(1.7, {0: draw}, [], ['sprite/test/frame.img'], header=header))

    def run_actions(self, actions, replay=None):
        result = subprocess.run([str(ROOT / 'build' / 'rep_gpu.exe'), '--frame-step',
            str(replay or self.replay), str(self.assets), ','.join(actions)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.strip(), 'recorded-frame stepping is not implemented')
        return json.loads(result.stdout)

    def test_adjacent_records_include_duplicate_timestamps_and_clamp_boundaries(self):
        data = self.run_actions(['select=0', 'next', 'next', 'prev', 'prev', 'prev',
                                 'next', 'next', 'next', 'next'])
        rows = data['snapshots']
        self.assertEqual(data['frame_count'], 4)
        self.assertEqual([x['ordinal'] for x in rows], [0, 1, 2, 1, 0, 0, 1, 2, 3, 3])
        self.assertEqual([x['timestamp'] for x in rows], [10, 10, 30, 10, 10, 10, 10, 30, 40, 40])
        self.assertEqual([x['changed'] for x in rows], [True, True, True, True, True, False, True, True, True, False])
        self.assertTrue(all(x['paused'] for x in rows[1:]))
        self.assertTrue(rows[-1]['ended'])
        self.assertTrue(all(x['crc'] == x['reference_crc'] for x in rows))

    def test_steps_after_realtime_skip_reconstruct_persistent_state(self):
        rows = self.run_actions(['select=0', 'select=31', 'prev', 'next', 'prev'])['snapshots']
        self.assertEqual([x['ordinal'] for x in rows], [0, 3, 2, 3, 2])
        self.assertTrue(all(x['crc'] == x['reference_crc'] for x in rows[2:]))
        # Next after a skipped camera update must also resolve an exact recorded frame.
        rows = self.run_actions(['select=0', 'select=11', 'next'])['snapshots']
        self.assertEqual(rows[-1]['ordinal'], 3)
        self.assertEqual(rows[-1]['crc'], rows[-1]['reference_crc'])

    def test_hidden_images_stay_hidden_across_both_directions(self):
        rows = self.run_actions(['select=0', 'hide', 'next', 'next', 'prev'])['snapshots']
        for row in rows[1:]:
            self.assertEqual(row['hidden_count'], 1)
            self.assertEqual(row['crc'], row['reference_crc'])
            self.assertTrue(row['images'])
            self.assertTrue(all(x['hidden'] and not x['drawn'] for x in row['images']))

    def test_resume_starts_from_selected_timestamp(self):
        rows = self.run_actions(['select=0', 'next', 'next', 'prev', 'resume'])['snapshots']
        self.assertEqual(rows[-1]['ordinal'], 1)
        self.assertEqual(rows[-1]['timestamp'], 10)
        self.assertFalse(rows[-1]['paused'])
        self.assertGreaterEqual(rows[-1]['elapsed'], 30)

    def test_empty_replay_steps_are_safe(self):
        data = self.run_actions(['next', 'prev'], self.empty)
        self.assertEqual(data['frame_count'], 0)
        self.assertTrue(all(not x['changed'] and x['ended'] for x in data['snapshots']))


if __name__ == '__main__':
    unittest.main()
