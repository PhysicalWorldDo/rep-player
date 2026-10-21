"""2x UI fallback keeps REP placement and uses half-size IMG geometry."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
from test_scene_branches import pack_replay, img_header, rec, legacy, op
from test_npk_priority import write_package
from test_default_layer_camera import draw43

ROOT = Path(__file__).resolve().parents[1]
LOGICAL = 'sprite/interface/draghandle.img'
HD = 'sprite/interface/(2x)draghandle.img'


class Img2xGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/img_missing_20261021' / ('geometry_' + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        print('GEOMETRY_VALIDATION=' + str(cls.folder), flush=True)

    def render(self, raw, aux, *, hd=True, offset=(2, 3), size=(4, 4), full=(12, 12), pattern=False):
        folder = self.folder / (self._testMethodName + ('_2x' if hd else '_1x'))
        folder.mkdir()
        density = 2 if hd else 1
        w, h = size
        colors = [(0, 0, 255, 255), (0, 255, 0, 255), (255, 0, 0, 255), (0, 255, 255, 255)]
        pixels = b''.join(bytes(colors[min(x // density, 3)] if pattern else colors[0])
                          for y in range(h * density) for x in range(w * density))
        image = img_header(2, 36, 1) + rec(16, 5, w * density, h * density, len(pixels),
                    x=offset[0] * density, y=offset[1] * density,
                    full_w=full[0] * density, full_h=full[1] * density) + pixels
        write_package(folder / 'sprite_interface.NPK', {HD if hd else LOGICAL: image})
        header = b'\x0b\0' + struct.pack('<8h', *([32, 32] * 4)) + b'\x0c' + struct.pack('<H', 6) + bytes(126)
        camera = op(50, struct.pack('<2fI3f', 0, 0, 0, 32, 32, 1))
        replay = folder / 'fixture.rep'
        replay.write_bytes(pack_replay(1.8, {0: camera + raw}, [(0, (0,), aux)], [LOGICAL], header))
        result = subprocess.run([str(ROOT / 'build/rep_gpu.exe'), '--consume', str(replay),
                    str(folder / 'report.json'), str(folder / 'frame.rgba'), str(folder)],
                    capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((folder / 'report.json').read_bytes())
        self.assertTrue(report['exact_eof'])
        return (folder / 'frame.rgba').read_bytes()

    def test_drag_handle_size_offset_are_halved_but_rep_anchor_is_unchanged(self):
        raw, aux = draw43(7, 9, 0)
        pixels = self.render(raw, aux)
        occupied = {(x, y) for y in range(32) for x in range(32) if pixels[(y * 32 + x) * 4]}
        self.assertEqual(occupied, {(x, y) for y in range(12, 16) for x in range(9, 13)})

    def test_recorded_drag_movement_keeps_its_full_distance(self):
        first, a = draw43(3, 4, 0)
        second, b = draw43(13, 4, 0)
        pixels = self.render(first + second, a + b)
        occupied = {(x, y) for y in range(32) for x in range(32) if pixels[(y * 32 + x) * 4]}
        self.assertEqual(occupied, {(x, y) for y in range(7, 11) for x in [*range(5, 9), *range(15, 19)]})

    def test_half_geometry_preserves_hd_texture_sampling(self):
        raw, aux = draw43(7, 9, 0)
        self.assertEqual(self.render(raw, aux, hd=True, pattern=True),
                         self.render(raw, aux, hd=False, pattern=True))

    def test_legacy_source_crop_and_recorded_pivot_keep_logical_units(self):
        raw, aux = legacy(7, 9, source=(0, 0, 2, 3), pivot=(3, 4), scale=(2, 1), opcode=4)
        self.assertEqual(self.render(raw, aux, hd=True), self.render(raw, aux, hd=False))

    def test_mirrored_handle_preserves_recorded_pivot(self):
        raw, aux = legacy(7, 9, special=3, pivot=(4, 5), opcode=4)
        self.assertEqual(self.render(raw, aux, hd=True), self.render(raw, aux, hd=False))

    def test_stretched_grid_uses_logical_cut_coordinates(self):
        raw, aux = legacy(7, 9, scale=(2, 2), opcode=4)
        grid = op(32, struct.pack('<hhB2hB2hIi', 0, 0, 2, 2, 4, 2, 3, 5, 0, 0))
        self.assertEqual(self.render(grid + raw, aux, hd=True), self.render(grid + raw, aux, hd=False))

    def test_quad2_source_and_destination_rectangles_stay_in_rep_units(self):
        data = bytearray(76)
        data[16] = data[36] = 1
        struct.pack_into('<4f', data, 20, 0, 0, 2, 3)
        struct.pack_into('<4f', data, 40, 0, 0, 12, 12)
        struct.pack_into('<I2f', data, 60, 0xffffffff, 1, 1)
        raw, aux = op(44, struct.pack('<I', 0) + data), struct.pack('<2h', 7, 9)
        self.assertEqual(self.render(raw, aux, hd=True), self.render(raw, aux, hd=False))


if __name__ == '__main__':
    unittest.main()
