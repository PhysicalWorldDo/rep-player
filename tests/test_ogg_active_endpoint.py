"""Tracking a buffer-end lifetime must preserve all submitted PCM samples."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
from ogg_fixtures import OGG_A, make_fixture, pack_replay

ROOT = Path(__file__).resolve().parents[1]


class ActiveOggEndpointPcmTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation/ogg_active_20261031/audio/endpoint'
        cls.folder.mkdir(parents=True, exist_ok=True)
        ffmpeg = cls.folder / 'ffmpeg.exe'
        shutil.copy2(ROOT / 'build/ffmpeg.exe', ffmpeg)
        fixture = make_fixture(cls.folder, ffmpeg)
        cls.client = fixture['client']
        (cls.client / 'audio.xml').write_text('<AUDIO><MUSIC ID="EDGE" FILE="' + OGG_A + '" LOOP_DELAY=".001" LOOP_TIMES="2"/></AUDIO>', encoding='utf8')
        decoded = subprocess.run([str(ffmpeg), '-v', 'error', '-i', str(fixture['oggA']), '-f', 'f32le', '-ac', '2', '-ar', '48000', '-'], capture_output=True, check=True)
        cls.frames = len(decoded.stdout) // 8
        # The source offset exceeds the exact decoded length plus the loop delay.
        offset = (cls.frames + 47) // 48 + 1
        sound = struct.pack('<II9i', 6, 36, 0, 0, 0, 1, 1, offset, 0, 25, -1)
        cls.replay = cls.folder / 'music-offset-buffer-end.rep'
        cls.replay.write_bytes(pack_replay({0: sound}, [(0, ()), (10, (0,)), (1000, ())], ['EDGE']))
        source = (ROOT / 'src/audio.cpp').read_text(encoding='utf8')
        source = source.replace('#include "audio_resources.hpp"', '#include "audio_resources.hpp"\nvoid observeActiveEndpointSubmission(const float*,size_t);', 1)
        marker = 'if(FAILED(voice->SubmitSourceBuffer(&submission)))'
        if source.count(marker) != 1:
            raise RuntimeError('Expected one actual XAudio2 buffer submission')
        source = source.replace(marker, 'observeActiveEndpointSubmission(buffer.data(),buffer.size());\n                        ' + marker)
        instrumented = cls.folder / 'observed_audio.cpp'
        instrumented.write_text(source, encoding='utf8')
        cls.exe = cls.folder / 'ogg_endpoint_probe.exe'
        command = [str(ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'), '-std=c++20', '-O2', '-municode', '-static', '-DNOMINMAX', '-I' + str(ROOT / 'vendor/zlib'), '-I' + str(ROOT / 'src'), str(ROOT / 'tests/ogg_active_endpoint_native.cpp'), str(ROOT / 'src/protocol.cpp'), str(ROOT / 'src/audio_resources.cpp'), str(instrumented), str(ROOT / 'vendor/zlib/libz.a'), '-lxaudio2_9', '-lole32', '-o', str(cls.exe)]
        result = subprocess.run(command, capture_output=True, text=True, env=dict(os.environ, TEMP=str(cls.folder), TMP=str(cls.folder)))
        (cls.folder / 'build.log').write_text(result.stdout + result.stderr, encoding='utf8')
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def test_music_offset_beyond_clip_at_buffer_end_keeps_original_pcm(self):
        result = subprocess.run([str(self.exe), str(self.client), str(self.replay)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        (self.folder / 'result.json').write_text(json.dumps(data, indent=2), encoding='utf8')
        self.assertEqual(data['samples'], 2880)
        self.assertEqual(data['different'], 0, 'Tracking the end boundary changed real submitted PCM')


if __name__ == '__main__':
    unittest.main(verbosity=2)
