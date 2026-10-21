"""Adjacent padded IMG tiles must stay opaque under viewport projection."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_scene_branches import pack_replay,npk,img_header,rec,op
from test_default_layer_camera import draw43

ROOT=Path(__file__).resolve().parents[1]

class TileProjectionTests(unittest.TestCase):
    def test_zoom_sampling_keeps_existing_inset_when_projection_cancels(self):
        folder=ROOT/'validation/font_floor_20261021'/uuid.uuid4().hex[:8];folder.mkdir(parents=True)
        pixels=bytes([0,0,255,255,0,255,0,255,255,0,0,255,0,255,255,255])*4
        image=img_header(2,36,1)+rec(16,5,4,4,len(pixels),x=0,y=0,full_w=4,full_h=4)+pixels
        (folder/'sprite_test.NPK').write_bytes(npk(image))
        draw,aux=draw43(10,10,0)
        raw=op(50,struct.pack('<2fI3f',0,0,0,32,32,2))+draw
        header=b'\x0b\0'+struct.pack('<8h',*([16,16]*4))+b'\x0c'+struct.pack('<H',6)+bytes(126)
        path=folder/'zoom.rep';path.write_bytes(pack_replay(1.8,{0:raw},[(0,(0,),aux)],['sprite/test/frame.img'],header))
        result=subprocess.run([str(ROOT/'build/rep_gpu.exe'),'--consume',str(path),str(path.with_suffix('.json')),str(path.with_suffix('.rgba')),str(folder)],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        actual=path.with_suffix('.rgba').read_bytes();pixel=tuple(actual[(3*16+2)*4:(3*16+2)*4+3])
        self.assertEqual(pixel,(159,96,0))

    def test_camera_projection_scale_does_not_sample_transparent_img_padding(self):
        folder=ROOT/'validation/font_floor_20261021'/uuid.uuid4().hex[:8];folder.mkdir(parents=True)
        pixels=bytes([0,0,255,255])*16
        image=img_header(2,36,1)+rec(16,5,4,4,len(pixels),x=0,y=0,full_w=4,full_h=4)+pixels
        (folder/'sprite_test.NPK').write_bytes(npk(image))
        for width,height in ((14,16),(16,14),(14,14)):
            with self.subTest(camera=(width,height)):
                raw=op(50,struct.pack('<2fI3f',0,0,0,width,height,1));aux=b''
                for y in range(-4,17,4):
                    for x in range(-4,17,4):
                        draw,a=draw43(x,y,0);raw+=draw;aux+=a
                header=b'\x0b\0'+struct.pack('<8h',*([16,16]*4))+b'\x0c'+struct.pack('<H',6)+bytes(126)
                path=folder/f'{width}_{height}.rep';path.write_bytes(pack_replay(1.8,{0:raw},[(0,(0,),aux)],['sprite/test/frame.img'],header))
                report=path.with_suffix('.json');rgba=path.with_suffix('.rgba')
                result=subprocess.run([str(ROOT/'build/rep_gpu.exe'),'--consume',str(path),str(report),str(rgba),str(folder)],capture_output=True,text=True,timeout=15)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertTrue(json.loads(report.read_bytes())['exact_eof'])
                actual=rgba.read_bytes()
                self.assertEqual(set(zip(actual[0::4],actual[1::4],actual[2::4],actual[3::4])),{(255,0,0,255)})

if __name__=='__main__':unittest.main()
