import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import uuid
import zlib
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay
from test_npk_reader import npk,img_header,rec
from rep_protocol import _default_draw_params
ROOT=Path(__file__).resolve().parents[1]
def op(number,data=b''):return struct.pack('<I',number)+data
def pixel_camera(layer=0):
    # Geometry tests use pixel coordinates, so initialize a native 2D camera.
    return op(50,struct.pack('<2fI3f',0,0,layer,16,16,1))
def legacy(x=0,y=0,color=0xffffffff,scale=(1,1),special=0,frame=0,pivot=(0,0),source=None,rotation=0,direction=0,opcode=3):
    p=bytearray(_default_draw_params(b''));struct.pack_into('<I',p,8,color);struct.pack_into('<2f',p,20,*scale);struct.pack_into('<2f',p,28,0,0);p[12]=special
    struct.pack_into('<h',p,4,frame);struct.pack_into('<2f',p,28,*pivot)
    struct.pack_into('<f',p,16,rotation);p[6]=direction
    if source:p[36]=1;struct.pack_into('<4h',p,38,*source)
    return op(opcode,struct.pack('<I',64)+p),struct.pack('<2h',x,y)
def extended(x=0,y=0,layer=0,mode=0,mirror=0,scale=(1,1),rotation=0,pivot=(0,0),opcode=42,extra=(0,1,1)):
    size=52 if opcode==43 else 40
    p=bytearray(size);struct.pack_into('<I',p,8,layer);p[4]=mode;p[12]=mirror
    struct.pack_into('<2f',p,16,*scale);struct.pack_into('<I',p,28,0xffffffff)
    struct.pack_into('<f',p,24,rotation);struct.pack_into('<2f',p,32,*pivot)
    if opcode==43:struct.pack_into('<3f',p,40,*extra)
    return op(opcode,struct.pack('<2I',size,0)+p),struct.pack('<2h',x,y)
class NativeSceneBranchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=ROOT/'validation'/('scene_branches_'+uuid.uuid4().hex[:8]);cls.folder.mkdir()
        cls.assets=cls.folder/'assets';cls.assets.mkdir()
        raw=bytes([0,0,255,255])*16
        image=img_header(2,36,1)+rec(16,5,4,4,len(raw),x=0,y=0,full_w=4,full_h=4)+raw
        (cls.assets/'sprite_test.NPK').write_bytes(npk(image))
        white=bytes([255,255,255,255])*16
        image=img_header(2,36,1)+rec(16,5,4,4,len(white),x=0,y=0,full_w=4,full_h=4)+white
        (cls.assets/'sprite_white.NPK').write_bytes(npk(image,'sprite/white/frame.img'))
        # Odd cropped dimensions and IMG offsets; the complete canvas is different.
        raw=bytes([0,0,255,255])*15
        image=img_header(2,36,1)+rec(16,5,5,3,len(raw),x=2,y=3,full_w=11,full_h=13)+raw
        (cls.assets/'sprite_offset.NPK').write_bytes(npk(image,'sprite/offset/frame.img'))
        # The same 4x4 image at an atlas offset, with distinct adjacent pixels.
        raw=bytes([255,0,0,255])*64
        raw=bytearray(raw)
        for y in range(2,6):
            for x in range(2,6):raw[(y*8+x)*4:(y*8+x+1)*4]=bytes([0,0,255,255])
        zipped=zlib.compress(raw)
        image=img_header(5,64,1)+struct.pack('<3I',1,0,0)
        image+=struct.pack('<7I',1,16,0,len(zipped),len(raw),8,8)
        image+=rec(16,7,4,4,0,x=0,y=0,full_w=4,full_h=4)+struct.pack('<7i',0,0,2,2,6,6,0)+zipped
        (cls.assets/'sprite_packed.NPK').write_bytes(npk(image,'sprite/packed/frame.img'))
        # Asymmetric source: red, green, blue, yellow columns.
        raw=bytes([0,0,255,255,0,255,0,255,255,0,0,255,0,255,255,255])*4
        image=img_header(2,36,1)+rec(16,5,4,4,len(raw),x=0,y=0,full_w=4,full_h=4)+raw
        (cls.assets/'sprite_columns.NPK').write_bytes(npk(image,'sprite/columns/frame.img'))
        raw=bytearray(bytes([0,0,0,0])*64)
        colors=[bytes([0,0,255,255]),bytes([0,255,0,255]),bytes([255,0,0,255]),bytes([0,255,255,255])]
        for y in range(2,6):
            for x in range(2,6):raw[(y*8+x)*4:(y*8+x+1)*4]=colors[x-2]
        zipped=zlib.compress(raw)
        image=img_header(5,64,1)+struct.pack('<3I',1,0,0)+struct.pack('<7I',1,16,0,len(zipped),len(raw),8,8)
        columns=image+rec(16,7,4,4,0,x=0,y=0,full_w=4,full_h=4)+struct.pack('<7i',0,0,2,2,6,6,0)+zipped
        (cls.assets/'sprite_atlas_columns.NPK').write_bytes(npk(columns,'sprite/atlas_columns/frame.img'))
        image+=rec(16,7,4,4,0,x=0,y=0,full_w=4,full_h=4)+struct.pack('<7i',0,0,2,2,6,6,1)+zipped
        (cls.assets/'sprite_rotated.NPK').write_bytes(npk(image,'sprite/rotated/frame.img'))
    def render(self,scenes,version=1.7,minor=7,resource='sprite/test/frame.img'):
        commands={};timeline=[]
        for n,(raw,aux) in enumerate(scenes):commands[n]=raw;timeline.append((n*10,(n,),aux))
        commands=dict(sorted(commands.items(),key=lambda p:len(p[1])))
        header=b'\x0b\0'+struct.pack('<8h',16,16,16,16,16,16,16,16)
        if version>=1.4:header+=b'\x0c'+struct.pack('<H',minor)+bytes(126)
        path=self.folder/(self._testMethodName+'.rep');path.write_bytes(pack_replay(version,commands,timeline,[resource],header=header))
        rgba=path.with_suffix('.rgba');report=path.with_suffix('.json')
        result=subprocess.run([str(ROOT/'build'/'rep_gpu.exe'),'--consume',str(path),str(report),str(rgba),str(self.assets)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.report=json.loads(report.read_text())
        self.assertTrue(self.report['exact_eof'])
        pixels=rgba.read_bytes()
        self.pixels=pixels
        return lambda x,y:tuple(pixels[(y*16+x)*4:(y*16+x)*4+3])
    def assert_red_rectangle(self,pixel,rect):
        left,top,right,bottom=rect
        for y in range(16):
            for x in range(16):
                expected=(255,0,0) if left<=x<right and top<=y<bottom else (0,0,0)
                self.assertEqual(pixel(x,y),expected,(x,y,rect))
    def rotation_draw(self,opcode,mode,x,y,scale,rotation,direction=0,extra=(0,1,1)):
        if opcode in (3,4):
            return legacy(x,y,scale=scale,pivot=(3,4),rotation=rotation,direction=direction,opcode=opcode)
        draw,aux=extended(x,y,mode=mode,mirror=direction,scale=scale,pivot=(3,4),rotation=rotation,opcode=opcode,extra=extra)
        return pixel_camera()+draw,aux
    def test_native_rotation_direction_and_scale_order_use_img_offsets(self):
        # Native virtual8: 1487E0640 -> 1487E0660 negates the angle before
        # D3DXMatrixRotationZ. 1487B5870 translates IMG offsets around the
        # explicit pivot; op3 is S*R and op4 is R*S in column notation.
        branches=((3,1),(4,0),(42,1),(42,0),(43,1),(43,0))
        for opcode,mode in branches:
            for angle,position,rects in (
                (math.pi/2,(5,8),((7,4,10,14),(6,8,12,13))),
                (-math.pi/2,(5,2),((6,4,9,14),(4,5,10,10))),
            ):
                with self.subTest(opcode=opcode,mode=mode,angle=angle):
                    draw,aux=self.rotation_draw(opcode,mode,*position,(1,2),angle)
                    pixel=self.render([(draw,aux)],resource='sprite/offset/frame.img')
                    self.assert_red_rectangle(pixel,rects[0 if mode else 1])
    def test_native_rotation_preserves_signed_scale_and_direction(self):
        branches=((3,1),(4,0),(42,1),(42,0),(43,1),(43,0))
        for opcode,mode in branches:
            for direction,rects in (
                (0,((6,4,9,14),(4,5,10,10))),
                (1,((7,4,10,14),(6,5,12,10))),
            ):
                with self.subTest(opcode=opcode,mode=mode,direction=direction):
                    draw,aux=self.rotation_draw(opcode,mode,5,2,(-1,-2),math.pi/2,direction)
                    pixel=self.render([(draw,aux)],resource='sprite/offset/frame.img')
                    self.assert_red_rectangle(pixel,rects[0 if mode else 1])
    def test_native_extra_rotation_sandwich_uses_negated_axis_angle(self):
        # 1487B4EA0 uses opposite rotations around extra anisotropic scale.
        # For angle pi/4 and scale(2,1), the column matrix is
        # [[1.5,-.5],[-.5,1.5]]. With pivot(3,4), offset(2,3), draw(5,8),
        # its quad corners are (7,11),(14.5,8.5),(5.5,15.5),(13,13).
        for mode in (0,1):
            with self.subTest(mode=mode):
                draw,aux=self.rotation_draw(43,mode,5,8,(1,1),0,extra=(math.pi/4,2,1))
                pixel=self.render([(draw,aux)],resource='sprite/offset/frame.img')
                self.assertEqual(pixel(12,10),(255,0,0))
                self.assertEqual(pixel(12,15),(0,0,0))
    def test_static_branch_draw_preserves_padded_source_extent(self):
        # Native 146F7FB30 delegates StaticBranch selector116 to builtin
        # program4. Flags=0 without a preset tail must still map cropped UVs.
        effect=op(19,b'\1'+struct.pack('<IIBII',16,1,0,0,0)+struct.pack('<f',0))
        for modern in (False,True):
            for scale,rect in (((1,1),(4,5,9,8)),((2,2),(4,6,14,12))):
                with self.subTest(modern=modern,scale=scale):
                    draw,aux=extended(0,0,scale=scale) if modern else legacy(0,0,scale=scale)
                    if scale==(1,1):
                        draw,aux=extended(2,2) if modern else legacy(2,2)
                    if modern:draw=pixel_camera()+draw
                    plain=self.render([(draw,aux)],resource='sprite/offset/frame.img')
                    self.assert_red_rectangle(plain,rect)
                    expected=self.pixels
                    pixel=self.render([(effect+draw+op(20),aux)],resource='sprite/offset/frame.img')
                    self.assert_red_rectangle(pixel,rect)
                    self.assertEqual(self.pixels,expected)
    def test_static_branch_draw_samples_offset_atlas_and_mirrored_columns(self):
        effect=op(19,b'\1'+struct.pack('<IIBII',16,1,0,0,0)+struct.pack('<f',0))
        colors=((255,0,0),(0,255,0),(0,0,255),(255,255,0))
        for modern in (False,True):
            for direction in (0,1):
                with self.subTest(modern=modern,direction=direction):
                    draw,aux=extended(2,2,mirror=direction,pivot=(2,0)) if modern else legacy(2,2,direction=direction,pivot=(2,0))
                    if modern:draw=pixel_camera()+draw
                    for prefix,suffix in ((b'',b''),(effect,op(20))):
                        pixel=self.render([(prefix+draw+suffix,aux)],resource='sprite/atlas_columns/frame.img')
                        for y in range(16):
                            for x in range(16):
                                column=5-x if direction else x-2
                                expected=colors[column] if 2<=x<6 and 2<=y<6 else (0,0,0)
                                self.assertEqual(pixel(x,y),expected,(x,y,direction))
    def test_static_branch_draw_keeps_rotated_atlas_rows_and_signed_scale(self):
        effect=op(19,b'\1'+struct.pack('<IIBII',16,1,0,0,0)+struct.pack('<f',0))
        # Physical atlas columns become logical rows through the IMG rotate90
        # mapping: top to bottom is yellow, blue, green, red.
        colors=((255,255,0),(0,0,255),(0,255,0),(255,0,0))
        for modern in (False,True):
            for sy in (1,-1):
                with self.subTest(modern=modern,scale_y=sy):
                    draw,aux=extended(2,2,scale=(1,sy),pivot=(0,2)) if modern else legacy(2,2,scale=(1,sy),pivot=(0,2))
                    if modern:draw=pixel_camera()+draw
                    for prefix,suffix in ((b'',b''),(effect,op(20))):
                        pixel=self.render([(prefix+draw+suffix,aux)],resource='sprite/rotated/frame.img')
                        for y in range(16):
                            for x in range(16):
                                row=y-2 if sy==1 else 5-y
                                expected=colors[row] if 2<=x<6 and 2<=y<6 else (0,0,0)
                                self.assertEqual(pixel(x,y),expected,(x,y,sy))
    def test_layer_camera_final_state_persists_and_legacy_ignores_it(self):
        draw,aux=extended(4,4)
        camera=op(50,struct.pack('<2fI3f',4,4,0,16,16,1))
        pixel=self.render([(draw+camera,aux),(draw,aux)])
        self.assertEqual(pixel(0,0),(255,0,0));self.assertEqual(pixel(5,5),(0,0,0))
        old,oldaux=legacy(4,4)
        pixel=self.render([(camera+old,oldaux)])
        self.assertEqual(pixel(4,4),(255,0,0));self.assertEqual(pixel(0,0),(0,0,0))
    def test_null_effect_pushes_preserve_previous_effect_and_pop_phantoms(self):
        effect=op(19,b'\1'+struct.pack('<IIBII',26,1,0,0,0)+struct.pack('<f',.5))
        draw,aux=legacy()
        for kind in (8,9,18,19,22,43,45,62,63):
            with self.subTest(kind=kind):
                null=op(19,b'\1'+struct.pack('<IIBII',kind,0,0,0,0))
                pixel=self.render([(effect+null+draw+op(20)+op(20),aux)])
                self.assertIn(pixel(1,1)[0],(88,89,90));self.assertEqual(pixel(8,8),(0,0,0))
    def test_stencil_write_is_invisible_and_tests_only_nonzero_alpha(self):
        mask,aux=legacy(2,2)
        draw,aux2=legacy(0,0,color=0xff00ff00,scale=(3,3))
        raw=op(26,b'\0')+op(28,b'\0\0\1')+mask+op(27,b'\0')+op(0,struct.pack('<2I',5,0x0000ff00))+draw+op(1)+op(29,b'\0')
        pixel=self.render([(raw,aux+aux2)])
        self.assertEqual(pixel(3,3),(0,255,0));self.assertEqual(pixel(0,0),(0,0,0));self.assertEqual(pixel(8,8),(0,0,0))
    def test_capture_alpha_masks_sprite_on_gpu(self):
        capture=op(58,struct.pack('<Ii7f4B',0,0,1,1,2,2,0,0,0,0,0,0,0))
        draw,aux=legacy(scale=(3,3))
        off=bytearray(40);struct.pack_into('<2f',off,16,3,3);struct.pack_into('<I',off,28,0xffffffff)
        pixel=self.render([(pixel_camera()+capture+op(45,struct.pack('<2I',40,0)+off)+op(59),aux)])
        self.assertEqual(pixel(3,3),(255,0,0));self.assertEqual(pixel(0,0),(0,0,0))
    def test_actor207_uses_own_transparent_targets(self):
        draw,aux=legacy(2,2)
        pixel=self.render([(draw+op(50,struct.pack('<2fI3f',0,0,207,16,16,1)),aux)])
        self.assertEqual(pixel(3,3),(255,0,0))
        self.assertEqual(self.report['actor_pool_updates'],12)
    def test_all_native_blend_factors_and_pipeline_stack_restore(self):
        background,a=legacy(color=0xff406080)
        foreground,b=legacy(color=0x80c86432)
        source=[200/255,100/255,50/255];dest=[64/255,96/255,128/255];alpha=128/255
        factors={0:(lambda s,d:s*alpha+d*(1-alpha)),1:(lambda s,d:s*d+d*alpha),
                 2:(lambda s,d:s*alpha+d),4:(lambda s,d:s*(1-s)+d*(1-alpha)),
                 8:(lambda s,d:s*(1-d)),32:(lambda s,d:s*(1-alpha)+d),64:(lambda s,d:d),
                 128:(lambda s,d:s*alpha),256:(lambda s,d:s+d*(1-alpha)),
                 512:(lambda s,d:s+d*alpha),1024:(lambda s,d:d*s),32768:(lambda s,d:s)}
        for flag,formula in factors.items():
            with self.subTest(flag=flag):
                raw=op(51,struct.pack('<H',32768))+background+op(52,b'\0\0')+op(51,struct.pack('<H',flag))+foreground+op(52,b'\0\0')
                pixel=self.render([(raw,a+b)],resource='sprite/white/frame.img')
                expected=[round(min(1,max(0,formula(s,d)))*255) for s,d in zip(source,dest)]
                self.assertTrue(all(abs(g-e)<=1 for g,e in zip(pixel(1,1),expected)),(flag,pixel(1,1),expected))
        raw=background+op(51,struct.pack('<H',2))+foreground+op(51,struct.pack('<H',16))+foreground+op(52,b'\0\0')+op(52,b'\0\0')
        pixel=self.render([(raw,a+b+b)],resource='sprite/white/frame.img')
        expected=[round(min(1,d+2*s*alpha)*255) for s,d in zip(source,dest)]
        self.assertTrue(all(abs(g-e)<=1 for g,e in zip(pixel(1,1),expected)),(pixel(1,1),expected))
    def test_pipeline_tint_alpha_and_clip_change_actual_gpu_pixels(self):
        draw,aux=legacy(color=0xffffffff,scale=(2,2))
        pixel=self.render([(op(53,struct.pack('<HI',1,0xff336699))+draw,aux)],resource='sprite/white/frame.img')
        self.assertEqual(pixel(2,2),(153,102,51))
        pixel=self.render([(op(53,struct.pack('<HI',2,32))+draw,aux)],resource='sprite/white/frame.img')
        self.assertEqual(pixel(2,2),(32,32,32))
        pixel=self.render([(op(12)+op(2,struct.pack('<4h',2,2,4,4))+draw,aux)],resource='sprite/white/frame.img')
        self.assertEqual(pixel(2,2),(255,255,255));self.assertEqual(pixel(1,2),(0,0,0));self.assertEqual(pixel(4,2),(0,0,0))
    def test_sampler_applies_scale_and_retains_offset(self):
        draw,aux=extended(1,1,layer=10)
        sampler=op(61,struct.pack('<BBHI6f',1,1,0,0,2,1,3,0,0,0))
        pixel=self.render([(pixel_camera(10)+op(63,b'\1')+sampler+draw+op(62),aux)])
        self.assertEqual(pixel(5,1),(255,0,0));self.assertEqual(pixel(1,1),(0,0,0))
    def test_native_resource_grid_stretches_selected_intervals(self):
        draw,aux=legacy(scale=(2,2))
        grid=op(32,struct.pack('<hhB3hB2hIi',0,0,3,1,3,99,2,1,3,0,0))
        pixel=self.render([(grid+draw,aux)])
        self.assertEqual(pixel(7,7),(255,0,0));self.assertEqual(pixel(9,9),(0,0,0))
    def test_factory_null_capture_and_cache_draws_skip(self):
        native=bytearray(40);struct.pack_into('<2f',native,16,1,1);struct.pack_into('<I',native,28,0xffffffff)
        cache=bytes(80)+_default_draw_params(b'')+bytes(4)
        pixel=self.render([(op(45,struct.pack('<2I',40,0)+native)+op(21,cache),struct.pack('<4h',0,0,0,0))])
        self.assertEqual(pixel(1,1),(0,0,0))
    def test_special_quad_preserves_selected_source_origin(self):
        draw,aux=legacy(2,2,special=1,pivot=(3,0),source=(1,0,4,4))
        pixel=self.render([(draw,aux)],resource='sprite/columns/frame.img')
        # Native keeps cropRatio .75 in the reflection matrix: 2.25px halves.
        self.assertEqual(pixel(2,3),(0,0,0));self.assertEqual(pixel(7,3),(0,0,0))
        self.assertEqual(pixel(3,3),(0,85,170));self.assertEqual(pixel(6,3),(0,85,170))
        self.assertEqual(pixel(4,3),pixel(5,3))
    def test_static_glow_keeps_native_geometry_and_atlas_mapping(self):
        values=[2.]+[0.]*16
        values[1+10]=1.
        values[1+6]=values[1+7]=1.
        values[1+12:1+16]=[1.,0.,0.,1.]
        effect=op(19,b'\1'+struct.pack('<IIBII',16,len(values),0,0,0)+struct.pack('<17f',*values))
        draw,aux=legacy(6,6)
        pixel=self.render([(effect+draw+op(20),aux)])
        # Original PS118 remaps local UV*3-1 within the original quad.
        self.assertEqual(pixel(7,7),(127,0,0))
        # UV=.625 remaps beyond the right/bottom half-texel bounds.
        self.assertEqual(pixel(8,8),(0,0,0))
        self.assertEqual(pixel(5,5),(0,0,0));self.assertEqual(pixel(10,10),(0,0,0))
        packed=self.render([(effect+draw+op(20),aux)],resource='sprite/packed/frame.img')
        self.assertEqual(packed(7,7),pixel(7,7))
        self.assertEqual(packed(8,8),pixel(8,8))
    def test_rotated_special_quad_uses_physical_source_and_swaps_extents(self):
        draw,aux=legacy(2,2,special=1,pivot=(2,0),source=(1,0,4,2),scale=(4/3,2))
        pixel=self.render([(draw,aux)],resource='sprite/rotated/frame.img')
        self.assertGreater(pixel(2,4)[1],100)
        self.assertEqual(pixel(2,4)[0],0)
        self.assertEqual(pixel(2,5),(0,0,0))
    def test_rotated_vertical_reflection_uses_right_hand_physical_strip(self):
        draw,aux=legacy(2,2,special=2,pivot=(0,1))
        pixel=self.render([(draw,aux)],resource='sprite/rotated/frame.img')
        self.assertEqual(pixel(3,2),(255,255,0))
        self.assertEqual(pixel(3,3),(255,255,0))
    def test_special_quad_keeps_negative_pivot_and_directed_extent(self):
        draw,aux=legacy(3,2,special=1,pivot=(-1,0))
        pixel=self.render([(draw,aux)])
        self.assertEqual(pixel(1,3),(255,0,0))
        self.assertEqual(pixel(2,3),(255,0,0))
        self.assertEqual(pixel(3,3),(0,0,0))
    def test_sampler_applies_to_each_ninepatch_cell(self):
        draw,aux=extended(1,1,layer=10,scale=(2,2))
        grid=op(32,struct.pack('<hhB2hB2hIi',0,0,2,1,3,2,1,3,0,0))
        sampler=op(61,struct.pack('<BBHI6f',1,1,0,0,1,1,4,0,0,0))
        pixel=self.render([(pixel_camera(10)+grid+op(63,b'\1')+sampler+draw+op(62),aux)])
        self.assertEqual(pixel(5,1),(255,0,0));self.assertEqual(pixel(1,1),(0,0,0))
    def test_text_glow_second_pass_clears_entire_effect_stack(self):
        words=(11,400,0,0,2,1,0,0xffff0000,0xff000000,0xff0000ff,0x100)
        text=op(48,struct.pack('<I11IIB',0,*words,0,0));aux=struct.pack('<2h',2,1)
        self.render([(pixel_camera()+text,aux)],resource='H');plain=self.pixels
        self.assertTrue(any(plain[0::4]))
        previous=op(19,b'\1'+struct.pack('<IIBII',26,1,0,0,0)+struct.pack('<f',0))
        glow=op(19,b'\1'+struct.pack('<IIBII',30,8,0,0,0)+bytes(32))
        self.render([(pixel_camera()+previous+glow+text+op(20)+op(20),aux)],resource='H')
        self.assertEqual(self.pixels,plain)
if __name__=='__main__':unittest.main()
