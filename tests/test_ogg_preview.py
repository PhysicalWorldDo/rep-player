"""Observe the actual XAudio2 submission buffers in an instrumented source copy."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
from ogg_fixtures import make_fixture

ROOT = Path(__file__).resolve().parents[1]


class OggPreviewSubmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.variant = 'red' if os.environ.get('OGG_PREVIEW_RED') == '1' else 'green'
        cls.folder = ROOT / 'validation/ogg_controls_20261031/audio/preview' / cls.variant
        cls.folder.mkdir(parents=True, exist_ok=True)
        ffmpeg = cls.folder / 'ffmpeg.exe'
        shutil.copy2(ROOT / 'build/ffmpeg.exe', ffmpeg)
        cls.fixture = make_fixture(cls.folder, ffmpeg)
        source = (ROOT / 'src/audio.cpp').read_text(encoding='utf8')
        source = source.replace('#include "audio_resources.hpp"', '#include "audio_resources.hpp"\nvoid observeOggSubmission(const void*,uint64_t,int64_t,const float*,size_t);', 1)
        marker = 'if(FAILED(voice->SubmitSourceBuffer(&submission)))'
        if source.count(marker) != 1:
            raise RuntimeError('Expected one actual XAudio2 buffer submission')
        source = source.replace(marker, 'observeOggSubmission(track.get(),serial,track->positionMilliseconds(),buffer.data(),buffer.size());\n                        ' + marker)
        if cls.variant == 'red':
            marker = 'track->setHiddenOggs(std::move(selection));'
            if source.count(marker) != 1:
                raise RuntimeError('Expected one worker filter application')
            source = source.replace(marker, '/* RED: simulate a worker that never applies the OGG selection. */')
        instrumented = cls.folder / 'observed_audio.cpp'
        instrumented.write_text(source, encoding='utf8')
        cls.exe = cls.folder / 'ogg_preview_probe.exe'
        command = [str(ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'), '-std=c++20', '-O2', '-municode', '-static', '-DNOMINMAX', '-I' + str(ROOT / 'vendor/zlib'), '-I' + str(ROOT / 'src'), str(ROOT / 'tests/ogg_preview_native.cpp'), str(ROOT / 'src/protocol.cpp'), str(ROOT / 'src/audio_resources.cpp'), str(instrumented), str(ROOT / 'vendor/zlib/libz.a'), '-lxaudio2_9', '-lole32', '-o', str(cls.exe)]
        built = subprocess.run(command, capture_output=True, text=True, env=dict(os.environ, TEMP=str(cls.folder), TMP=str(cls.folder)))
        (cls.folder / 'build.log').write_text(built.stdout + built.stderr, encoding='utf8')
        if built.returncode:
            raise RuntimeError(built.stdout + built.stderr)

    def test_device_submission_obeys_hidden_selection_and_raw_audition_finishes_once(self):
        result = subprocess.run([str(self.exe), str(self.fixture['client']), str(self.fixture['replay'])], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        (self.folder / 'submissions.json').write_text(json.dumps(data, indent=2), encoding='utf8')
        hidden = [b for b in data['hidden'] if b['serial'] == data['hiddenSerial']]
        enabled = [b for b in data['enabled'] if b['serial'] == data['enabledSerial']]
        audition = [b for b in data['audition'] if b['serial'] == data['soloSerial']]
        self.assertGreaterEqual(len(hidden), 3)
        self.assertEqual(max(b['peak'] for b in hidden), 0, 'Disabled OGG reached a real submitted device buffer')
        self.assertGreaterEqual(len(enabled), 3)
        self.assertGreater(max(b['peak'] for b in enabled), .01)
        self.assertGreater(max(b['peak'] for b in audition if b['end'] <= data['duration']), .01)
        tail = [b for b in audition if b['end'] >= data['duration'] + 10]
        self.assertTrue(tail, 'No complete buffer after the end of the raw audition was observed')
        self.assertEqual(max(b['peak'] for b in tail), 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
