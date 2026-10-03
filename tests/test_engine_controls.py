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
from rep_protocol import _default_draw_params

ROOT = Path(__file__).resolve().parents[1]


class NativeEngineControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'engine_controls'
        cls.folder.mkdir(exist_ok=True)
        cls.assets = cls.folder / 'client' / 'ImagePacks2'
        cls.assets.mkdir(parents=True, exist_ok=True)
        raw = bytes((0, 0, 255, 128)) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (cls.assets / 'sprite_test.NPK').write_bytes(npk(image))
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        command = struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        cls.replay = cls.folder / 'controls.rep'
        cls.replay.write_bytes(pack_replay(1.7, {0: command},
            [(t, (0,), struct.pack('<2h', 2, 2)) for t in (0, 100, 300)],
            ['sprite/test/frame.img', 'sprite/unused/frame.img'], header=header))

    def contract(self, replay=None, hidden=None, time=None):
        args = [str(ROOT / 'build' / 'rep_gpu.exe'), '--engine-contract',
            str(replay or self.replay), str(self.assets), str(self.folder)]
        if hidden or time is not None:
            args.append(hidden or 'sprite/test/frame.img')
        if time is not None:
            args.append(str(time))
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.strip(), 'engine controls, IMG calls and transparent render contract is absent')
        return json.loads(result.stdout)

    def test_hidden_path_keeps_calls_and_restores_identical_pixels(self):
        data = self.contract()
        current = data['current_images']
        self.assertEqual([(x['path'], x['frame'], x['count']) for x in current],
                         [('sprite/test/frame.img', 0, 1)])
        self.assertTrue(current[0]['hidden'])
        self.assertFalse(current[0]['drawn'])
        self.assertNotEqual(data['visible_crc'], data['hidden_crc'])
        self.assertEqual(data['visible_crc'], data['restored_crc'])
        self.assertTrue(data['exact_eof'])
        all_images = {x['path']: x for x in data['all_images']}
        self.assertGreater(all_images['sprite/test/frame.img']['count'], 0)
        self.assertTrue(all_images['sprite/test/frame.img']['drawn'])
        self.assertEqual(all_images['sprite/unused/frame.img']['count'], 0)
        self.assertTrue(all_images['sprite/unused/frame.img']['registered'])

    def test_transparency_preserves_real_alpha_and_straight_color(self):
        self.contract()
        visible = (self.folder / 'visible.rgba').read_bytes()
        hidden = (self.folder / 'hidden.rgba').read_bytes()
        self.assertEqual(visible[:4], bytes(4))
        self.assertEqual(tuple(visible[(2 * 16 + 2) * 4:(2 * 16 + 2) * 4 + 4]), (255, 0, 0, 128))
        self.assertEqual(hidden, bytes(16 * 16 * 4))

    def test_pause_refresh_resume_and_backward_seek(self):
        data = self.contract()
        self.assertEqual(data['paused_before'], data['paused_after'])
        self.assertGreaterEqual(data['resumed_elapsed'], data['paused_after'] + 20)
        self.assertEqual(data['refresh_timestamp_before'], data['refresh_timestamp_after'])
        self.assertEqual(data['backward_timestamp'], 0)
        self.assertEqual(data['duration'], 300)
        self.assertEqual(data['inspection_scenes'], 3)

    def test_whole_replay_scan_includes_scheduled_shader_dependencies(self):
        raw = bytes((255, 255, 255, 255)) * 16
        records = b''.join(rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) for _ in range(5))
        image = img_header(2, len(records), 5) + records + raw * 5
        (self.assets / 'sprite_shader.NPK').write_bytes(npk(image, 'sprite/shader/mask.img'))
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        values = [0.] * 25
        values[0] = values[1] = values[18] = 1.
        values[10] = -1.
        effect = struct.pack('<I', 19) + b'\1' + struct.pack('<IIBII', 17, 25, 0, 0, 0) + struct.pack('<25f', *values)
        command = effect + struct.pack('<II', 3, 64) + params + struct.pack('<I', 20)
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        replay = self.folder / 'shader_dependencies.rep'
        replay.write_bytes(pack_replay(1.7, {0: command}, [(0, (0,), struct.pack('<2h', 2, 2))],
                                      ['sprite/test/frame.img'], header=header))
        data = self.contract(replay)
        inspection = {x['path']: x for x in data.get('inspection_images', [])}
        self.assertIn('sprite/shader/mask.img', inspection, 'CPU dispatch omitted a builtin shader texture')
        self.assertEqual(inspection['sprite/shader/mask.img']['role'], 'shader-input')
        self.assertEqual(inspection['sprite/shader/mask.img']['count'], 1)
        self.assertFalse(inspection['sprite/shader/mask.img']['drawn'])
        current = {x['path']: x for x in data['current_images']}
        self.assertEqual(current['sprite/shader/mask.img']['role'], 'shader-input')

    def test_hiding_inverse_stencil_input_suppresses_dependent_composite(self):
        raw = bytes((255, 255, 255, 255)) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (self.assets / 'sprite_mask.NPK').write_bytes(npk(image, 'sprite/mask/frame.img'))
        mask = bytearray(_default_draw_params(b''))
        struct.pack_into('<I', mask, 0, 1)
        struct.pack_into('<2f', mask, 28, 0, 0)
        source = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', source, 20, 3, 3)
        struct.pack_into('<2f', source, 28, 0, 0)
        opcode = lambda n, p=b'': struct.pack('<I', n) + p
        command = opcode(26, b'\0') + opcode(28, b'\0\0\1') + opcode(3, struct.pack('<I', 64) + mask)
        command += opcode(27, b'\0') + opcode(28, b'\0\1\1') + opcode(3, struct.pack('<I', 64) + source) + opcode(29, b'\0')
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        replay = self.folder / 'inverse_stencil.rep'
        replay.write_bytes(pack_replay(1.7, {0: command}, [(0, (0,), struct.pack('<4h', 2, 2, 0, 0))],
                                      ['sprite/test/frame.img', 'sprite/mask/frame.img'], header=header))
        data = self.contract(replay, 'sprite/mask/frame.img')
        self.assertEqual((self.folder / 'hidden.rgba').read_bytes(), bytes(16 * 16 * 4),
                         'an inverse stencil expanded after its hidden dependency was removed')
        self.assertEqual(data['visible_crc'], data['restored_crc'])
        calls = {x['path']: x for x in data['current_images']}
        self.assertEqual(calls['sprite/mask/frame.img']['role'], 'stencil')
        self.assertFalse(calls['sprite/mask/frame.img']['drawn'])

    def test_straight_alpha_keeps_additive_color_energy(self):
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        command = struct.pack('<IH', 51, 256) + struct.pack('<II', 3, 64) + params + struct.pack('<IH', 52, 0)
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        replay = self.folder / 'additive_alpha.rep'
        replay.write_bytes(pack_replay(1.7, {0: command}, [(0, (0,), struct.pack('<2h', 2, 2))],
                                      ['sprite/test/frame.img'], header=header))
        self.contract(replay)
        rgba = (self.folder / 'visible.rgba').read_bytes()
        r, g, b, a = rgba[(2 * 16 + 2) * 4:(2 * 16 + 2) * 4 + 4]
        self.assertGreaterEqual(round(r * a / 255), 254, 'RGBA export clipped additive color when composited over black')
        self.assertEqual(rgba[:4], bytes(4))

    def test_paused_refresh_keeps_previous_scene_blend_state(self):
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        opcode = lambda n, p=b'': struct.pack('<I', n) + p
        first = opcode(51, struct.pack('<H', 256)) + opcode(3, struct.pack('<I', 64) + params) + opcode(52, bytes(2))
        second = opcode(51, struct.pack('<H', 16)) + opcode(3, struct.pack('<I', 64) + params)
        second += opcode(51, bytes(2)) + opcode(3, struct.pack('<I', 64) + params) + opcode(52, bytes(2)) + opcode(52, bytes(2))
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        replay = self.folder / 'refresh_blend.rep'
        replay.write_bytes(pack_replay(1.7, {0: first, 1: second},
                                      [(0, (0,), struct.pack('<2h', 2, 2)), (100, (1,), struct.pack('<4h', 2, 2, 8, 2))],
                                      ['sprite/test/frame.img'], header=header))
        data = self.contract(replay, time=100)
        self.assertEqual(data['visible_crc'], data['restored_crc'], 'refresh reused the end-of-scene blend instead of its entry blend')

    def test_fallback_reports_actual_placeholder_and_hides_either_path(self):
        raw = bytes((255, 255, 255, 128)) * 16
        records = b''.join(rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) for _ in range(92))
        image = img_header(2, len(records), 92) + records + raw * 92
        (self.assets / 'sprite_interface.NPK').write_bytes(npk(image, 'interface/base.img'))
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        command = struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        replay = self.folder / 'fallback.rep'
        replay.write_bytes(pack_replay(1.7, {0: command}, [(0, (0,), struct.pack('<2h', 2, 2))],
                                      ['sprite/missing/frame.img'], header=header))
        for path in ('sprite/missing/frame.img', 'sprite/interface/base.img'):
            with self.subTest(hidden=path):
                data = self.contract(replay, path)
                calls = {x['path']: x for x in data['current_images']}
                self.assertIn('sprite/interface/base.img', calls, 'fallback was shown as if the missing IMG was drawn')
                self.assertEqual(calls['sprite/interface/base.img']['frame'], 91)
                self.assertFalse(calls['sprite/missing/frame.img']['drawn'])
                self.assertEqual((self.folder / 'hidden.rgba').read_bytes(), bytes(16 * 16 * 4))
                self.assertEqual(data['visible_crc'], data['restored_crc'])
                all_images = {x['path']: x for x in data['all_images']}
                self.assertTrue(all_images['sprite/interface/base.img']['drawn'])

    def test_empty_clip_keeps_call_without_marking_gpu_draw(self):
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        command = struct.pack('<I4h', 2, 0, 0, 0, 0) + struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16] * 8)) + b'\x0c' + struct.pack('<H', 7) + bytes(126)
        replay = self.folder / 'empty_clip.rep'
        replay.write_bytes(pack_replay(1.7, {0: command}, [(0, (0,), struct.pack('<2h', 2, 2))],
                                      ['sprite/test/frame.img'], header=header))
        data = self.contract(replay)
        self.assertEqual(data['current_images'][0]['count'], 1)
        self.assertFalse(data['all_images'][0]['drawn'])
        self.assertEqual((self.folder / 'visible.rgba').read_bytes(), bytes(16 * 16 * 4))


if __name__ == '__main__':
    unittest.main()
