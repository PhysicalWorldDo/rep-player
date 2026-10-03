import json
from pathlib import Path
import subprocess
import sys
import unittest
import zlib
import struct
import numpy as np
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from native_freetype import glyph
from font_backend import font_path
from font_backend import draw_text
from font_atlas import FontAtlas
from rep_protocol import TextOperation
from test_rep_protocol_versions import pack_replay
ROOT=Path(__file__).resolve().parents[1]
class NativeFontTests(unittest.TestCase):
    def test_native_text_layout_fixed_bitmap_negative_padding_and_styles(self):
        cases=[(11,400,0,0,2,1,0,0),(22,400,0,0,2,1,100,0),
               (15,400,2,16,2,4,0,0x100),(11,400,0,0,4,-1,0,0),
               (14,700,2,0,3,2,-700,1),(11,400,0,0,2,1,0,0x1010000),
               (0,0,0,0,2,0,0,0),(15,400,-1,0,2,1,0,0)]
        for size,weight,font,slant,border,padding,tracking,flags in cases:
            with self.subTest(size=size,font=font,padding=padding,flags=flags):
                words=(size,weight,font,slant,border,padding,tracking,0xffff0000,0xff000000,0xff0000ff,flags)
                raw=struct.pack('<2I',48,0)+struct.pack('<11I',*(w&0xffffffff for w in words))+struct.pack('<IB',0,0)
                header=b'\x0c'+struct.pack('<H',7)+bytes(126)
                fixture=ROOT/'validation'/'text_layout.rep'
                fixture.write_bytes(pack_replay(1.7,{0:raw},[(0,(0,),struct.pack('<2h',4,5))],['ii'],header=header))
                result=subprocess.run([str(ROOT/'build'/'rep_gpu.exe'),'--text-layout',str(fixture)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertTrue(result.stdout.strip(),'native text layout fixture is missing')
                got=json.loads(result.stdout)
                expected=[]
                def capture(coverage,x,y,scale,argb,margin,lower,**texture):
                    w,h=texture['texture_dimensions'];u0,v0,u1,v1=texture['texture_uv_rect']
                    expected.append(dict(x=x,y=y,scale=scale,width=coverage.shape[1],height=coverage.shape[0],
                                         rect=[round(u0*w),round(v0*h),round(u1*w),round(v1*h)],
                                         channel=texture['texture_channel'],gradient=lower is not None))
                operation=TextOperation(48,'ii',b'',0,x=4,y=5,fields={'font_parameters':words,'enabled':0})
                draw_text(np.zeros((100,100,3),np.uint8),operation,font_atlases={},glyph_renderer=capture)
                self.assertEqual(len(got),len(expected))
                for actual,reference in zip(got,expected):
                    for key in ('width','height','rect','channel','gradient'):self.assertEqual(actual[key],reference[key],key)
                    for key in ('x','y','scale'):self.assertAlmostEqual(actual[key],reference[key],places=4)
    def test_actual_atlas_switches_to_ba_and_all_four_gpu_planes(self):
        result=subprocess.run([str(ROOT/'build'/'rep_gpu.exe'),'--font-plane-atlas'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(result.stdout.strip(),'native atlas plane fixture is missing')
        got=json.loads(result.stdout)
        self.assertEqual(got['formats'],[5,6])
        self.assertEqual(got['alpha'],[[255]*4,[17]*4,[0,85,170,255],[119]*4])
    def test_native_glyph_metrics_and_packed_atlas(self):
        for font_id,size,weight,slant,outline,padding,scale,text in [(0,11,400,0,2,1,2,'Ab01'),(36,15,400,0,2,2,1,'Skill'),(4,18,400,5,1,1,1,'Test')]:
            with self.subTest(font_id=font_id):
                result=subprocess.run([str(ROOT/'build'/'rep_gpu.exe'),'--font',str(font_id),str(size),str(weight),str(slant),str(outline),str(padding),str(scale),text],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertTrue(result.stdout.strip(),'native font path is not implemented')
                got=json.loads(result.stdout)
                atlas=FontAtlas();metrics=[]
                for character in text:
                    n=glyph(font_path(font_id,weight),size,weight,font_id,slant,0,character)
                    b=glyph(font_path(font_id,weight),size,weight,font_id,slant,outline,character)
                    atlas.register((character,),n[0],b[0],padding,scale)
                    metrics.append(list(n[1:]))
                self.assertEqual(got['metrics'],metrics)
                self.assertEqual(got['atlas_crc32'],zlib.crc32(atlas.texture_rgba().tobytes()))
if __name__=='__main__':unittest.main()
