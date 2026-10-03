"""GPU state equivalence for the explicitly bounded opcode66 ignore policy.

This verifies compatibility behavior, not the unidentified native66 semantics.
Fixtures, copied GPU tool, shaders, cache and reports stay in a fresh validation
directory. Original clients and helper modules are read-only inputs.
"""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay, npk, img_header, rec, legacy, op
from test_protocol_render_adaptation import effect, context_mask

ROOT = Path(__file__).resolve().parents[1]
IGNORE = op(66, b'\0')


def camera(layer=2, x=0, y=0):
    return op(50, struct.pack('<2fI3f', x, y, layer, 32, 32, 1)), b''


def draw43(x, y, layer=2):
    params = bytearray(60)
    struct.pack_into('<I', params, 4, layer)
    struct.pack_into('<2f', params, 16, 1, 1)
    struct.pack_into('<I', params, 28, 0xffffffff)
    struct.pack_into('<2f', params, 48, 1, 1)
    return op(43, struct.pack('<I', 0) + params), struct.pack('<2h', x, y)


def state(raw):
    return raw, b''


class Opcode66RenderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'opcode66_20261004' / ('gpu_' + uuid.uuid4().hex[:8])
        cls.assets = cls.folder / 'fixture_client' / 'ImagePacks2'
        cls.assets.mkdir(parents=True)
        executable = Path(os.environ.get('REP_OPCODE66_GPU', ROOT / 'build' / 'rep_gpu.exe'))
        cls.exe = cls.folder / 'build' / 'rep_gpu.exe'
        cls.exe.parent.mkdir()
        shutil.copy2(executable, cls.exe)
        shaders = cls.folder / 'assets' / 'shaders'
        shaders.mkdir(parents=True)
        for source in (ROOT / 'assets' / 'shaders').glob('*.dxbc'):
            shutil.copy2(source, shaders / source.name)
        rgba = bytes((255, 255, 255, 255)) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(rgba),
                                         x=0, y=0, full_w=4, full_h=4) + rgba
        (cls.assets / 'sprite_test.NPK').write_bytes(npk(image))
        (cls.folder / 'input_executable.json').write_text(json.dumps({
            'source': str(executable), 'copied_executable': str(cls.exe),
            'size': executable.stat().st_size,
        }, indent=2), encoding='utf-8')

    def pair(self, scenes):
        commands, timeline, compatible_timeline = {}, [], []
        next_id, ignored_references = 1, 0
        for ordinal, fragments in enumerate(scenes):
            ids, aux = [], bytearray()
            for raw, auxiliary in fragments:
                commands[next_id] = raw
                ids.append(next_id)
                aux.extend(auxiliary)
                next_id += 1
            timestamp = [0, 10, 10, 40][min(ordinal, 3)]
            timeline.append((timestamp, tuple(ids), bytes(aux)))
            compatible_ids = [0]
            for command_id in ids:
                compatible_ids.extend((command_id, 0))
            compatible_ids.append(0)
            ignored_references += len(ids) + 2
            compatible_timeline.append((timestamp, tuple(compatible_ids), bytes(aux)))
        header = b'\x0b\0' + struct.pack('<8h', *([32, 32] * 4))
        header += b'\x0c' + struct.pack('<H', 6) + bytes(126)
        paths = []
        for name, dictionary, frames in (
                ('strict_reference', commands, timeline),
                ('compatible_ignored66', {0: IGNORE, **commands}, compatible_timeline)):
            dictionary = dict(sorted(dictionary.items(), key=lambda item: len(item[1])))
            path = self.folder / (self._testMethodName + '_' + name + '.rep')
            path.write_bytes(pack_replay(1.8, dictionary, frames,
                                        ['sprite/test/frame.img'], header))
            paths.append(path)
        return paths, ignored_references

    def launch(self, path, profile, mode='consume', actions=None):
        report_path, rgba_path = path.with_suffix('.json'), path.with_suffix('.rgba')
        if mode == 'consume':
            command = [str(self.exe), '--consume', str(path), str(report_path),
                       str(rgba_path), str(self.assets), '--profile', profile]
        else:
            command = [str(self.exe), '--frame-step', str(path), str(self.assets),
                       actions, '--profile', profile]
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding='utf-8', timeout=30)
        path.with_suffix('.run.json').write_text(json.dumps({
            'command': command, 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr,
        }, ensure_ascii=False, indent=2), encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stderr)
        if mode == 'consume':
            report = json.loads(report_path.read_text(encoding='utf-8'))
            self.assertTrue(report['exact_eof'])
            pixels = rgba_path.read_bytes()
            self.assertEqual(len(pixels), 32 * 32 * 4)
            return report, pixels
        report = json.loads(result.stdout)
        report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
        return report

    def assert_equivalent(self, scenes):
        paths, ignored = self.pair(scenes)
        reference, expected = self.launch(paths[0], 'dnf-july')
        # Prove the reference is a valid GPU fixture before testing compatibility.
        self.assertGreater(reference['gpu_draws'], 0)
        self.assertTrue(any(expected[index] for index in range(len(expected)) if index % 4 != 3))
        candidate, actual = self.launch(paths[1], 'dnf-compatible')
        self.assertEqual(actual, expected)
        self.assertEqual(candidate['compatibility_ignored_instructions'], ignored)
        self.assertEqual(reference['compatibility_ignored_instructions'], 0)
        self.assertEqual(candidate['opcodes'][66], ignored)
        self.assertEqual(candidate['opcodes'][:66], reference['opcodes'][:66])
        for field in ('scenes', 'gpu_draws', 'width', 'height', 'last_timestamp',
                      'aux_bytes', 'null_resources', 'null_captures', 'fallbacks',
                      'actor_pool_updates', 'camera_updates', 'audio_events',
                      'phantom_pushes', 'grid_cells', 'stencil_draws', 'sampler_draws',
                      'null_caches', 'context_allocations', 'context_bindings',
                      'context_releases', 'last_context', 'camera_keys',
                      'inspection_images', 'shader_counts'):
            self.assertEqual(candidate[field], reference[field], field)
        return candidate

    def test_ignored66_preserves_shader_phantom_and_blend_stacks(self):
        report = self.assert_equivalent([[
            state(effect(26, (.5,))), state(effect(8)),
            state(op(0, struct.pack('<2I', 5, 0x0000ff00))),
            legacy(2, 2, color=0xffff8040), state(op(1) + op(20)),
            legacy(8, 2), state(op(20)), legacy(14, 2),
        ]])
        self.assertEqual(report['phantom_pushes'], 1)
        self.assertEqual(report['shader_counts'][26], 1)

    def test_ignored66_preserves_nested_blend_flags(self):
        self.assert_equivalent([[
            state(op(51, struct.pack('<H', 32768))), legacy(2, 2, color=0xff406080),
            state(op(52, b'\0\0')), state(op(51, struct.pack('<H', 2))),
            legacy(2, 2, color=0x80c86432), state(op(51, struct.pack('<H', 16))),
            legacy(8, 2), state(op(52, b'\0\0')), legacy(2, 2, color=0x80c86432),
            state(op(52, b'\0\0')), legacy(14, 2),
        ]])

    def test_ignored66_preserves_sampler_enable_nested_restore_and_draws(self):
        outer = op(61, struct.pack('<BBHI6f', 1, 1, 0, 0, 2, 1, 3, 0, 0, 0))
        inner = op(61, struct.pack('<BBHI6f', 1, 1, 0, 0, 1, 2, 0, 3, 0, 0))
        report = self.assert_equivalent([[
            state(op(16, b'\x0a')), state(op(63, b'\1')), state(outer), legacy(1, 1),
            state(inner), legacy(8, 1), state(op(62)), legacy(1, 8),
            state(op(63, b'\0')), legacy(16, 16), state(op(62)),
        ]])
        self.assertEqual(report['sampler_draws'], 3)
        pixels = (self.folder / (self._testMethodName + '_compatible_ignored66.rgba')).read_bytes()
        # GPU draw counts reflect batching. Verify the four visible primitives,
        # including the restored outer transform and the disabled sampler.
        rectangles = ((5, 1, 13, 5), (8, 5, 12, 13),
                      (5, 8, 13, 12), (16, 16, 20, 20))
        for y in range(32):
            for x in range(32):
                visible = any(left <= x < right and top <= y < bottom
                              for left, top, right, bottom in rectangles)
                offset = (y * 32 + x) * 4
                rgb = pixels[offset:offset + 3]
                self.assertEqual(any(rgb), visible, (x, y))
                self.assertEqual(rgb[0], rgb[1], (x, y))
                self.assertEqual(rgb[1], rgb[2], (x, y))

    def test_ignored66_preserves_camera_updates_across_recorded_scenes(self):
        report = self.assert_equivalent([
            [camera(2, 4, 5), draw43(6, 7)],
            [draw43(9, 8)],
            [camera(2, 1, 2), draw43(5, 6)],
        ])
        self.assertEqual(report['camera_updates'], 2)

    def context_scenes(self):
        return [
            [state(op(38)), legacy(2, 4), state(op(39))],
            [state(context_mask()), legacy(0, 0, scale=(3, 3)), state(op(40)),
             state(op(38)), legacy(16, 4), state(op(39))],
            [state(context_mask()), legacy(8, 0, scale=(3, 3)), state(op(40)), legacy(24, 20)],
        ]

    def test_ignored66_preserves_context_textures_and_type54_across_scenes(self):
        report = self.assert_equivalent(self.context_scenes())
        self.assertEqual(report['context_allocations'], 2)
        self.assertEqual(report['context_bindings'], 2)

    def test_ignored66_keeps_backward_steps_and_hidden_refresh_equivalent(self):
        paths, _ = self.pair(self.context_scenes())
        actions = 'next,next,hide,hide,prev,next,select=40,prev,next'
        reference = self.launch(paths[0], 'dnf-july', mode='step', actions=actions)
        for snapshot in reference['snapshots']:
            self.assertEqual(snapshot['crc'], snapshot['reference_crc'])
        candidate = self.launch(paths[1], 'dnf-compatible', mode='step', actions=actions)
        self.assertEqual(candidate['frame_count'], reference['frame_count'])
        self.assertEqual(len(candidate['snapshots']), len(reference['snapshots']))
        for actual, expected in zip(candidate['snapshots'], reference['snapshots']):
            self.assertEqual(actual['crc'], actual['reference_crc'])
            for field in ('ordinal', 'timestamp', 'paused', 'ended', 'changed',
                          'crc', 'reference_crc', 'hidden_count', 'images'):
                self.assertEqual(actual[field], expected[field], field)


if __name__ == '__main__':
    unittest.main()
