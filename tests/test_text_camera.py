"""Text48 camera zoom flag, with both font registries."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay, op

ROOT = Path(__file__).resolve().parents[1]


class TextCameraTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / ('text_camera_' + uuid.uuid4().hex[:8])
        for client in ('cn', 'us'):
            (cls.folder / client / 'ImagePacks2').mkdir(parents=True)
            if client == 'cn':
                (cls.folder / client / 'DNF.exe').write_bytes(b'font-registry fixture')

    def render(self, client, minor, zoom, bypass, glow=False):
        width, height = 128, 96
        header = b'\x0b\0' + struct.pack('<8h', *([width, height] * 4))
        header += b'\x0c' + struct.pack('<H', minor) + bytes(126)
        words = (12, 400, 0, 0, 2, 1, 0, 0xffffffff, 0xff000000, 0, 1)
        text = op(48, struct.pack('<I11II', 0, *words, 2))
        aux = b''
        if minor < 7:
            text += struct.pack('<2f', 70, 45)
        else:
            aux = struct.pack('<2h', 70, 45)
        text += bytes([bypass])
        if glow:
            previous = op(19, b'\1' + struct.pack('<IIBII', 26, 1, 0, 0, 0) + struct.pack('<f', 0))
            effect = op(19, b'\1' + struct.pack('<IIBII', 30, 8, 0, 0, 0) + bytes(32))
            text = previous + effect + text + op(20) + op(20)
        camera = op(50, struct.pack('<2fI3f', 20, 10, 2, width, height, zoom))
        path = self.folder / f'{client}_{minor}_{zoom}_{bypass}_{int(glow)}.rep'
        path.write_bytes(pack_replay(1.8, {0: camera + text}, [(0, (0,), aux)], ['AB'], header))
        result = subprocess.run([str(ROOT / 'build/rep_gpu.exe'), '--consume', str(path),
                                 str(path.with_suffix('.json')), str(path.with_suffix('.rgba')),
                                 str(self.folder / client / 'ImagePacks2')], capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(path.with_suffix('.json').read_bytes())['exact_eof'])
        return path.with_suffix('.rgba').read_bytes()

    def test_recorded_bypass_zoom_matches_unit_zoom_for_both_font_registries(self):
        # Native font v104 sets payload+154; v208 uses zoom=1 for that byte.
        for client in ('cn', 'us'):
            for minor in (6, 7):
                with self.subTest(client=client, minor=minor):
                    expected = self.render(client, minor, 1, 0)
                    self.assertTrue(any(expected[n] for n in range(len(expected)) if n % 4 != 3))
                    self.assertEqual(self.render(client, minor, 2, 1), expected)

    def test_unflagged_text_keeps_camera_zoom(self):
        for client in ('cn', 'us'):
            with self.subTest(client=client):
                self.assertNotEqual(self.render(client, 6, 2, 0), self.render(client, 6, 1, 0))

    def test_glow_second_pass_honors_bypass_zoom(self):
        for client in ('cn', 'us'):
            with self.subTest(client=client):
                self.assertEqual(self.render(client, 6, 2, 1, glow=True),
                                 self.render(client, 6, 1, 0, glow=True))


if __name__ == '__main__':
    unittest.main()
