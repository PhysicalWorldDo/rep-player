"""Native HUD sprite color and cropped Quad2 placement contracts."""
import struct
import sys
import unittest
import uuid
from pathlib import Path

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_scene_branches as scene
from test_scene_branches import op,pixel_camera,legacy

ROOT=Path(__file__).resolve().parents[1]

def modern(opcode,x=4,y=5,color=0xffffffff,minor=7,include_offset=False):
    mode=(opcode-42)%3
    if minor<7:
        data=bytearray((48,60,76)[mode]);struct.pack_into('<2I',data,0,0,0)
        struct.pack_into('<I',data,28 if mode<2 else 60,color)
        struct.pack_into('<2f',data,16 if mode<2 else 64,1,1)
        if mode==1:struct.pack_into('<2f',data,48,1,1)
        if mode==2:data[72]=include_offset
        raw=op(opcode,struct.pack('<I',0)+data)
    else:
        data=bytearray((40,52,64)[mode]);struct.pack_into('<I',data,28 if mode<2 else 52,color)
        struct.pack_into('<2f',data,16 if mode<2 else 56,1,1)
        if mode==1:struct.pack_into('<2f',data,44,1,1)
        if mode==2:data[8]=include_offset
        raw=op(opcode,struct.pack('<2I',len(data),0)+data)
    return raw,struct.pack('<2h',x,y)

class HudNativeFieldsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):scene.NativeSceneBranchTests.setUpClass.__func__(cls)
    render=scene.NativeSceneBranchTests.render
    assert_red_rectangle=scene.NativeSceneBranchTests.assert_red_rectangle

    def test_modern_sprite_colors_are_abgr_for_all_six_opcodes(self):
        for minor in (6,7):
            for opcode in range(42,48):
                for color,expected in ((0xff0000ff,(255,0,0)),(0xffff4800,(0,72,255)),(0xffd7b800,(0,184,215))):
                    with self.subTest(minor=minor,opcode=opcode,color=hex(color)):
                        draw,aux=modern(opcode,color=color,minor=minor)
                        mask=op(58,struct.pack('<Ii7f4B',0,0,1,1,0,0,0,0,0,0,0,0,0)) if opcode>=45 else b''
                        pixel=self.render([(pixel_camera()+mask+draw,aux)],version=1.8,minor=minor,resource='sprite/white/frame.img')
                        self.assertEqual(pixel(5,6),expected)

    def test_legacy_sprite_color_remains_argb(self):
        draw,aux=legacy(4,5,color=0xffff0000)
        pixel=self.render([(draw,aux)],resource='sprite/white/frame.img')
        self.assertEqual(pixel(5,6),(255,0,0))

    def test_quad2_retains_cropped_img_offsets(self):
        for minor in (6,7):
            for opcode in (44,47):
                with self.subTest(minor=minor,opcode=opcode):
                    draw,aux=modern(opcode,minor=minor)
                    mask=op(58,struct.pack('<Ii7f4B',0,0,1,1,-2,-3,0,0,0,0,0,0,0)) if opcode==47 else b''
                    pixel=self.render([(pixel_camera()+mask+draw,aux)],version=1.8,minor=minor,resource='sprite/offset/frame.img')
                    self.assert_red_rectangle(pixel,(6,8,11,11))

    def test_quad2_offset_extent_flag_is_not_horizontal_reflection(self):
        # Native Quad2 uses this byte to include IMG offsets in its extent;
        # it never calls the separate ordinary-Quad direction setter.
        for minor in (6,7):
            with self.subTest(minor=minor):
                draw,aux=modern(44,minor=minor,include_offset=True)
                pixel=self.render([(pixel_camera()+draw,aux)],version=1.8,minor=minor,resource='sprite/columns/frame.img')
                self.assertEqual(pixel(4,6),(255,0,0))
                self.assertEqual(pixel(7,6),(255,255,0))

if __name__=='__main__':unittest.main()
