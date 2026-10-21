"""115us metric callback regressions: context+28 is outline width, not size."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay,op

ROOT=Path(__file__).resolve().parents[1]

class FontLayoutNativeTests(unittest.TestCase):
    def font(self,font,size,outline,text):
        result=subprocess.run([str(ROOT/'build/rep_gpu.exe'),'--font',str(font),str(size),'400','0',str(outline),'1','1',text],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        return json.loads(result.stdout)['metrics']

    def test_latin_native_advances_include_style_outline_instead_of_height(self):
        self.assertEqual([m[2] for m in self.font(0,11,2,'Ab01 I')],[7,6,6,6,5,4])
        self.assertEqual([m[2] for m in self.font(0,11,8,'A A')],[7,9,7])

    def test_chinese_default_font_uses_the_same_native_metric_field(self):
        self.assertEqual([m[2] for m in self.font(0,12,2,'中文')],[12,12])

    def test_proportional_face_space_uses_style_outline(self):
        self.assertEqual([m[2] for m in self.font(2,12,2,'A B')],[9,5,8])

    def test_normal_glyph_cache_distinguishes_metric_outline(self):
        folder=ROOT/'validation/font_floor_20261021'/uuid.uuid4().hex[:8];folder.mkdir(parents=True)
        commands={}
        for n,border in enumerate((2,8)):
            words=(11,400,0,0,border,1,0,0xffffffff,0xff000000,0,0)
            commands[n]=op(48,struct.pack('<I11II2fB',0,*words,0,0,n*20,0))
        path=folder/'styles.rep';path.write_bytes(pack_replay(1.8,commands,[(0,(0,1),b'')],['A A'],b'\x0c'+struct.pack('<H',6)+bytes(126)))
        result=subprocess.run([str(ROOT/'build/rep_gpu.exe'),'--text-layout',str(path)],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual([q['x'] for q in json.loads(result.stdout)],[0,7,12,0,7,16])

if __name__=='__main__':unittest.main()
