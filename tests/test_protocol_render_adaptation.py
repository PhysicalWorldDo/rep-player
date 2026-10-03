"""GPU geometry regressions derived from the two native REP readers."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay, npk, img_header, rec, legacy, op

ROOT = Path(__file__).resolve().parents[1]


def grid57(version, extent=(14, 14), source_scale=(1, 1), position=(1, 2),
           xs=(2, 4), ys=(2, 4)):
    # resource DWORD followed by the 32/40-byte versioned parameter body.
    params = bytearray(40 if version >= 1.8 else 32)
    struct.pack_into('<I', params, 0, 0)
    struct.pack_into('<i', params, 4, 0)
    struct.pack_into('<2f', params, 8, *position)
    struct.pack_into('<2f', params, 16, *extent)
    struct.pack_into('<I', params, 28, 0xffffffff)
    if version >= 1.8:
        struct.pack_into('<2f', params, 32, *source_scale)
    arrays = struct.pack('<QQ', len(xs), len(ys))
    arrays += struct.pack('<' + 'i' * len(xs), *xs)
    arrays += struct.pack('<' + 'i' * len(ys), *ys)
    arrays += struct.pack('<2I', 0, 0)
    return op(57, struct.pack('<I', 0) + params + arrays)


def effect(kind, values=()):
    body = b'\1' + struct.pack('<IIBII', kind, len(values), 0, 0, 0)
    return op(19, body + struct.pack('<' + 'f' * len(values), *values))


def context_mask(mode=0, enabled=True):
    # Native54 serializer: offset2, scale2, facing, mode, bind-context flag.
    return effect(54, (0, 0, 0, 0, 0, mode, float(enabled)))


class ProtocolRenderAdaptationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'protocol_102' / ('render_' + uuid.uuid4().hex[:8])
        cls.assets = cls.folder / 'assets'
        cls.assets.mkdir(parents=True)
        # Full IMG canvas 12x12, visible cropped rectangle [2,2,6,6].
        raw = bytes([0, 0, 255, 255]) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw),
                                          x=2, y=2, full_w=12, full_h=12) + raw
        (cls.assets / 'sprite_grid.NPK').write_bytes(npk(image, 'sprite/grid/frame.img'))

    def render(self, raw, version=1.8, aux=b'', profile='dfo'):
        header = b'\x0b\0' + struct.pack('<8h', *([32, 32] * 4))
        header += b'\x0c' + struct.pack('<H', 6) + bytes(126)
        path = self.folder / (self._testMethodName + '.rep')
        path.write_bytes(pack_replay(version, {0: raw}, [(0, (0,), aux)],
                                    ['sprite/grid/frame.img'], header))
        rgba, report = path.with_suffix('.rgba'), path.with_suffix('.json')
        command = [str(ROOT / 'build' / 'rep_gpu.exe'), '--consume', str(path),
                   str(report), str(rgba), str(self.assets), '--profile', profile]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.report = json.loads(report.read_text(encoding='utf-8'))
        self.assertTrue(self.report['exact_eof'])
        pixels = rgba.read_bytes()
        self.assertEqual(len(pixels), 32 * 32 * 4)
        return lambda x, y: tuple(pixels[(y * 32 + x) * 4:(y * 32 + x) * 4 + 3])

    def assert_rectangle(self, pixel, rectangle):
        self.assert_rectangles(pixel, [rectangle])

    def assert_rectangles(self, pixel, rectangles):
        for y in range(32):
            for x in range(32):
                visible = any(l <= x < r and t <= y < b for l, t, r, b in rectangles)
                expected = (255, 0, 0) if visible else (0, 0, 0)
                self.assertEqual(pixel(x, y), expected, (x, y, rectangles))

    def test_v18_grid57_uses_direct_target_extent(self):
        # Native >=1.8 forwards14 directly, stretching [2,4] from2 to4;
        # visible source [2,6] becomes [2,8], then position is added.
        pixel = self.render(grid57(1.8))
        self.assert_rectangle(pixel, (3, 4, 9, 10))

    def test_v18_grid57_source_scale_changes_origin_and_extent(self):
        pixel = self.render(grid57(1.8, source_scale=(2, 1)))
        self.assert_rectangle(pixel, (5, 4, 17, 10))

    def test_v18_grid57_signed_source_scale_keeps_native_origin(self):
        pixel = self.render(grid57(1.8, source_scale=(-1, 1), position=(10, 2)))
        self.assert_rectangle(pixel, (2, 4, 8, 10))

    def test_v18_grid57_zero_source_scale_draws_nothing(self):
        pixel = self.render(grid57(1.8, source_scale=(0, 1)))
        self.assert_rectangle(pixel, (0, 0, 0, 0))

    def test_legacy_grid57_converts_stored_float_to_extent(self):
        # full12-cropped4+cropped4*1.5 gives14, the same target as above.
        pixel = self.render(grid57(1.7, extent=(1.5, 1.5)), version=1.7)
        self.assert_rectangle(pixel, (3, 4, 9, 10))

    def test_v18_ordinary_grid_keeps_legacy_scale_conversion(self):
        grid = op(32, struct.pack('<hhB2hB2hIi', 0, 0, 2, 2, 4, 2, 2, 4, 0, 0))
        draw, aux = legacy(1, 2, scale=(1.5, 1.5))
        pixel = self.render(grid + draw, aux=aux)
        self.assert_rectangle(pixel, (3, 4, 9, 10))

    def test_cn_context38_routes_draw_to_separate_render_texture(self):
        draw, aux = legacy()
        pixel = self.render(op(38) + draw, aux=aux, profile='dnf-july')
        self.assert_rectangle(pixel, (0, 0, 0, 0))
        self.assertEqual(self.report['context_allocations'], 1)
        self.assertEqual(self.report['last_context'], 219)

    def test_cn_context39_restores_output_without_pushing_an_effect(self):
        mask, first = legacy()
        draw, second = legacy(-2, -2, scale=(2, 2))
        pixel = self.render(op(38) + mask + op(39) + draw,
                            aux=first + second, profile='dnf-july')
        self.assert_rectangle(pixel, (2, 2, 10, 10))
        self.assertEqual(self.report['context_bindings'], 1)
        self.assertEqual(self.report['last_context'], 81)

    def test_cn_type54_binds_context_texture_only_when_flag_is_set(self):
        mask, first = legacy()
        draw, second = legacy(-2, -2, scale=(2, 2))
        pixel = self.render(op(38) + mask + op(39) + context_mask() + draw,
                            aux=first + second, profile='dnf-july')
        self.assert_rectangle(pixel, (2, 2, 6, 6))
        self.assertEqual(self.report['shader_counts'][54], 1)

    def test_cn_context40_releases_actual_effect_stack(self):
        mask, first = legacy()
        draw, second = legacy(-2, -2, scale=(2, 2))
        tail, third = legacy(12, 0)
        raw = op(38) + mask + op(39) + context_mask() + draw + op(40) + tail
        pixel = self.render(raw, aux=first + second + third, profile='dnf-july')
        self.assert_rectangles(pixel, [(2, 2, 6, 6), (14, 2, 18, 6)])
        self.assertEqual(self.report['context_releases'], 1)

    def test_cn_context40_bypasses_phantom_count_unlike_opcode20(self):
        mask, first = legacy()
        draw, second = legacy(-2, -2, scale=(2, 2))
        raw = op(38) + mask + op(39) + context_mask() + effect(8) + op(40) + draw + op(20)
        pixel = self.render(raw, aux=first + second, profile='dnf-july')
        self.assert_rectangle(pixel, (2, 2, 10, 10))
        self.assertEqual(self.report['phantom_pushes'], 1)
        self.assertEqual(self.report['context_releases'], 1)

    def test_cn_context_allocator_caps_at_twelve_slots(self):
        pixel = self.render(op(38) * 14, profile='dnf-july')
        self.assert_rectangle(pixel, (0, 0, 0, 0))
        self.assertEqual(self.report['context_allocations'], 14)
        self.assertEqual(self.report['last_context'], 230)

    def test_cn_camera_prepass_uses_saved_context_and_restores_it(self):
        camera = op(50, struct.pack('<ffIfff', 0, 0, 0, 32, 32, 1))
        self.render(op(38) + camera + op(39) + camera, profile='dnf-july')
        self.assertEqual(sorted((v['context'], v['layer']) for v in self.report['camera_keys']),
                         [(81, 0), (219, 0)])

    def test_cn_inspection40_pops_actual_stack_even_with_phantom(self):
        values = [0.] * 24
        values[21] = 1
        draw, aux = legacy()
        self.render(effect(36, values) + effect(8) + op(40) + draw + op(20),
                    aux=aux, profile='dnf-july')
        self.assertFalse(any(v['role'] == 'shader-input' for v in self.report['inspection_images']))

    def test_dfo_context38to40_remain_noops(self):
        draw, aux = legacy()
        pixel = self.render(op(38) + draw + op(39) + op(40), aux=aux)
        self.assert_rectangle(pixel, (2, 2, 6, 6))
        for field in ('context_allocations', 'context_bindings', 'context_releases'):
            self.assertEqual(self.report[field], 0)


if __name__ == '__main__':
    unittest.main()
