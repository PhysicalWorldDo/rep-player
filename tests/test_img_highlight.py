"""Real GPU source-contour contract, exercised against both desktop render cores."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).parent))
from test_scene_branches import pack_replay, npk, img_header, rec, legacy, op
ROOT = Path(__file__).resolve().parents[1]
PRISM = ROOT.parent / 'dnf_skill_prism'

class ImgHighlightTests(unittest.TestCase):
    def test_source_contour_and_preview_isolation(self):
        prism = os.environ.get('HIGHLIGHT_CORE') == 'prism'
        project = PRISM if prism else ROOT
        folder = project / 'validation' / 'img_highlight_20261020' / uuid.uuid4().hex[:8]
        folder.mkdir(parents=True)
        assets = folder / 'client/ImagePacks2'
        assets.mkdir(parents=True)
        shutil.copytree(ROOT/'assets/shaders',folder/'assets/shaders')
        raw = bytearray(bytes((0, 0, 0, 255)) * 64)
        raw[4:8] = bytes((0,0,255,255))
        raw[8:12] = bytes((255,255,255,0))
        raw[12:16] = bytes((128,128,128,128))
        for y in range(2,6):
            for x in range(2,6):
                raw[(y*8+x)*4:(y*8+x+1)*4] = bytes((255, 0, 255, 0))
        image = img_header(2,36,1) + rec(16,5,8,8,len(raw),x=0,y=0,full_w=8,full_h=8) + raw
        (assets / 'sprite_test.NPK').write_bytes(npk(image))
        one, aux1 = legacy(8,8)
        two, aux2 = legacy(22,8)
        replay = folder / 'highlight.rep'
        header = b'\x0b\0' + struct.pack('<8h', *([32,32]*4))
        rotated, ar = legacy(15,10,rotation=.6,scale=(1.3,.8),direction=1)
        cropped, ac = legacy(4,6,source=(1,0,7,8),scale=(1.4,1.2))
        cropped = op(60,struct.pack('<4i',6,9,14,15)) + cropped
        gridded, ag = legacy(8,9,scale=(2,1.5))
        gridded = op(32,struct.pack('<hhB2hB2hIi',0,0,2,1,3,2,1,3,0,0)) + gridded
        additive, aa = legacy(8,8)
        additive = op(0,struct.pack('<II',2,0)) + additive + op(1)
        multiply, am = legacy(8,8)
        multiply = op(0,struct.pack('<II',1,0)) + multiply + op(1)
        dictionary={0:one+two,1:rotated,2:cropped,3:gridded,4:additive,5:multiply}
        replay.write_bytes(pack_replay(1.7,dict(sorted(dictionary.items(),key=lambda item:len(item[1]))),
            [(0,(0,),aux1+aux2),(10,(1,),ar),(20,(2,),ac),(30,(3,),ag),(40,(4,),aa),(50,(5,),am)],['sprite/test/frame.img'],header=header))
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        names = ('protocol','resources','gpu','bindings','fonts','movies','engine')
        objects = [project / 'build' / (('rep_' if prism else '') + name + '.o') for name in names]
        binary = folder / 'highlight_probe.exe'
        command = [str(compiler),'-std=c++20','-O2','-DNOMINMAX','-DUNICODE','-D_UNICODE','-municode','-static',
                   '-I',str(project / ('native/rep' if prism else 'src')),'-I',str(ROOT/'vendor/zlib'),
                   '-I',str(ROOT/'vendor/freetype/include'),str(ROOT/'tests/highlight_native.cpp'),*map(str,objects),
                   str(ROOT/'vendor/freetype/libfreetype.a'),str(ROOT/'vendor/zlib/libz.a'),
                   '-ld3d11','-ldxgi','-ld3dcompiler','-ldxguid','-o',str(binary)]
        build = subprocess.run(command,capture_output=True,text=True,timeout=60)
        self.assertEqual(build.returncode,0,build.stderr)
        result = subprocess.run([str(binary),str(ROOT/'assets/shaders'),str(assets),str(replay),str(folder)],capture_output=True,text=True,timeout=30)
        (folder/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        print(result.stdout+result.stderr,flush=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        # Compile the actual render-thread controller as well, with its public API.
        native = project / ('native/rep' if prism else 'src')
        controller = folder / 'controller_probe.exe'
        controller_command = command[:]
        controller_command[controller_command.index(str(ROOT/'tests/highlight_native.cpp'))] = str(ROOT/'tests/highlight_controller_native.cpp')
        position = controller_command.index(str(ROOT/'vendor/freetype/libfreetype.a'))
        controller_command[position:position] = [str(native/(name+'.cpp')) for name in ('ui_playback','audio','audio_resources')]
        controller_command[-1] = str(controller)
        controller_command[-2:-2] = ['-lxaudio2_9','-lole32']
        if prism:
            controller_command.insert(1,'-DPRISM_HIGHLIGHT')
        build = subprocess.run(controller_command,capture_output=True,text=True,timeout=60)
        self.assertEqual(build.returncode,0,build.stderr)
        result = subprocess.run([str(controller),str(folder),str(assets.parent),str(replay)],capture_output=True,text=True,timeout=30)
        (folder/'controller.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        print(result.stdout+result.stderr,flush=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__ == '__main__':
    unittest.main()
