"""Recorded font handles resolve to native family kinds, not identical IDs."""
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
from native_freetype import glyph
from font_backend import font_path
from test_fonts import native_metric_reference

ROOT=Path(__file__).resolve().parents[1]

class FontHandleTests(unittest.TestCase):
    def metrics(self,handle,size,text):
        result=subprocess.run([str(ROOT/'build/rep_gpu.exe'),'--font',str(handle),str(size),'400','0','2','1','1',text],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        return json.loads(result.stdout)['metrics']

    def test_tahoma_handle_zero_uses_kind26_metrics(self):
        self.assertEqual(self.metrics(0,11,'100%I'),[[1,1,6],[0,1,6],[0,1,6],[0,1,11],[0,1,4]])

    def test_tahoma_registered_aliases_keep_the_same_family(self):
        expected=self.metrics(0,15,'Shop 115')
        for handle in (7,8,22,24,26,27,34,35):
            with self.subTest(handle=handle):
                self.assertEqual(self.metrics(handle,15,'Shop 115'),expected)

    def test_blade_and_noto_handles_use_their_registered_family_kind(self):
        for handle,kind in ((1,6),(2,7),(3,8),(4,11),(5,12),(6,13),(36,10)):
            with self.subTest(handle=handle,kind=kind):
                expected=[list(native_metric_reference(font_path(handle),15,400,handle,0,0,c,2)[1:]) for c in 'Ab']
                self.assertEqual(self.metrics(handle,15,'Ab'),expected)

    def test_tahoma_large_text_is_not_dotum_fixed_bitmap(self):
        folder=ROOT/'validation/hud_details_20261021'/uuid.uuid4().hex[:8];folder.mkdir(parents=True)
        words=(22,400,0,0,2,1,0,0xffffffff,0xff000000,0,0)
        command=op(48,struct.pack('<I11II2fB',0,*words,0,0,0,0))
        path=folder/'large.rep';path.write_bytes(pack_replay(1.8,{0:command},[(0,(0,),b'')],['A'],b'\x0c'+struct.pack('<H',6)+bytes(126)))
        result=subprocess.run([str(ROOT/'build/rep_gpu.exe'),'--text-layout',str(path)],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        q=json.loads(result.stdout)[0]
        bitmap,left,top,_=glyph(font_path(0),22,400,26,0,0,'A')
        self.assertEqual((q['scale'],q['width'],q['height'],q['x'],q['y']),(1,bitmap.shape[1],bitmap.shape[0],left,top))

if __name__=='__main__':unittest.main()
