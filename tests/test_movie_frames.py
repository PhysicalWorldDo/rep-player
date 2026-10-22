"""Native Movie23 frame units and real Bink Alpha decoding."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(os.environ.get('REP_CN_CLIENT', r'E:\WeGameApps\地下城与勇士：创新世纪'))
MOVIE = 'Video/Character/Cutscene_buf/01_ghost_M_buf_bsk.bk2'
# Same-client Bink DLL, opened with the native CN 14729D1A0 flags 0x105000.
# Frame numbers are Bink's one-based numbers. RGB CRCs isolate frame selection
# from Alpha decoding, so the two regressions fail independently.
BINK_FRAMES = {3: (205920021, 1691112752),
               32: (2458059727, 2294018564),
               38: (3108753067, 539666543)}


class MovieFrameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'bk2_native_fix_20261022' / ('cpu_' + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        cls.probe = cls.folder / 'movie_frame_probe.exe'
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        argv = [compiler, '-std=c++20', '-O2', '-DNOMINMAX', '-municode', '-static',
                '-I', ROOT / 'src', ROOT / 'tests/movie_frame_probe.cpp',
                ROOT / 'src/movies.cpp', ROOT / 'build/protocol.o',
                ROOT / 'vendor/zlib/libz.a', '-o', cls.probe]
        result = subprocess.run(list(map(str, argv)), capture_output=True, timeout=60)
        (cls.folder / 'build.log').write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError(result.stderr.decode(errors='replace'))
        shutil.copy2(ROOT / 'build/ffmpeg.exe', cls.folder / 'ffmpeg.exe')

    def frames(self, client, logical, numbers):
        cache = self.folder / (self._testMethodName + '_cache')
        result = subprocess.run([str(self.probe), str(client), logical, str(cache),
                                 *map(str, numbers)], capture_output=True, timeout=45)
        (self.folder / (self._testMethodName + '.log')).write_bytes(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        return json.loads(result.stdout)

    def test_bink_decodes_transparent_and_partial_alpha(self):
        # Input 0 aliases the first Bink frame in both versions. This isolates
        # the open flags from the separate recorded-frame-number regression.
        got = self.frames(CLIENT, MOVIE, [0])[0]
        self.assertGreater(got['transparent'], 0)
        self.assertGreater(got['partial'], 0)
        self.assertLess(got['opaque'], 970 * 668)

    def test_bink_uses_recorded_frames_and_rewinds_without_time_conversion(self):
        numbers = [3, 32, 38, 3]
        got = self.frames(CLIENT, MOVIE, numbers)
        self.assertEqual([f['rgb_crc32'] for f in got], [BINK_FRAMES[n][1] for n in numbers])
        self.assertEqual([f['rgba_crc32'] for f in got], [BINK_FRAMES[n][0] for n in numbers])

    def test_bink_first_frame_alias_and_last_frame_clamping(self):
        got = self.frames(CLIENT, MOVIE, [0, 1, 61, 62, 4294967295])
        self.assertEqual(got[0], got[1])
        self.assertEqual(got[2], got[3])
        self.assertEqual(got[2], got[4])
        self.assertNotEqual(got[0]['rgba_crc32'], got[2]['rgba_crc32'])

    def test_avi_time_based_decode_path_is_preserved(self):
        client = self.folder / 'avi_client'
        client.mkdir()
        ffmpeg = self.folder / 'ffmpeg.exe'
        source = client / 'frames.avi'
        subprocess.run([str(ffmpeg), '-v', 'error', '-f', 'lavfi', '-i',
                        'testsrc2=size=32x24:rate=10:duration=1', '-c:v', 'rawvideo',
                        '-pix_fmt', 'yuv420p', str(source)], check=True, capture_output=True)
        reference = subprocess.run([str(ffmpeg), '-v', 'error', '-i', str(source),
                                    '-f', 'rawvideo', '-pix_fmt', 'rgba', 'pipe:1'],
                                   check=True, capture_output=True).stdout
        numbers = [0, 3, 8, 3, 0]
        got = self.frames(client, 'frames.avi', [n * 100 for n in numbers])
        size = 32 * 24 * 4
        self.assertEqual([f['rgba_crc32'] for f in got],
                         [zlib.crc32(reference[n * size:(n + 1) * size]) for n in numbers])


if __name__ == '__main__':
    unittest.main()
