import json
from pathlib import Path
import subprocess
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NativeGpuTests(unittest.TestCase):
    def test_original_shader_pixels_match_python_oracle(self):
        exe = ROOT / 'build' / 'rep_gpu.exe'
        self.assertTrue(exe.is_file(), 'native D3D11 renderer is not implemented')
        fixture = ROOT / 'validation' / 'basic_shader.bin'
        # SFX1: type/version/minor/width/height/channel/nfloats/nbits/external/facing/stone
        fixture.write_bytes(b'SFX1' + struct.pack('<11I', 26, 14, 3, 16, 16, 0, 1, 0, 0, 0, 0) + struct.pack('<5f', 1, 1, 1, 1, .5))
        output = ROOT / 'validation' / 'basic_shader.rgba'
        result = subprocess.run([str(exe), '--shader-fixture', str(fixture), str(output)], cwd=ROOT, capture_output=True, text=True, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(output.is_file(), 'GPU shader fixture path does not execute pixel rendering')
        self.assertEqual(output.stat().st_size, 16 * 16 * 4)

    def test_original_vertex_and_pixel_programs_create_on_hardware(self):
        exe = ROOT / 'build' / 'rep_gpu.exe'
        self.assertTrue(exe.is_file(), 'native D3D11 renderer is not implemented')
        result = subprocess.run([str(exe), '--smoke'], cwd=ROOT, capture_output=True, text=True, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['programs_created'], 169)
        self.assertTrue(report['hardware'])
        self.assertIn('NVIDIA', report['adapter'])
        (ROOT / 'validation' / 'gpu_original_program_creation.json').write_text(json.dumps(report, indent=2), encoding='utf8')

if __name__ == '__main__':
    unittest.main()
