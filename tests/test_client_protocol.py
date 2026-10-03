"""Client profile reaches playback, inspection and exports through real entry points."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay

ROOT = Path(__file__).resolve().parents[1]


class ClientProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'protocol_102' / ('client_' + uuid.uuid4().hex[:8])
        cls.client = cls.folder / 'client'
        (cls.client / 'ImagePacks2').mkdir(parents=True)
        # A module name is a source selector, never a payload-length heuristic.
        (cls.client / 'DNF.exe').write_bytes(b'owned test marker; never executed')
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        header += b'\x0c' + struct.pack('<H', 3) + bytes(126)
        command = struct.pack('<III', 22, 0x12345678, 17)
        data = pack_replay(1.4, {0: command}, [(0, (0,), b''), (1000, (0,), b'')], header=header)
        for path in ('Swordman/BloodSword.rep', 'Priest/DivinePunishment.rep'):
            replay = cls.client / 'Replay' / 'SkillReplay' / path
            replay.parent.mkdir(parents=True, exist_ok=True)
            replay.write_bytes(data)
        cls.replay = cls.client / 'Replay' / 'SkillReplay' / 'Swordman' / 'BloodSword.rep'

    def export(self, client, *options):
        result = subprocess.run([
            str(ROOT / 'build' / 'rep_export.exe'), '--client', str(client), '--replay', str(self.replay),
            '--format', 'png', '--fps', '30', '--alpha', '1', '--output', str(self.folder),
            '--name', self._testMethodName, *options,
        ], capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual(data['executed_scenes'], 2)
        self.assertEqual(len(list(Path(data['output']).glob('frame_*.png'))), 31)

    def test_export_selects_dnf_profile_from_selected_client(self):
        self.export(self.client)

    def test_export_accepts_explicit_profile_for_custom_client(self):
        custom = self.folder / 'custom_client'
        (custom / 'ImagePacks2').mkdir(parents=True)
        self.export(custom, '--profile', 'dnf-july', '--codepage', '949')

    def test_export_rejects_partial_codepage_argument(self):
        result = subprocess.run([
            str(ROOT / 'build' / 'rep_export.exe'), '--client', str(self.client), '--replay', str(self.replay),
            '--format', 'png', '--output', str(self.folder), '--name', self._testMethodName,
            '--codepage', '949junk',
        ], capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('codepage', result.stderr.lower())

    def test_gui_playback_and_inspection_use_same_dnf_profile(self):
        report = self.folder / 'ui.json'
        result = subprocess.run([
            str(ROOT / 'build' / 'rep_player.exe'), '--ui-test', str(report), '--client', str(self.client),
        ], capture_output=True, timeout=35)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        data = json.loads(report.read_text(encoding='utf-8'))
        self.assertTrue(data['pass'] and data['autoplay'] and data['frozen'])
        self.assertEqual(data['tree_replays'], 2)


if __name__ == '__main__':
    unittest.main()
