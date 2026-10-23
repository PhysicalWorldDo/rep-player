"""Exercise actual preparation overlap and superseded inspection ownership."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import uuid
import wave

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).parent))
from test_scene_branches import pack_replay, legacy
from test_npk_priority import image, write_package, RED

ROOT = Path(__file__).resolve().parents[1]


class LoadingStageOneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT/'validation/load_stage1_20261023/controller'/uuid.uuid4().hex[:8]
        cls.folder.mkdir(parents=True)
        client = cls.folder/'client'
        packs = client/'ImagePacks2'; packs.mkdir(parents=True)
        write_package(packs/'sprite_test.NPK', {'sprite/test/frame.img': image(RED)})
        (client/'Replay').mkdir()
        (client/'Music').mkdir()
        with wave.open(str(client/'Music/tone.wav'), 'wb') as f:
            f.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            f.writeframes(struct.pack('<2h', 1024, 1024)*4800)
        (client/'audio.xml').write_text('<AUDIO><EFFECT ID="TONE" FILE="Music/tone.wav" LOOP_DELAY="0"/></AUDIO>', encoding='utf8')
        draw, aux = legacy()
        draw = bytearray(draw); struct.pack_into('<I', draw, 8, 1); draw = bytes(draw)
        sound = struct.pack('<II9i', 6, 36, 0, 0, 0, 7, 1, 0, 0, -1, -1)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16]*4))
        for name in ('a', 'b'):
            (client/'Replay'/f'{name}.rep').write_bytes(pack_replay(1.7, {0: sound, 1: draw},
                [(0, (0, 1), aux), (10000, (1,), aux)], ['TONE', 'sprite/test/frame.img'], header=header))
        (client/'Replay/heavy.rep').write_bytes(pack_replay(1.7, {1: draw},
            [(0, (1,)*5000, aux*5000)], ['TONE', 'sprite/test/frame.img'], header=header))
        cls.client = client
        shutil.copy2(ROOT/'build/ffmpeg.exe', cls.folder/'ffmpeg.exe')
        subprocess.run(['powershell', '-NoProfile', '-Command',
                        f"New-Item -ItemType Junction -Path '{cls.folder/'assets'}' -Target '{ROOT/'assets'}' | Out-Null"], check=True, capture_output=True)
        (cls.folder/'loading_measure.hpp').write_text('''#pragma once
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
extern std::mutex measureMutex;
extern std::condition_variable measureCondition;
extern bool measureAudioEntered,measureOverlap,measureInspectEntered,measureReleaseInspect;
extern std::string measureMode;
void measureImagePreparation();
void measureAudioPreparation(const std::function<bool()>& cancelled);
void measureInspection();
''', encoding='utf8')
        injected = {
            'engine': [('void Executor::prepare(const std::function<bool()>& cancelled){', 'measureImagePreparation();'),
                       ('ReplayInspection inspectReplayImages(Replay& replay,const std::function<bool()>& cancelled){', 'measureInspection();')],
            'audio': [('impl_->cancelled=cancelled;impl_->profile=replay.options.profile;', 'measureAudioPreparation(cancelled);')],
        }
        compiler = ROOT/'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        flags = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-municode', '-static',
                 '-I', str(ROOT/'src'), '-I', str(cls.folder), '-I', str(ROOT/'vendor/zlib'), '-I', str(ROOT/'vendor/freetype/include')]
        objects = []
        for name in ('protocol', 'resources', 'gpu', 'bindings', 'fonts', 'movies', 'engine', 'audio_resources', 'audio', 'ui_playback'):
            source = ROOT/'src'/f'{name}.cpp'
            if name in injected:
                text = source.read_text(encoding='utf8')
                for needle, addition in injected[name]:
                    if text.count(needle) != 1: raise RuntimeError(f'Missing boundary {needle}')
                    text = text.replace(needle, needle+addition)
                source = cls.folder/f'{name}.cpp'
                source.write_text('#include "loading_measure.hpp"\n'+text, encoding='utf8')
            obj = cls.folder/f'{name}.o'; objects.append(obj)
            result = subprocess.run([str(compiler), *flags, '-c', str(source), '-o', str(obj)], capture_output=True, timeout=120)
            if result.returncode: raise RuntimeError(result.stderr.decode(errors='replace'))
        cls.exe = cls.folder/'controller.exe'
        result = subprocess.run([str(compiler), *flags, str(ROOT/'tests/loading_stage1_native.cpp'), *map(str, objects),
            str(ROOT/'vendor/freetype/libfreetype.a'), str(ROOT/'vendor/zlib/libz.a'),
            '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lxaudio2_9', '-lole32', '-o', str(cls.exe)], capture_output=True, timeout=120)
        if result.returncode: raise RuntimeError(result.stderr.decode(errors='replace'))
        print('LOADING_CONTROLLER='+str(cls.folder), flush=True)

    def run_mode(self, mode):
        result = subprocess.run([str(self.exe), mode, str(self.folder), str(self.client)], capture_output=True, timeout=35)
        (self.folder/f'{mode}.log').write_bytes(result.stdout+result.stderr)
        self.assertEqual(result.returncode, 0, (result.stdout+result.stderr).decode(errors='replace'))
        self.assertTrue(json.loads(result.stdout)['pass'])

    def test_audio_preparation_overlaps_complete_image_preparation(self): self.run_mode('parallel')
    def test_new_open_does_not_wait_for_stale_inspection(self): self.run_mode('cancel')
    def test_inspection_cancels_inside_a_large_scene(self): self.run_mode('inspect_cancel')
    def test_image_error_cancels_parallel_audio_before_waiting(self): self.run_mode('error_cancel')


if __name__ == '__main__': unittest.main()
