"""Read-only Python oracle; every generated file stays in rep_player."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
import rep_protocol as oracle
from test_rep_protocol_versions import pack_replay


class NativeProtocolTests(unittest.TestCase):
    def native(self, data):
        exe = ROOT / 'build' / 'rep_validate.exe'
        self.assertTrue(exe.is_file(), 'native REP protocol executable has not been implemented')
        with tempfile.TemporaryDirectory(dir=ROOT / 'validation') as folder:
            path = Path(folder) / 'fixture.rep'
            path.write_bytes(data)
            result = subprocess.run([str(exe), '--dump', str(path)], capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)

    def test_versions_dictionary_xor_sparse_ids_aux_and_exact_eof(self):
        for version in (1., 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7):
            draw = struct.pack('<II', 3, 12) + struct.pack('<IhBBI', 1, -2, 1, 0, 0x80FFAABB)
            data = pack_replay(version, {2: draw, 9: draw, 11: b''},
                               [(7, (2, 999, 9, 11), struct.pack('<4h', -3, 4, 5, -6)), (7, (), b''), (-1, (), b'')],
                               ['first.img', 'second.img'])
            out = self.native(data)
            self.assertEqual(out['version'], version)
            self.assertEqual((out['scenes'], out['references'], out['aux_bytes']), (3, 4, 8))
            self.assertEqual(out['timestamps'], [7, 7, -1])
            self.assertEqual(out['resources'], {'0': 'first.img', '1': 'second.img'})
            entries = {row['id']: row for row in out['dictionary']}
            self.assertEqual(entries[2]['raw_crc32'], zlib.crc32(draw))
            self.assertEqual(entries[9]['raw_crc32'], zlib.crc32(draw))
            self.assertEqual(entries[11]['instructions'], [])
            self.assertEqual(entries[2]['instructions'][0]['params_hex'], oracle._default_draw_params(draw[8:]).hex())
            self.assertTrue(out['exact_eof'])

    def test_tagged_header_and_absent_resource_slot(self):
        extension = struct.pack('<HBBII', 7, 2, 1, 20230316, 21600) + bytes(116)
        header = b'\2' + struct.pack('<8H', 2023, 10, 2, 17, 11, 43, 21, 876)
        header += b'\x0b\0' + struct.pack('<8h', 800, 600, 800, 600, 1200, 675, 800, 600)
        header += b'\x0c' + extension + b'\x0d\2'
        out = self.native(pack_replay(1.7, {}, resources=[None, 'a.img'], header=header))
        self.assertEqual((out['minor'], out['width'], out['height'], out['render_mode']), (7, 1200, 675, 2))
        self.assertEqual(out['resources'], {'0': '', '1': 'a.img'})

    def test_all_legal_opcodes_have_exact_native_boundaries(self):
        defaults = oracle._default_draw_params(b'')
        for version, minor in ((1.0, 0), (1.3, 0), (1.4, 0), (1.4, 3), (1.5, 5), (1.7, 6), (1.7, 7)):
            extension = b'\x0c' + struct.pack('<H', minor) + bytes(126) if version >= 1.4 else b''
            payloads = {
                0: struct.pack('<4I', 7, 0, 1, 2), 1: b'', 2: struct.pack('<4h', 1, 2, 3, 4),
                3: struct.pack('<I', 64) + defaults, 4: struct.pack('<I', 0),
                5: bytes(16) if version < 1.5 or minor < 5 else struct.pack('<I', 0),
                6: struct.pack('<I', 0), 7: bytes(20), 8: b'', 9: b'',
                10: struct.pack('<I', 2) + defaults + struct.pack('<IhhhIhhh', 6, 2, 3, 4, 6, -2, -3, -4),
                11: b'', 12: b'', 13: bytes(64), 14: bytes(36), 15: b'\1' + bytes(36),
                16: b'\2', 17: b'', 18: bytes(6), 20: b'',
                21: bytes(76 if version < 1.6 and minor != 6 else 80) + defaults + bytes(4),
                22: b'', 23: bytes(56), 24: bytes(16), 25: b'', 26: b'\0', 27: b'\0',
                28: bytes(3), 29: b'\0', 30: struct.pack('<I', 0), 31: b'',
                32: struct.pack('<hhB2hB3hIi', 1, 2, 2, 3, 4, 3, 5, 6, 7, 0, 0), 33: bytes(8),
                41: b'', 48: bytes(52) + (bytes(8) if minor < 7 else b'') + b'\1',
                50: bytes(24), 51: bytes(2), 52: bytes(2), 53: bytes(6), 55: bytes(8), 56: bytes(8),
                57: bytes(36) + struct.pack('<QQ', 2, 3) + bytes(20) + bytes(8),
                58: bytes(40), 59: b'', 60: bytes(16), 61: bytes(32), 62: b'', 63: b'\1', 64: bytes(12)
            }
            if version < 1.3:
                payloads[19] = b'\1' + bytes(120)
            elif version == 1.3:
                payloads[19] = b'\1' + struct.pack('<7I', 25, 2, 0, 1, 4, 5, 1) + struct.pack('<3f', .1, .2, .3)
            elif minor:
                payloads[19] = b'\1' + struct.pack('<IIBII3f2I', 25, 3, 1, 0, 2, .1, .2, .3, 1, 2)
            else:
                payloads[19] = b'\1' + struct.pack('<4I2f', 25, 2, 1, 0, .1, .2)
            for op in range(42, 48):
                mode = (op - 42) % 3
                payloads[op] = (bytes(4 + (48, 60, 76)[mode]) if minor < 7
                                else struct.pack('<II', (40, 52, 64)[mode], 0) + bytes((40, 52, 64)[mode]))
            for op in (34, 35, 36, 37, 38, 39, 40, 49, 54):
                payloads[op] = b''
            commands = {op: struct.pack('<I', op) + payloads[op] for op in range(65)}
            # Native XOR encoding uses the immediate predecessor. Increasing sizes
            # ensure the oracle packer never truncates zip against a shorter entry.
            commands = dict(sorted(commands.items(), key=lambda row: len(row[1])))
            out = self.native(pack_replay(version, commands, header=extension))
            decoded = {row['id']: row for row in out['dictionary']}
            for op, raw in commands.items():
                expected = oracle._decode_commands(raw, op, version, minor)
                item = decoded[op]['instructions'][0]
                self.assertEqual((item['opcode'], item['payload_bytes']), (op, len(raw) - 4))
                self.assertEqual(item['aux_bytes'], oracle._command_aux_size(expected))
                params = expected[0][1].get('params') if isinstance(expected[0][1], dict) else None
                if params is not None:
                    self.assertEqual(item['params_hex'], params.hex(), (version, minor, op))

    def test_rejects_crc_length_unknown_opcode_and_aux_mismatch(self):
        exe = ROOT / 'build' / 'rep_validate.exe'
        self.assertTrue(exe.is_file(), 'native REP protocol executable has not been implemented')
        valid = pack_replay(1.7, {0: struct.pack('<I', 17)})
        cases = [valid[:8] + valid[8:-1] + bytes([valid[-1] ^ 1]), valid[:-1],
                 pack_replay(1.7, {0: struct.pack('<I', 65)}),
                 pack_replay(1.7, {0: struct.pack('<II', 3, 0)}, [(0, (0,), b'')])]
        with tempfile.TemporaryDirectory(dir=ROOT / 'validation') as folder:
            for i, data in enumerate(cases):
                path = Path(folder) / f'bad{i}.rep'
                path.write_bytes(data)
                out = subprocess.run([str(exe), str(path)], capture_output=True)
                self.assertNotEqual(out.returncode, 0, i)


if __name__ == '__main__':
    (ROOT / 'validation').mkdir(exist_ok=True)
    unittest.main()
