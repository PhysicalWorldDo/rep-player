"""A selected scene and its final publication use one coherent applied IMG source."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import unittest
import uuid
import zlib

ROOT=Path(__file__).resolve().parents[1]
LOGICAL='sprite/source/frame.img'

def image(colors):
    record=struct.pack('<9i',16,5,2,2,16,0,0,2,2)
    pixels=b''.join(bytes((b,g,r,a))*4 for r,g,b,a in colors)
    return b'Neople Img File\0'+struct.pack('<4I',36*len(colors),0,2,len(colors))+record*len(colors)+pixels

def npk(data):
    seed=(b'puchikon@neople dungeon and fighter '+b'DNF'*100)[:255]+b'\0'
    encoded=bytes(a^b for a,b in zip(LOGICAL.encode().ljust(256,b'\0'),seed))
    return b'NeoplePack_Bill\0'+struct.pack('<I',1)+struct.pack('<II',284,len(data))+encoded+data

def replay(draw_both):
    commands=struct.pack('<I2fI3f',50,0,0,0,16,16,1)
    auxiliary=b''
    for frame,x in ([(0,1),(1,7)] if draw_both else [(0,1)]):
        params=bytearray(64)
        struct.pack_into('<h',params,4,frame)
        struct.pack_into('<I',params,8,0xffffffff)
        for offset in (20,24,52,56):struct.pack_into('<f',params,offset,1)
        commands+=struct.pack('<II',3,64)+params
        auxiliary+=struct.pack('<2h',x,1)
    dictionary=struct.pack('<IIH',1,0,len(commands))+commands+struct.pack('<IBH',1,1,len(LOGICAL))+LOGICAL.encode()+b'\0'
    timeline=struct.pack('<iIII',0,4,0,len(auxiliary))+auxiliary
    header=b'\x0b\0'+struct.pack('<8h',*([16]*8))+b'\x0c'+struct.pack('<H',7)+bytes(126)
    payload=b'\1'+struct.pack('<f',1.7)+b'\0'+struct.pack('<I',len(header))+header
    for section in (timeline,dictionary):
        zipped=zlib.compress(section);payload+=struct.pack('<I',len(zipped))+zipped
    return struct.pack('<II',zlib.crc32(payload),len(payload))+payload

class LoadingSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=ROOT/'validation/load_complete_20261029/sources'/uuid.uuid4().hex[:8]
        cls.folder.mkdir(parents=True)
        source=cls.folder/'src'
        shutil.copytree(ROOT/'src',source)
        compiler=ROOT/'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        flags=['-std=c++20','-O2','-static','-DNOMINMAX','-DUNICODE','-D_UNICODE','-municode','-I',source,'-I',ROOT/'vendor/zlib','-I',ROOT/'vendor/freetype/include']
        env=dict(os.environ,TEMP=str(cls.folder),TMP=str(cls.folder))
        objects=[]
        for name in ('protocol','resources','gpu','bindings','fonts','movies','engine'):
            obj=cls.folder/(name+'.o')
            build=subprocess.run(list(map(str,[compiler,*flags,'-c',source/(name+'.cpp'),'-o',obj])),capture_output=True,timeout=120,env=env)
            (cls.folder/(name+'_build.log')).write_bytes(build.stdout+build.stderr)
            if build.returncode:raise RuntimeError(build.stderr.decode(errors='replace'))
            objects.append(obj)
        cls.exe=cls.folder/'loading_sources.exe'
        build=subprocess.run(list(map(str,[compiler,*flags,ROOT/'tests/loading_sources_native.cpp',*objects,ROOT/'vendor/freetype/libfreetype.a',ROOT/'vendor/zlib/libz.a','-ld3d11','-ldxgi','-ld3dcompiler','-ldxguid','-o',cls.exe])),capture_output=True,timeout=120,env=env)
        (cls.folder/'link.log').write_bytes(build.stdout+build.stderr)
        if build.returncode:raise RuntimeError(build.stderr.decode(errors='replace'))
        print('SOURCE_OUTPUT='+str(cls.folder),flush=True)

    def check(self,mode):
        folder=self.folder/mode
        packs=folder/'ImagePacks2'
        packs.mkdir(parents=True)
        (packs/'sprite_source.NPK').write_bytes(npk(image([(255,0,0,255),(0,255,0,255)])))
        (packs/'!patch.NPK').write_bytes(npk(image([(0,0,255,255)])))
        recording=folder/'source.rep';recording.write_bytes(replay(mode=='mixed'))
        result=subprocess.run(list(map(str,[self.exe,mode,packs,recording,ROOT/'assets/shaders',folder/'cache'])),capture_output=True,text=True,encoding='utf8',timeout=45)
        (folder/'result.log').write_text(result.stdout+result.stderr,encoding='utf8')
        if result.stdout.strip():(folder/'result.json').write_text(result.stdout,encoding='utf8')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        data=json.loads(result.stdout)
        self.assertEqual(data['actual_crc'],data['official_crc'])

    def test_later_missing_patch_frame_restarts_entire_target_with_official_source(self):self.check('mixed')
    def test_shared_index_rejection_during_commit_cannot_install_old_patch_pixels(self):self.check('gate')

if __name__=='__main__':unittest.main()
